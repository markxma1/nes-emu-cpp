///   Copyright 2016 Xma1
///
///   This file is part of NES-C#.
///
///   NES-C# is free software: you can redistribute it and/or modify
///   it under the terms of the GNU General Public License as published by
///   the Free Software Foundation, either version 3 of the License, or
///   (at your option) any later version.
///
///   NES-C# is distributed in the hope that it will be useful,
///   but WITHOUT ANY WARRANTY; without even the implied warranty of
///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
///   See the GNU General Public License for more details.
///
///   You should have received a copy of the GNU General Public License
///   along with NES-C#. If not, see http://www.gnu.org/licenses/.
///
/// @brief Mapper-correctness harness, not a port of anything - new test-only
/// code (same spirit as tests/nestest/nestest_check.cpp) exercising every
/// implemented Mapper (Mappers/*.h) against synthetic PRG/CHR data instead
/// of a real .nes file, so the exact byte at any given CPU/PPU address can
/// be predicted and checked without needing a real ROM dump on disk.
///
/// Every synthetic PRG-ROM/CHR-ROM array used here is filled with a simple
/// repeating ramp - byte at ROM offset `p` is `p & 0xFF` - so "is the right
/// bank/window currently mapped at this CPU/PPU address" reduces to "does
/// this byte equal (expected ROM offset) & 0xFF".
///
/// Covers two things per mapper: (1) the bank-switching behaviour itself,
/// against each mapper's cited nesdev page (see Mappers/Mapper_*.cpp for the
/// exact citations), and (2) a regression test for the PRG-ROM
/// write-corruption bug fixed in Mapper::Install() (see its own FIXED note)
/// - found while investigating why every real commercial ROM that switches
/// PRG banks (Contra/UxROM, Chip 'n Dale/MMC1, Batman III/MMC3, The Lion
/// King/AxROM) stayed on a permanently black screen: real cartridge PRG-ROM
/// is read-only hardware, so a CPU write anywhere in $8000-$FFFF must never
/// change the byte NES_Memory::Memory[] reports at that address unless a
/// mapper's own bank-switch logic legitimately repaints that exact address
/// as part of handling the write.
#include "INES.h"
#include "Interrupt.h"
#include "MapperFactory.h"
#include "NES_CPU.h"
#include "NES_Console.h"
#include "NES_Memory.h"
#include "NES_PPU.h"
#include "NES_PPU_Memory.h"
#include "NES_Register.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace NES;

namespace
{
    int failures = 0;

