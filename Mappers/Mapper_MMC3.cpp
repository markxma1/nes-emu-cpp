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
#include "Mapper_MMC3.h"
#include "INES.h"
#include "Interrupt.h"
#include "NES_PPU_Memory.h"
#include "NES_PPU.h"
#include "NES_Register.h"
#include <cstdlib>
#include <iostream>

namespace NES
{
    void Mapper_MMC3::SerializeState(std::vector<uint8_t>& out) const
    {
        out.push_back(bankSelect);
        for (uint8_t reg : r)
            out.push_back(reg);
        out.push_back(irqLatch);
        out.push_back(irqCounter);
        out.push_back(irqReloadFlag ? 1 : 0);
        out.push_back(irqEnabled ? 1 : 0);
    }

    void Mapper_MMC3::DeserializeState(const uint8_t*& in, const uint8_t* end)
    {
        if (end - in < 12)
            return; // truncated/foreign save data - leave power-on-default state alone
        bankSelect = *in++;
        for (uint8_t& reg : r)
            reg = *in++;
        irqLatch = *in++;
        irqCounter = *in++;
        irqReloadFlag = (*in++) != 0;
        irqEnabled = (*in++) != 0;
        // See Mapper_MMC1::DeserializeState()'s own comment - re-derive the
        // visible PRG/CHR windows from the restored registers so the
        // *next* real register write starts from consistent state.
        ApplyPrgBanks();
        ApplyChrBanks();
    }

    void Mapper_MMC3::OnInstall()
    {
        bankSelect = 0;
        for (uint8_t& reg : r)
            reg = 0;
        irqLatch = 0;
        irqCounter = 0;
        irqReloadFlag = false;
        irqEnabled = false;
        WriteMirroring(0); // sane default until the game writes its own choice to $A000
        ApplyPrgBanks();
        ApplyChrBanks();
    }

    void Mapper_MMC3::WriteRegister(uint16_t address, uint8_t value)
    {
        bool odd = (address & 0x0001) != 0;
        switch (address & 0x6000) // which $2000-sized quarter of $8000-$FFFF
        {
            case 0x0000: // $8000-$9FFF
                if (odd) WriteBankData(value); else WriteBankSelect(value);
                break;
            case 0x2000: // $A000-$BFFF
                if (!odd) WriteMirroring(value);
                // odd ($A001, PRG-RAM protect): accepted, not acted on -
                // this port has no PRG-RAM/battery-save support.
                break;
            case 0x4000: // $C000-$DFFF: IRQ latch/reload
                if (odd) WriteIrqReload(); else WriteIrqLatch(value);
                break;
            case 0x6000: // $E000-$FFFF: IRQ disable/enable
            default:
                if (odd) WriteIrqEnable(); else WriteIrqDisable();
                break;
        }
    }

    // See OnScanline() below for what this whole IRQ counter is. Register
    // semantics here match http://wiki.nesdev.com/w/index.php/MMC3 ("IRQ
    // registers") exactly - OnScanline() clocks it once per real visible
    // scanline (0-239), matching real hardware's PPU-A12-toggle cadence.
    void Mapper_MMC3::WriteIrqLatch(uint8_t value)
    {
        irqLatch = value;
        if (std::getenv("NES_TRACE_MMC3_IRQLATCH"))
            std::cerr << "[irqLatch] set to 0x" << std::hex << static_cast<int>(value) << std::dec << std::endl;
    }

    void Mapper_MMC3::WriteIrqReload()
    {
        // Real hardware reloads the counter at the *next* clock, not
        // immediately - ClockIrqCounter() checks this flag for exactly that.
        irqReloadFlag = true;
    }

    void Mapper_MMC3::WriteIrqDisable()
    {
        irqEnabled = false;
        // "Writing any value to this register ... acknowledges any pending
        // interrupts" - cancels a not-yet-serviced IRQ this mapper raised,
        // same as real hardware's IRQ line dropping.
        Interrupt::IRQ(false);
    }

    void Mapper_MMC3::WriteIrqEnable()
    {
        irqEnabled = true;
        if (std::getenv("NES_TRACE_MMC3_IRQLATCH"))
            std::cerr << "[irqEnable] enabled, latch=0x" << std::hex << static_cast<int>(irqLatch) << std::dec << std::endl;
    }

    void Mapper_MMC3::ClockIrqCounter()
    {
        if (irqCounter == 0 || irqReloadFlag)
        {
            irqCounter = irqLatch;
            irqReloadFlag = false;
        }
        else
        {
            --irqCounter;
        }
        if (irqCounter == 0 && irqEnabled)
        {
            Interrupt::IRQ(true);
            if (std::getenv("NES_TRACE_MMC3_IRQLATCH"))
                std::cerr << "[irqFire] counter hit 0 at scanline=" << NES_PPU::CurrentScanline()
                          << " latch=0x" << std::hex << static_cast<int>(irqLatch) << std::dec << std::endl;
        }
    }

    // FIXED (new design, not a C# port - see Mapper_MMC3.h's own LEARNING
    // NOTE for the full before/after story): this used to be OnFrame(),
    // bursting all 240 of a frame's worth of IRQ clocks in one shot right
    // before the (then whole-frame-snapshot) PPU composited a frame -
    // reliable "does the IRQ fire at all" but lost *where on screen* an
    // IRQ-driven CHR-bank split (e.g. a status bar) should land, since
    // every one of those 240 clocks (and the bank-select register writes a
    // game's own IRQ handler makes in response to them) landed before the
    // single snapshot the whole frame was built from. Now that
    // NES_PPU::AdvanceDots()/OnScanlineStart() call this once per real
    // visible scanline instead, a single ClockIrqCounter() call here
    // reproduces the real per-scanline PPU-A12-toggle cadence directly -
    // a game's IRQ-driven bank switch now takes effect starting at the
    // exact scanline it should.
    void Mapper_MMC3::OnScanline()
    {
        ClockIrqCounter();
    }

    // FIXED (performance, not correctness - found while investigating why
    // Tiny Toon Adventures' CPU throughput measured ~30000 instr/sec, down
    // from a normal ~500000+, during a real, repeating $8000/$8001
    // bank-select/bank-data write loop, turning what should have been a
    // handful of real-NES frames into "still not done after 90 real
    // seconds"): both handlers used to unconditionally repaint *both* the
    // PRG windows (WritePrgWindow, up to 32KB) *and* the CHR windows
    // (WriteChrWindow, up to 8KB) on every single write, regardless of
    // which one (if either) could actually have changed. Per
    // http://wiki.nesdev.com/w/index.php/MMC3:
    //  - WriteBankData() ($8001) only ever updates *one* of the 8 R0-R7
    //    registers (bankSelect's low 3 bits pick which) - R6/R7 feed
    //    ApplyPrgBanks() only, R0-R5 feed ApplyChrBanks() only (see those
    //    functions below) - so the other one is always a no-op repaint of
    //    data that provably didn't change.
    //  - WriteBankSelect() ($8000)'s low 3 bits only pick *which register*
    //    the *next* $8001 write will target - they have no effect on
    //    current banking by themselves. Only its mode bits (bit 6: PRG
    //    mode, bit 7: CHR A12 inversion, bits 6-7 together = 0xC0) actually
    //    change which window layout is in effect, so re-applying is only
    //    ever needed when those two bits actually changed.
    // Skipping the redundant repaints doesn't change any observable
    // behaviour (WritePrgWindow/WriteChrWindow are simple, deterministic
    // copies from `prg`/`chr` - calling them again with identical inputs
    // that haven't changed can only ever reproduce the same bytes), only
    // how much wasted work a tight bank-switching loop does.
    void Mapper_MMC3::WriteBankSelect(uint8_t value)
    {
        uint8_t oldModeBits = static_cast<uint8_t>(bankSelect & 0xC0);
        bankSelect = value;
        if ((value & 0xC0) != oldModeBits)
        {
            ApplyPrgBanks();
            ApplyChrBanks();
        }
    }

    void Mapper_MMC3::WriteBankData(uint8_t value)
    {
        r[bankSelect & 0x07] = value;
        if ((bankSelect & 0x07) >= 6)
            ApplyPrgBanks();
        else
            ApplyChrBanks();
    }

    void Mapper_MMC3::WriteMirroring(uint8_t value)
    {
        // FIXED (real bug, found by cross-checking Tiny Toon Adventures'
        // actual live nametable content against FCEUX 2.6.6 - same ROM,
        // same CRC32 0x99dddb04, byte-identical input sequence fed to both
        // emulators via a frame-numbered playback file): this game writes
        // $A000=$01 (a single, never-changing value - confirmed via a
        // memory.registerwrite hook in FCEUX) early on and never touches it
        // again. FCEUX's own nametable RAM (read live via its Lua ppu.readbyte)
        // shows $2000 and $2400 holding byte-identical content after that
        // write - i.e. real hardware's actual behavior for bit0=1 here pairs
        // $2000/$2400 (and separately $2800/$2C00), exactly the pairing
        // NES_PPU_Memory::RewireNameTableMirroring() implements for
        // INES::Mirror::horisontal, not ::vertical. The previous mapping
        // (bit0=1 -> ::vertical) had bit0=1 producing a $2000/$2800 pairing
        // instead - directly why Tiny Toon Adventures' own status-bar HUD
        // graphics (which the game streams into the *other* pair via a
        // separate nametable-streaming pass) never appeared: this port put
        // that streamed data one physical bank away from where the
        // background renderer was actually sampling for the visible screen.
        // The MMC3 register doc's own parenthetical notation (nesdev.org/
        // wiki/MMC3, "0: horizontal (A10); 1: vertical (A11)") reads as
        // agreeing with the old mapping at a glance, but the general
        // mirroring wiring reference (nesdev.org/wiki/Mirroring) defines
        // *horizontal* mirroring as "connect PPU A11 to CIRAM A10" (pairs
        // $2000/$2400, $2800/$2C00) and *vertical* as "connect PPU A10 to
        // CIRAM A10" (pairs $2000/$2800, $2400/$2C00) - i.e. the opposite
        // parenthetical pairing from what a literal same-letter reading of
        // the MMC3 page's own "(A10)"/"(A11)" hints suggests. Given the
        // prose across nesdev's own pages reads as genuinely
        // self-contradictory here (a well-known trap - the wiki's own
        // Mirroring article calls the horizontal/vertical naming
        // "counter-intuitive"), this is fixed against the empirical,
        // unambiguous ground truth (a trusted reference emulator's actual
        // live VRAM content for this exact ROM) rather than re-litigating
        // the prose - swapping which enum bit0's two values map to, with
        // RewireNameTableMirroring()'s own bank-pairing table (and its own
        // FIXED note) left exactly as is.
        INES::arrangement = (value & 0x01) ? INES::Mirror::horisontal : INES::Mirror::vertical;
        NES_PPU_Memory::RewireNameTableMirroring();
    }