    void Check(bool condition, const std::string& what)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << what << std::endl;
            ++failures;
        }
    }

    /// A ramp buffer: byte at offset `i` is `i & 0xFF`, so any window copied
    /// from it can be identified purely from where in the buffer it came
    /// from, without a real .nes file.
    std::vector<uint8_t> Ramp(size_t size)
    {
        std::vector<uint8_t> v(size);
        for (size_t i = 0; i < size; ++i)
            v[i] = static_cast<uint8_t>(i & 0xFF);
        return v;
    }

    uint8_t PrgByte(uint16_t cpuAddress) { return NES_Memory::Memory[cpuAddress]->value(); }
    uint8_t ChrByte(int ppuAddress) { return NES_PPU_Memory::PatternTable[static_cast<size_t>(ppuAddress)]->value(); }

    /// Simulates a real CPU STA instruction: goes through AddressSetup::Value()
    /// (the hooked setter every Assembly_6502 STA_* handler uses - see
    /// CPU/CPU/Assembly_6502.cpp), not value() - a raw value() write would
    /// skip the mapper's AfterSet hook entirely and not be a real test of
    /// register-write handling.
    void CpuWrite(uint16_t address, uint8_t value)
    {
        NES_Memory::Memory[address]->Value(value);
    }

    void TestNROM()
    {
        auto prg = Ramp(32768);
        auto mapper = CreateMapper(0);
        mapper->Install(prg, {});

        Check(PrgByte(0x8000) == 0x00, "NROM: $8000 should be prg[0]");
        Check(PrgByte(0xBFFF) == static_cast<uint8_t>(0x3FFF & 0xFF), "NROM: $BFFF should be prg[0x3FFF]");
        Check(PrgByte(0xC000) == static_cast<uint8_t>(0x4000 & 0xFF), "NROM: $C000 should be prg[0x4000]");
        Check(PrgByte(0xFFFF) == static_cast<uint8_t>(0x7FFF & 0xFF), "NROM: $FFFF should be prg[0x7FFF]");

        // Regression test: PRG-ROM is read-only hardware - a CPU write
        // anywhere in $8000-$FFFF must never actually change what's stored
        // there (see Mapper::Install()'s FIXED note).
        CpuWrite(0x9000, 0xAA);
        Check(PrgByte(0x9000) == static_cast<uint8_t>(0x1000 & 0xFF),
              "NROM: a CPU write must not corrupt read-only PRG-ROM");
    }

    void Test16kNROMMirrors()
    {
        auto prg = Ramp(16384);
        auto mapper = CreateMapper(0);
        mapper->Install(prg, {});

        Check(PrgByte(0x8000) == PrgByte(0xC000), "NROM 16KB: $8000 and $C000 must mirror the same byte");
        Check(PrgByte(0xBFFF) == PrgByte(0xFFFF), "NROM 16KB: $BFFF and $FFFF must mirror the same byte");
    }

    void TestUxROM()
    {
        auto prg = Ramp(8 * 16384); // 8 banks
        auto mapper = CreateMapper(2);
        mapper->Install(prg, {});

        // Power-on default: low bank = bank 0, high bank fixed = last bank (7).
        Check(PrgByte(0x8000) == 0x00, "UxROM: $8000 should start on bank 0");
        Check(PrgByte(0xC000) == static_cast<uint8_t>((7 * 16384) & 0xFF), "UxROM: $C000 fixed to the last bank");
        Check(PrgByte(0xFFFF) == static_cast<uint8_t>((7 * 16384 + 16383) & 0xFF), "UxROM: $FFFF still the last bank");

        // Any address in $8000-$FFFF selects the low bank (http://wiki.nesdev.com/w/index.php/UxROM).
        CpuWrite(0x8123, 3);
        Check(PrgByte(0x8000) == static_cast<uint8_t>((3 * 16384) & 0xFF), "UxROM: register write should switch the low bank");
        Check(PrgByte(0xBFFF) == static_cast<uint8_t>((3 * 16384 + 16383) & 0xFF), "UxROM: low bank window is a full 16KB");
        Check(PrgByte(0xC000) == static_cast<uint8_t>((7 * 16384) & 0xFF), "UxROM: high bank must stay fixed after a low-bank switch");

        // Regression test: a bank-select write landing inside the *fixed*
        // $C000-$FFFF bank (the exact failure mode found via Contra, whose
        // own bank-select writes land at $FFD0/$FFD1) must not corrupt that
        // fixed bank's real ROM byte, even though this register write is
        // legitimately selecting a (different) low-bank window that doesn't
        // cover $FFD0 at all.
        uint8_t before = PrgByte(0xFFD0);
        CpuWrite(0xFFD0, 5);
        Check(PrgByte(0xFFD0) == before, "UxROM: a register write inside the fixed bank must not corrupt it");
        Check(PrgByte(0x8000) == static_cast<uint8_t>((5 * 16384) & 0xFF), "UxROM: that same write must still have switched the low bank");
    }

    void TestCNROM()
    {
        auto prg = Ramp(32768);
        auto chr = Ramp(4 * 8192); // 4 banks
        auto mapper = CreateMapper(3);
        mapper->Install(prg, chr);

        Check(PrgByte(0x8000) == 0x00, "CNROM: PRG is fixed, $8000 should be prg[0]");
        Check(ChrByte(0x0000) == 0x00, "CNROM: CHR should start on bank 0");

        CpuWrite(0x8000, 2);
        Check(ChrByte(0x0000) == static_cast<uint8_t>((2 * 8192) & 0xFF), "CNROM: register write should switch the CHR bank");
        Check(ChrByte(0x1FFF) == static_cast<uint8_t>((2 * 8192 + 8191) & 0xFF), "CNROM: CHR window is a full 8KB");

        // Regression test: CNROM's WriteRegister() only ever repaints CHR,
        // never PRG - so a CHR-select write's own CPU-side address must be
        // protected by Mapper::Install()'s generic fix, not by anything
        // CNROM-specific.
        Check(PrgByte(0x8000) == 0x00, "CNROM: a CHR-select write must not corrupt read-only PRG-ROM at its own address");
    }

    /// Regression test for the real bug found live via Tiny Toon Adventures
    /// (an MMC3 game): one sprite (the player) rendered perfectly while
    /// another sprite on the same screen was a fragmented mess of unrelated
    /// pixel blocks, confirmed via a user-captured save state and reproduced
    /// twice. Root cause: NES_PPU::DecodeSpriteTileFresh()/
    /// DecodeBackgroundTileFresh() cache decoded tiles keyed by (tile index,
    /// palette, bank) - a *within-frame* cache purely for performance (see
    /// NES_PPU.Tile.cpp's own comment on why going fully uncached was a
    /// measured, severe regression) - but it was only ever cleared once per
    /// whole frame, with no way to notice that a mapper had just remapped
    /// the *underlying* CHR data a (tile index, bank) pair points to.
    /// Mappers that support CHR banking commonly do this *mid-frame*
    /// (Mapper::WriteChrWindow() is the single choke point every one of
    /// them funnels through - MMC1, MMC3, CNROM, UxROM, NROM), so a tile
    /// decoded before such a switch and referenced again afterward
    /// incorrectly reused stale pre-switch pixels. Fixed by clearing both
    /// fresh-tile caches from WriteChrWindow() itself. Exercised here via
    /// CNROM (the simplest CHR-banked mapper) rather than MMC3 directly, to
    /// isolate "does a CHR bank switch invalidate the cache" from MMC3's own
    /// unrelated bank-arithmetic complexity.
    void TestChrBankSwitchInvalidatesFreshTileCache()
    {
        std::vector<uint8_t> chr(2 * 8192, 0x00);
        chr[0] = 0x80; // bank 0, tile 0, pixel (0,0): low-bitplane bit 7 set -> palette index 1
        // bank 1 (offset 8192) stays all-zero -> tile 0 decodes fully palette-index-0 (transparent)
        auto prg = Ramp(32768);
        auto mapper = CreateMapper(3); // CNROM
        mapper->Install(prg, chr);

        NES_PPU_Memory::SpritePalette[1]->Value(0x01); // palette group 0, index 1: a real, non-transparent NES color
        NES::NES_PPU::ClearFreshTileCaches();

        NES::NES_PPU::Color bank0Color = NES::NES_PPU::DecodeSpriteTileFresh(0, 0, 0).GetPixel(0, 0);
        Check(!(bank0Color == NES::NES_PPU::Color::Transparent()),
              "sanity: CNROM bank 0's tile 0 must decode as non-transparent before the bank switch");

        CpuWrite(0x8000, 1); // CNROM: switch the whole 8KB CHR window to bank 1 (all-zero tile 0)
        NES::NES_PPU::Color afterSwitchColor = NES::NES_PPU::DecodeSpriteTileFresh(0, 0, 0).GetPixel(0, 0);

        Check(afterSwitchColor == NES::NES_PPU::Color::Transparent(),
              "DecodeSpriteTileFresh(): a CHR bank switch (WriteChrWindow()) must invalidate the fresh-tile cache - "
              "re-decoding the same (tile, palette, bank) key afterward returned the stale pre-switch color instead "
              "of reflecting the newly-mapped (all-zero/transparent) CHR data");
    }

    void TestAxROM()
    {
        auto prg = Ramp(4 * 32768); // 4 banks
        auto mapper = CreateMapper(7);
        mapper->Install(prg, {});

        Check(PrgByte(0x8000) == 0x00, "AxROM: should start on bank 0");

        CpuWrite(0x8000, 0x02); // bank 2, single-screen A (bit4=0)
        Check(PrgByte(0x8000) == static_cast<uint8_t>((2 * 32768) & 0xFF), "AxROM: register write should switch the full 32KB window");
        Check(PrgByte(0xFFFF) == static_cast<uint8_t>((2 * 32768 + 32767) & 0xFF), "AxROM: window covers all of $8000-$FFFF");
        Check(INES::arrangement == INES::Mirror::single_screen_a, "AxROM: bit4=0 should select single-screen A");

        CpuWrite(0x8000, 0x11); // bank 1, single-screen B (bit4=1)
        Check(PrgByte(0x8000) == static_cast<uint8_t>((1 * 32768) & 0xFF), "AxROM: a second register write should re-switch the bank");
        Check(INES::arrangement == INES::Mirror::single_screen_b, "AxROM: bit4=1 should select single-screen B");
    }

    /// Performs one real MMC1 register write: 5 separate CPU writes, one bit
    /// of `regValue` (LSB first) per write, the 5th one's *address* deciding
    /// which internal register gets committed - see Mapper_MMC1.cpp's
    /// WriteRegister() and http://wiki.nesdev.com/w/index.php/MMC1.
    void MMC1Write(uint16_t address, uint8_t regValue)
    {
        for (int i = 0; i < 5; ++i)
            CpuWrite(address, static_cast<uint8_t>((regValue >> i) & 0x01));
    }

    void TestMMC1()
    {
        auto prg = Ramp(8 * 16384); // 8x16KB = 128KB, like the real Chip 'n Dale ROM tested by hand this session
        auto mapper = CreateMapper(1);
        mapper->Install(prg, {});

        // Power-on default (control=0x0C -> PRG mode 3): fix the *last*
        // bank at $C000, bank register (0) switches $8000.
        Check(PrgByte(0x8000) == 0x00, "MMC1: $8000 should start on bank 0");
        Check(PrgByte(0xC000) == static_cast<uint8_t>((7 * 16384) & 0xFF), "MMC1: $C000 should start fixed to the last bank");

        // Switch $8000's 16KB bank via the PRG-bank register ($E000-$FFFF).
        MMC1Write(0xE000, 0x03);
        Check(PrgByte(0x8000) == static_cast<uint8_t>((3 * 16384) & 0xFF), "MMC1: PRG-bank register should switch the $8000 window");
        Check(PrgByte(0xC000) == static_cast<uint8_t>((7 * 16384) & 0xFF), "MMC1: $C000 must stay fixed to the last bank in PRG mode 3");

        // Regression test: the first 4 (of 5) writes of the *next* register
        // write must not leave any corrupted byte behind at their own
        // address, even though WriteRegister() does nothing observable
        // until the 5th write commits.
        uint8_t before = PrgByte(0xE000);
        for (int i = 0; i < 4; ++i)
            CpuWrite(0xE000, static_cast<uint8_t>((0x05 >> i) & 0x01));
        Check(PrgByte(0xE000) == before,
              "MMC1: an in-progress (not yet committed) shift-register write must not corrupt PRG-ROM");
        CpuWrite(0xE000, static_cast<uint8_t>((0x05 >> 4) & 0x01)); // 5th write commits bank 5
        Check(PrgByte(0x8000) == static_cast<uint8_t>((5 * 16384) & 0xFF), "MMC1: the 5th write should commit the shifted-in bank number");

        // Switch to 32KB PRG mode (control bits 2-3 = 0 or 1) and verify the
        // whole window moves together.
        MMC1Write(0x8000, 0x00); // control: mirroring=single_screen_a, prgMode=0 (32KB), chrMode=0
        MMC1Write(0xE000, 0x02); // bank register 2 -> 32KB bank (2>>1)=1
        Check(PrgByte(0x8000) == static_cast<uint8_t>((32768) & 0xFF), "MMC1: 32KB mode should switch the whole window from bank register bit 1+");
        Check(PrgByte(0xFFFF) == static_cast<uint8_t>((32768 + 32767) & 0xFF), "MMC1: 32KB mode window covers all of $8000-$FFFF");

        // A reset write (bit 7 set) must abort any in-progress shift
        // sequence and force PRG mode back to 3 (fixed-last-bank-at-$C000) -
        // see Mapper_MMC1.cpp's WriteRegister() comment.
        CpuWrite(0xE000, 0x80);
        Check(PrgByte(0xC000) == static_cast<uint8_t>((7 * 16384) & 0xFF), "MMC1: reset (bit 7) must force PRG mode 3 (fixed last bank at $C000)");

        // Regression test for the swapped vertical/horizontal mirroring
        // bug fixed in Mapper_MMC1.cpp's ApplyMirroring() (see its own
        // FIXED note) - found live via Chip and Dale's background
        // desyncing from the level's real collision map. Per
        // https://www.nesdev.org/wiki/MMC1#Control_(internal,_$8000-$9FFF),
        // control-register mirroring bits 0-1: 0=one-screen lower,
        // 1=one-screen upper, 2=vertical, 3=horizontal.
        MMC1Write(0x8000, 0x02); // control: mirroring=2 (vertical), prgMode=0, chrMode=0
        Check(INES::arrangement == INES::Mirror::vertical, "MMC1: control register mirroring=2 should select vertical mirroring");
        MMC1Write(0x8000, 0x03); // control: mirroring=3 (horizontal)
        Check(INES::arrangement == INES::Mirror::horisontal, "MMC1: control register mirroring=3 should select horizontal mirroring");
    }

    /// Regression test for Interrupt::SuppressNMI() / Mapper_MMC1's use of
    /// it in WriteRegister() (see both their own FIXED notes) - a residual
    /// recurrence of Chip 'n Dale's already-documented shift-register
    /// corruption bug, found live via a save-state capture: an NMI landing
    /// between two writes of the game's own 5-write MMC1 bank-switch burst
    /// interleaved with it and corrupted Mapper_MMC1's shared
    /// shiftRegister/writeCount state, committing a garbage register value
    /// and permanently stuck the game's own cooperative task scheduler.
    /// Verifies the actual guarantee: a pending NMI must not be delivered
    /// while an MMC1 register write burst is only partially complete
    /// (writeCount 1-4), but must still be delivered - not silently dropped
    /// - once the burst finishes.
    void TestMmc1BurstSuppressesNmiDelivery()
    {
        auto prg = Ramp(8 * 16384);
        auto mapper = CreateMapper(1);
        mapper->Install(prg, {});

        // Main "loop": an infinite self-jump at $0200, in RAM (never
        // touched by the mapper) so its exact opcode content can't be
        // stomped by a PRG-bank commit - keeps PC pinned at a single known
        // address across an unbounded number of Step() calls instead of
        // needing a step-budget-sized run of NOPs.
        constexpr uint16_t kMainLoop = 0x0200;
        NES_Memory::Memory[kMainLoop]->value(0x4C); // JMP
        NES_Memory::Memory[kMainLoop + 1]->value(static_cast<uint8_t>(kMainLoop));
        NES_Memory::Memory[kMainLoop + 2]->value(static_cast<uint8_t>(kMainLoop >> 8));

        // NMI handler: also in RAM, for the same reason.
        constexpr uint16_t kHandler = 0x0300;
        NES_Memory::Memory[kHandler]->value(0xEA); // NOP
        NES_Memory::Memory[kHandler + 1]->value(0x40); // RTI
        auto SetNmiVector = [&]() {
            NES_Memory::Memory[0xFFFA]->value(static_cast<uint8_t>(kHandler));
            NES_Memory::Memory[0xFFFB]->value(static_cast<uint8_t>(kHandler >> 8));
        };
        SetNmiVector();

        NES_Register::PC = kMainLoop;
        NES_Register::S = 0xFD;
        NES_Register::P.P = 0x24;
        Interrupt::NMI(false);
        Interrupt::SuppressNMI(false);

        for (int i = 0; i < 3; ++i)
            NES_CPU::Step();

        Interrupt::NMI(true); // request an NMI

        // Start, but don't finish, a real 5-write MMC1 burst (4 of 5).
        for (int i = 0; i < 4; ++i)
            CpuWrite(0xE000, static_cast<uint8_t>((0x05 >> i) & 0x01));

        // Generous budget (same margin TestNmiReentrancyGuard uses for
        // SevenClock's ~7-9 step interrupt-response delay): the burst is
        // still incomplete, so NMI must stay pending, not enter the handler.
        for (int i = 0; i < 20; ++i)
            NES_CPU::Step();
        Check(NES_Register::PC != kHandler,
              "MMC1 burst guard: NMI must not be delivered while a register write burst is incomplete");
        Check(Interrupt::NMI(),
              "MMC1 burst guard: the pending NMI request must not be silently dropped while suppressed");

        // Complete the burst (5th write) - this commits the register (also
        // re-paints the mapper's PRG window, which can overlap $FFFA/$FFFB
        // depending on PRG mode - re-point the vector fresh afterward;
        // that's test-only bookkeeping, not part of the guarantee under
        // test).
        CpuWrite(0xE000, static_cast<uint8_t>((0x05 >> 4) & 0x01));
        SetNmiVector();

        bool entered = false;
        for (int i = 0; i < 20 && !entered; ++i)
        {
            NES_CPU::Step();
            entered = NES_Register::PC == kHandler;
        }
        Check(entered, "MMC1 burst guard: a pending NMI must be delivered once the register write burst completes");
    }

    void TestMMC3()
    {
        auto prg = Ramp(8 * 8192); // 8 x 8KB = 64KB
        auto chr = Ramp(8 * 1024); // 8 x 1KB = 8KB
        auto mapper = CreateMapper(4);
        mapper->Install(prg, chr);

        // Power-on default (bankSelect=0, all R=0): PRG mode 0 -> $8000=R6,
        // $A000=R7, $C000=second-to-last, $E000=last. With R6=R7=0 that's
        // bank 0 at both $8000 and $A000.
        Check(PrgByte(0x8000) == 0x00, "MMC3: $8000 should start on R6=0");
        Check(PrgByte(0xE000) == static_cast<uint8_t>((7 * 8192) & 0xFF), "MMC3: $E000 fixed to the last bank");
        Check(PrgByte(0xC000) == static_cast<uint8_t>((6 * 8192) & 0xFF), "MMC3: $C000 fixed to the second-to-last bank in PRG mode 0");

        // Select R6 (bank-select value 6, PRG mode bit clear) then write
        // bank data 3 into it via the $8000 (even=select)/$8001 (odd=data) pair.
        CpuWrite(0x8000, 0x06);
        CpuWrite(0x8001, 0x03);
        Check(PrgByte(0x8000) == static_cast<uint8_t>((3 * 8192) & 0xFF), "MMC3: R6 bank-data write should switch $8000");

        // PRG mode bit (bit 6 of bank-select) swaps $8000 and $C000's roles.
        CpuWrite(0x8000, 0x40 | 0x06); // select R6, PRG mode 1
        CpuWrite(0x8001, 0x02);
        Check(PrgByte(0xC000) == static_cast<uint8_t>((2 * 8192) & 0xFF), "MMC3: PRG mode 1 should route R6 to $C000");
        Check(PrgByte(0x8000) == static_cast<uint8_t>((6 * 8192) & 0xFF), "MMC3: PRG mode 1 should fix $8000 to the second-to-last bank");

        // CHR: R0 selects a 2KB window at $0000 (CHR A12 inversion bit clear).
        CpuWrite(0x8000, 0x00); // select R0, PRG mode 0
        CpuWrite(0x8001, 0x02); // R0 = 2 (1KB units, low bit ignored for the 2KB window)
        Check(ChrByte(0x0000) == static_cast<uint8_t>((2 * 1024) & 0xFF), "MMC3: R0 bank-data write should switch the $0000 CHR window");

        // Mirroring register ($A000, even).
        // FIXED (real bug, see Mapper_MMC3::WriteMirroring()'s own FIXED
        // note for the full story): this test previously asserted the
        // opposite mapping, matching the bug it was written to prevent
        // regressing on. Cross-checked against FCEUX 2.6.6 running the
        // exact same ROM (Tiny Toon Adventures, CRC32 0x99dddb04) with a
        // byte-identical input sequence: the game writes $A000=$01 once
        // early on and never changes it again, and FCEUX's own live
        // nametable RAM (read via its Lua ppu.readbyte) shows $2000/$2400
        // holding identical content afterward - the pairing
        // INES::Mirror::horisontal produces here (see
        // TestNameTableMirroringBankPairing() above), not ::vertical.
        CpuWrite(0xA000, 0x01);
        Check(INES::arrangement == INES::Mirror::horisontal, "MMC3: mirroring register bit0=1 should select horizontal mirroring (2000/2400 paired) - verified against FCEUX");
        CpuWrite(0xA000, 0x00);
        Check(INES::arrangement == INES::Mirror::vertical, "MMC3: mirroring register bit0=0 should select vertical mirroring (2000/2800 paired) - verified against FCEUX");

        // Regression test: a bank-select write ($8000, even) must not
        // corrupt the fixed $E000 bank it never touches, and an odd write
        // to the *PRG-RAM-protect* register ($A001, accepted but not acted
        // on - see WriteRegister()'s comment) must not corrupt anything either.
        uint8_t beforeE000 = PrgByte(0xE000);
        CpuWrite(0x8000, 0x40 | 0x07); // select R7, PRG mode 1 - doesn't touch $E000
        Check(PrgByte(0xE000) == beforeE000, "MMC3: a bank-select write must not corrupt the fixed $E000 bank");
        uint8_t beforeA001 = PrgByte(0xA001);
        CpuWrite(0xA001, 0xAA);
        Check(PrgByte(0xA001) == beforeA001, "MMC3: PRG-RAM-protect write (accepted, unacted-on) must not corrupt PRG-ROM");
    }

    /// Regression test for Mapper_MMC3::OnScanline()'s scanline-IRQ
    /// clocking (see its own comment for the full before/after story) -
    /// added specifically because this project's own investigation into
    /// permanently-black MMC3 titles found the counter/reload/enable
    /// register semantics themselves (http://wiki.nesdev.com/w/index.php/MMC3
    /// "IRQ registers") to be the concrete, checkable part of that fix.
    ///
    /// UPDATE (later in the same overall effort - see NES_PPU::AdvanceDots()):
    /// OnScanline() replaced the older OnFrame() (a 240-clock burst
    /// approximating one whole frame's worth of scanlines in one shot) -
    /// this test now calls it 240 times in a loop (one real visible
    /// scanline's worth of frame each) instead of once, exercising the
    /// same real per-scanline cadence NES_PPU::OnScanlineStart() now
    /// drives it at in production.
    void TestMMC3Irq()
    {
        auto prg = Ramp(8 * 8192);
        auto mapper = CreateMapper(4);
        mapper->Install(prg, {});

        auto ClockOneFramesWorthOfScanlines = [&]() {
            for (int i = 0; i < 240; ++i)
                mapper->OnScanline();
        };

        Interrupt::IRQ(false);
        CpuWrite(0xE000, 0x00); // disable (also acknowledges) - start from a clean slate
        CpuWrite(0xC000, 0x04); // IRQ latch = 4
        CpuWrite(0xC001, 0x00); // IRQ reload (forces a reload on the next clock)
        ClockOneFramesWorthOfScanlines();
        Check(!Interrupt::IRQ(), "MMC3 IRQ: must not fire while disabled, no matter how the counter is clocked");

        CpuWrite(0xE001, 0x00); // enable
        ClockOneFramesWorthOfScanlines(); // 240 real scanlines - latch=4 guarantees at least one 0 well within that
        Check(Interrupt::IRQ(),
              "MMC3 IRQ: must fire at least once per frame's worth of scanlines once enabled, with a small latch value");

        CpuWrite(0xE000, 0x00); // disable - must also acknowledge the pending IRQ
        Check(!Interrupt::IRQ(), "MMC3 IRQ: writing the disable register must acknowledge (clear) a pending IRQ");

        // Re-enable with a latch large enough that one frame's worth of
        // scanlines can't possibly reach 0 (240 scanlines < 250), proving
        // OnScanline() really clocks the counter down once per call rather
        // than firing unconditionally.
        Interrupt::IRQ(false);
        CpuWrite(0xC000, 0xFA); // latch = 250
        CpuWrite(0xC001, 0x00); // reload
        CpuWrite(0xE001, 0x00); // enable
        ClockOneFramesWorthOfScanlines();
        Check(!Interrupt::IRQ(),
              "MMC3 IRQ: must not fire when the latch value exceeds one frame's worth of real per-scanline clocks");
    }

    /// Writes a minimal, valid NROM iNES file (16KB PRG, CHR-RAM) whose only
    /// interesting property is its mirroring bit, and returns its path.
    std::string WriteSyntheticNROM(const std::filesystem::path& path, bool verticalMirroring)
    {
        std::vector<uint8_t> file(16 + 16384, 0);
        file[0] = 'N'; file[1] = 'E'; file[2] = 'S'; file[3] = 0x1A;
        file[4] = 1; // 1x16KB PRG
        file[5] = 0; // CHR-RAM
        // Per INES::VRAMMirroring() (see its own FIXED note, confirmed
        // against https://www.nesdev.org/wiki/INES "Flags 6" bit 0):
        // bit0=0 -> horizontal mirroring, bit0=1 -> vertical mirroring (the
        // classic iNES "arrangement" vs "mirroring" naming swap makes the
        // raw bit look backwards from the mirroring type it actually
        // selects). bits4-7 stay 0 (mapper low nibble = NROM).
        file[6] = verticalMirroring ? 0x01 : 0x00;
        file[7] = 0;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        return path.string();
    }

    /// Regression test for the bug reported live by the project's user after
    /// this session's mapper work: switching ROMs through the in-app picker
    /// (NES/main.cpp's switchRom(), which just calls NES_Console::LoadRom()
    /// again on the same already-initialized console - see NES_ROM.cpp's own
    /// FIXED note for the full explanation) left the *previous* ROM's
    /// nametable-mirroring wiring in place for any newly loaded ROM whose own
    /// mapper never re-asserts it itself (NROM/UxROM/CNROM never do - only
    /// MMC1/MMC3/AxROM have a runtime mirroring register). Reported
    /// symptom - not visible on the freshly loaded title screen, only once
    /// real gameplay scrolling crossed into a nametable half that was still
    /// wired to a stale physical bank from the *previous* game - matches
    /// exactly what this test checks for.
    void TestRomSwitchRewiresMirroring()
    {
        auto dir = std::filesystem::temp_directory_path();
        std::string romVertical = WriteSyntheticNROM(dir / "mapper_check_vertical.nes", true);
        std::string romHorizontal = WriteSyntheticNROM(dir / "mapper_check_horizontal.nes", false);

        NES_Console::LoadRom(romVertical);
        Check(INES::arrangement == INES::Mirror::vertical, "ROM switch: vertical ROM's header should be read correctly");
        // Vertical mirroring pairs $2000/$2800 (slots 0/2, left column) onto
        // one physical bank and $2400/$2C00 (slots 1/3, right column) onto
        // the other - https://www.nesdev.org/wiki/Mirroring (see
        // RewireNameTableMirroring()'s own FIXED note) - mark it so the
        // next load can check whether slot 2 still points at it (correct
        // only in vertical mode).
        NES_PPU_Memory::NameTableN[0][5]->value(0xAB);
        Check(NES_PPU_Memory::NameTableN[2][5]->value() == 0xAB,
              "ROM switch: vertical mirroring should alias slots 0 and 2 (left column) onto the same physical bank");

        NES_Console::LoadRom(romHorizontal);
        Check(INES::arrangement == INES::Mirror::horisontal, "ROM switch: loading a second ROM should update INES::arrangement to its own header");
        // Horizontal mirroring pairs $2000/$2400 (slots 0/1, top row) onto
        // one bank and $2800/$2C00 (slots 2/3, bottom row) onto the other -
        // so slot 2 must now share a bank with slot 3, *not* slot 0 - it
        // must no longer show the marker written above. Before the
        // original fix this regression test still covers, LoadRom() never
        // re-ran RewireNameTableMirroring(), so slot 2 stayed aliased to
        // the *first* ROM's bank and this read back 0xAB - the exact
        // "leftover graphics from the previous game" bug.
        Check(NES_PPU_Memory::NameTableN[2][5]->value() != 0xAB,
              "ROM switch: loading a new ROM must re-wire mirroring instead of keeping the previous ROM's");
    }

    /// Direct regression test for the swapped iNES header bit 0 mapping
    /// fixed in INES::VRAMMirroring() (see its own FIXED note) - a *second*,
    /// independent bug from the one RewireNameTableMirroring() has, found
    /// live only after fixing that one: it broke Galaga (an NROM game,
    /// whose mirroring is entirely header-driven) even though it fixed
    /// Chip and Dale (an MMC1 game, whose mirroring instead comes from
    /// Mapper_MMC1::ApplyMirroring()) - the two bugs had been silently
    /// cancelling out for every header-mirrored ROM. Confirmed against
    /// https://www.nesdev.org/wiki/INES ("Flags 6", bit 0): bit0=0 is
    /// horizontal mirroring, bit0=1 is vertical mirroring.
    void TestINESHeaderMirroringBit()
    {
        auto dir = std::filesystem::temp_directory_path();
        std::string romBit0 = WriteSyntheticNROM(dir / "mapper_check_bit0.nes", false);
        std::string romBit1 = WriteSyntheticNROM(dir / "mapper_check_bit1.nes", true);

        NES_Console::LoadRom(romBit0);
        Check(INES::arrangement == INES::Mirror::horisontal,
              "INES::VRAMMirroring(): header bit 0 = 0 must select horizontal mirroring");

        NES_Console::LoadRom(romBit1);
        Check(INES::arrangement == INES::Mirror::vertical,
              "INES::VRAMMirroring(): header bit 0 = 1 must select vertical mirroring");
    }

    /// Direct regression test for the swapped bankForSlot pairing bug fixed
    /// in NES_PPU_Memory::RewireNameTableMirroring() (see its own FIXED
    /// note) - the actual root cause behind a real, reproducible gameplay
    /// bug (Chip and Dale's background rendering split left/right instead
    /// of top/bottom while INES::arrangement correctly read "vertical").
    /// Confirmed against https://www.nesdev.org/wiki/Mirroring: vertical
    /// mirroring pairs $2000/$2800 (left column, slots 0/2) and $2400/$2C00
    /// (right column, slots 1/3); horizontal mirroring pairs $2000/$2400
    /// (top row, slots 0/1) and $2800/$2C00 (bottom row, slots 2/3).
    void TestNameTableMirroringBankPairing()
    {
        auto mapper = CreateMapper(0); // NROM: never touches mirroring itself
        auto prg = Ramp(16384);
        mapper->Install(prg, {});

        INES::arrangement = INES::Mirror::vertical;
        NES_PPU_Memory::RewireNameTableMirroring();
        NES_PPU_Memory::NameTableN[0][0]->value(0x11);
        NES_PPU_Memory::NameTableN[1][0]->value(0x22);
        Check(NES_PPU_Memory::NameTableN[2][0]->value() == 0x11,
              "vertical mirroring: slot 2 (bottom-left) must share slot 0's (top-left) bank - left column pairing");
        Check(NES_PPU_Memory::NameTableN[3][0]->value() == 0x22,
              "vertical mirroring: slot 3 (bottom-right) must share slot 1's (top-right) bank - right column pairing");
        Check(NES_PPU_Memory::NameTableN[2][0]->value() != NES_PPU_Memory::NameTableN[3][0]->value(),
              "vertical mirroring: left column (slots 0/2) and right column (slots 1/3) must be different physical banks");

        INES::arrangement = INES::Mirror::horisontal;
        NES_PPU_Memory::RewireNameTableMirroring();
        NES_PPU_Memory::NameTableN[0][0]->value(0x33);
        NES_PPU_Memory::NameTableN[2][0]->value(0x44);
        Check(NES_PPU_Memory::NameTableN[1][0]->value() == 0x33,
              "horisontal mirroring: slot 1 (top-right) must share slot 0's (top-left) bank - top row pairing");
        Check(NES_PPU_Memory::NameTableN[3][0]->value() == 0x44,
              "horisontal mirroring: slot 3 (bottom-right) must share slot 2's (bottom-left) bank - bottom row pairing");
        Check(NES_PPU_Memory::NameTableN[0][0]->value() != NES_PPU_Memory::NameTableN[2][0]->value(),
              "horisontal mirroring: top row (slots 0/1) and bottom row (slots 2/3) must be different physical banks");
    }
}

int main()
{
    // Same setup nestest_check.cpp uses: NES_Console::INIT() constructs
    // NES_Memory (installing the static Memory[] array every mapper writes
    // through) plus the PPU/GamePad/APU register classes.
    NES_Console::INIT();

    TestNROM();
    Test16kNROMMirrors();
    TestUxROM();
    TestCNROM();
    TestChrBankSwitchInvalidatesFreshTileCache();
    TestAxROM();
    TestMMC1();
    TestMmc1BurstSuppressesNmiDelivery();
    TestMMC3();
    TestMMC3Irq();
    TestRomSwitchRewiresMirroring();
    TestNameTableMirroringBankPairing();
    TestINESHeaderMirroringBit();

    if (failures == 0)
    {
        std::cout << "PASS: all mapper checks passed" << std::endl;
        return 0;
    }
    std::cerr << failures << " mapper check(s) FAILED" << std::endl;
    return 1;
}