    void Mapper_MMC3::ApplyPrgBanks()
    {
        size_t numBanks8k = prg.size() / 8192;
        if (numBanks8k == 0)
            return;
        size_t r6 = static_cast<size_t>(r[6] & 0x3F) % numBanks8k;
        size_t r7 = static_cast<size_t>(r[7] & 0x3F) % numBanks8k;
        size_t secondLast = numBanks8k >= 2 ? numBanks8k - 2 : 0;
        size_t last = numBanks8k - 1;

        // http://wiki.nesdev.com/w/index.php/MMC3 "PRG ROM bank mode" (bit 6 of $8000).
        if (!(bankSelect & 0x40))
        {
            WritePrgWindow(0x8000, r6 * 8192, 8192);
            WritePrgWindow(0xA000, r7 * 8192, 8192);
            WritePrgWindow(0xC000, secondLast * 8192, 8192);
            WritePrgWindow(0xE000, last * 8192, 8192);
        }
        else
        {
            WritePrgWindow(0x8000, secondLast * 8192, 8192);
            WritePrgWindow(0xA000, r7 * 8192, 8192);
            WritePrgWindow(0xC000, r6 * 8192, 8192);
            WritePrgWindow(0xE000, last * 8192, 8192);
        }
    }

    void Mapper_MMC3::ApplyChrBanks()
    {
        if (HasChrRam())
            return;
        size_t numBanks1k = chr.size() / 1024;
        if (numBanks1k == 0)
            return;
        if (std::getenv("NES_TRACE_MMC3_CHR"))
            std::cerr << "[mmc3-chr] bankSelect=0x" << std::hex << static_cast<int>(bankSelect) << std::dec
                      << " r0=" << static_cast<int>(r[0]) << " r1=" << static_cast<int>(r[1])
                      << " r2=" << static_cast<int>(r[2]) << " r3=" << static_cast<int>(r[3])
                      << " r4=" << static_cast<int>(r[4]) << " r5=" << static_cast<int>(r[5])
                      << " numBanks1k=" << numBanks1k << " chr.size()=" << chr.size() << std::endl;

        // R0/R1 select 2KB banks - low bit ignored per nesdev (2KB banks
        // must be even-numbered in 1KB units).
        size_t r0 = (static_cast<size_t>(r[0] & 0xFE) % numBanks1k) * 1024;
        size_t r1 = (static_cast<size_t>(r[1] & 0xFE) % numBanks1k) * 1024;
        size_t r2 = (static_cast<size_t>(r[2]) % numBanks1k) * 1024;
        size_t r3 = (static_cast<size_t>(r[3]) % numBanks1k) * 1024;
        size_t r4 = (static_cast<size_t>(r[4]) % numBanks1k) * 1024;
        size_t r5 = (static_cast<size_t>(r[5]) % numBanks1k) * 1024;

        // http://wiki.nesdev.com/w/index.php/MMC3 "CHR A12 inversion" (bit 7 of $8000).
        if (!(bankSelect & 0x80))
        {
            WriteChrWindow(0x0000, r0, 2048);
            WriteChrWindow(0x0800, r1, 2048);
            WriteChrWindow(0x1000, r2, 1024);
            WriteChrWindow(0x1400, r3, 1024);
            WriteChrWindow(0x1800, r4, 1024);
            WriteChrWindow(0x1C00, r5, 1024);
        }
        else
        {
            WriteChrWindow(0x1000, r0, 2048);
            WriteChrWindow(0x1800, r1, 2048);
            WriteChrWindow(0x0000, r2, 1024);
            WriteChrWindow(0x0400, r3, 1024);
            WriteChrWindow(0x0800, r4, 1024);
            WriteChrWindow(0x0C00, r5, 1024);
        }
    }
}
