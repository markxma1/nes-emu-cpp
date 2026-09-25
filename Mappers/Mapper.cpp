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
#include "Mapper.h"
#include "NES_Memory.h"
#include "NES_PPU_Memory.h"
#include "NES_PPU.h"
#include <memory>

namespace NES
{
    void Mapper::Install(std::vector<uint8_t> prgRom, std::vector<uint8_t> chrRom)
    {
        prg = std::move(prgRom);
        chr = std::move(chrRom);
        UnmapAllRom();
        InvalidateBankCache();
        OnInstall();

        // Every address in $8000-$FFFF, not just specific ones - see the
        // comment on this function in Mapper.h for why. Safe to call again
        // for a different ROM/mapper later (e.g. switching ROMs via the
        // in-emulator picker - see main.cpp's switchRom()): AddressSetup::
        // AfterSet() simply replaces whatever hook was there before, so the
        // previous mapper's (about-to-be-destroyed) hook is gone before it
        // could ever dangle - as long as the CPU thread is stopped for the
        // duration, which switchRom() already guarantees.
        //
        // FIXED (found while investigating why every real commercial ROM
        // that uses PRG bank-switching - Contra/UxROM, Chip 'n Dale/MMC1,
        // Batman III/MMC3, The Lion King/AxROM, all confirmed via
        // NES_TRACE_PC never getting stuck on an illegal opcode, i.e. never
        // crashing, just silently never progressing past their own boot
        // code - stayed on a permanently black screen, while NROM/Galaga
        // and CNROM/Monster Truck Rally, neither of which switch PRG banks,
        // both rendered fine): per
        // http://wiki.nesdev.com/w/index.php/Cartridge_connector, PRG-ROM is
        // read-only hardware - "the CPU can never write to this" - a CPU
        // write anywhere in $8000-$FFFF only ever reaches the mapper's
        // register latch; it can never change what byte the ROM chip itself
        // holds. AddressSetup::Value(v) (the hooked setter every STA/write
        // instruction goes through - see AddressSetup.cpp) has no concept
        // of "read-only": it unconditionally does `valueCore = v` *before*
        // calling this AfterSet hook. So every single mapper-register write
        // - even one that WriteRegister() below doesn't (yet) act on, e.g.
        // 4 of MMC1's 5 serial shift-register writes, or *any* UxROM/MMC3
        // register write whose CPU address happens to fall inside a PRG
        // window this particular commit doesn't repaint (most commonly
        // UxROM/MMC3's fixed $C000-$FFFF bank, which is only ever painted
        // once, in OnInstall()) - silently overwrote one real PRG-ROM byte
        // at that exact CPU address with the raw value the CPU wrote (a
        // small bank-select number, not real code/data), permanently
        // corrupting whatever ROM byte used to live there. Confirmed via a
        // temporary trace: Contra hits exactly this, corrupting $FFD0/$FFD1
        // - inside its fixed bank - within the first 200 frames. A single
        // corrupted byte rarely crashes outright (it still usually decodes
        // as *some* valid opcode/operand), which is exactly why this showed
        // up as "runs seemingly fine, never any exception, just never
        // finishes booting" rather than a clean crash.
        //
        // Fixed by snapshotting each address's real byte immediately before
        // every write (BeforSet fires before AddressSetup::Value() mutates
        // valueCore) and restoring it after WriteRegister() returns, unless
        // WritePrgWindow() itself repainted that exact address as part of
        // handling this write (tracked via prgWindowsTouchedThisWrite/
        // PrgWindowCoveredAddress() below) - in which case the freshly
        // mapped-in real ROM byte is correct and must be left alone.
        for (int address = 0x8000; address <= 0xFFFF; ++address)
        {
            auto cell = NES_Memory::Memory[static_cast<size_t>(address)];
            auto preWriteByte = std::make_shared<uint8_t>(cell->value());

            cell->BeforSet([cell, preWriteByte]() { *preWriteByte = cell->value(); });

            cell->AfterSet(
                [this, address, cell, preWriteByte](uint8_t value)
                {
                    prgWindowsTouchedThisWrite.clear();
                    WriteRegister(static_cast<uint16_t>(address), value);
                    if (!PrgWindowCoveredAddress(address))
                        cell->value(*preWriteByte);
                });
        }
    }

    // Memory cells keep pointers into this mapper's ROM bytes and slot table. At program exit the mapper
    // can be destroyed after the memory cells (static destruction order), so the destructor must not touch
    // them - and a mapper replaced or destroyed mid-run must not leave dangling pointers behind. So the
    // ROM bytes and the slot table are handed to a store that is never freed (ROM images are small and
    // replaced rarely); Install() of the next mapper unmaps the cells again.
    Mapper::~Mapper()
    {
        struct Retired
        {
            std::vector<uint8_t> prg, chr;
            std::shared_ptr<void> slots;
        };
        static auto* retired = new std::vector<Retired>();
        retired->push_back(Retired{std::move(prg), std::move(chr), slotTable});
    }

    // Cells that read through this mapper's slot arrays must not outlive it.
    void Mapper::UnmapAllRom()
    {
        for (int k = 0; k < 64; ++k)
            if (prgMapped[k])
                for (int i = 0; i < 1024; ++i)
                    NES_Memory::Memory[static_cast<size_t>(k * 1024 + i)]->UnmapRom();
        for (int k = 0; k < 8; ++k)
            if (chrMapped[k])
                for (int i = 0; i < 1024; ++i)
                    NES_PPU_Memory::PatternTable[static_cast<size_t>(k * 1024 + i)]->UnmapRom();
        for (bool& m : prgMapped) m = false;
        for (bool& m : chrMapped) m = false;
    }

    void Mapper::WritePrgWindow(int cpuStart, size_t romByteOffset, int length)
    {
        // Unchanged window: nothing to copy. It is not recorded as "touched" either, so the cell the
        // CPU just wrote to (a bank register) gets its ROM byte back in the write hook below.
        size_t normalizedOffset = romByteOffset % prg.size();
        auto cached = prgWindowCache.find(cpuStart);
        if (cached != prgWindowCache.end() && cached->second.offset == normalizedOffset && cached->second.length == length)
            return;
        prgWindowCache[cpuStart] = WindowCache{normalizedOffset, length};
        prgWindowsTouchedThisWrite.emplace_back(cpuStart, cpuStart + length);
        if (prg.size() % 1024 == 0 && cpuStart % 1024 == 0 && length % 1024 == 0 && cpuStart >= 0 && cpuStart + length <= 0x10000)
        {
            // Point each 1 KB piece of the window at its ROM bytes (no copying).
            for (int piece = 0; piece < length / 1024; ++piece)
            {
                int k = cpuStart / 1024 + piece;
                slotTable->prg[k] = prg.data() + (normalizedOffset + static_cast<size_t>(piece) * 1024) % prg.size();
                if (!prgMapped[k])
                {
                    prgMapped[k] = true;
                    for (int i = 0; i < 1024; ++i)
                        NES_Memory::Memory[static_cast<size_t>(k * 1024 + i)]->MapRom(&slotTable->prg[k], static_cast<uint16_t>(i));
                }
            }
            return;
        }
        for (int i = 0; i < length; ++i)
        {
            uint8_t byte = prg[(romByteOffset + static_cast<size_t>(i)) % prg.size()];
            NES_Memory::Memory[static_cast<size_t>(cpuStart + i)]->value(byte);
        }
    }

    bool Mapper::PrgWindowCoveredAddress(int address) const
    {
        for (const auto& range : prgWindowsTouchedThisWrite)
            if (address >= range.first && address < range.second)
                return true;
        return false;
    }

    // FIXED (real bug, found live: Tiny Toon Adventures - an MMC3 game -
    // showing one sprite (the player) rendering perfectly while another
    // sprite on the same screen was a fragmented mess of unrelated pixel
    // blocks, confirmed via a user-captured save state and reproduced
    // twice): NES_PPU::DecodeBackgroundTileFresh()/DecodeSpriteTileFresh()
    // cache their decoded-tile results (freshBackgroundTileCache/
    // freshSpriteTileCache - see NES_PPU.Tile.cpp's own comment on why a
    // *within-frame* cache exists at all: pure performance, re-decoding
    // every tile from raw CHR bits on every single scanline it appears on
    // was a measured, severe regression). That cache is keyed by (tile
    // index, palette, bank) and is only ever cleared once per whole frame
    // (NES_PPU::OnScanlineStart()'s scanline-0 branch) - it has no way to
    // notice that this exact function just overwrote the *underlying* CHR
    // data a (tile index, bank) pair points to. MMC3 games commonly rewire
    // CHR banks *mid-frame*, multiple times, via the same scanline-IRQ
    // mechanism this port's whole scanline-accurate PPU redesign exists to
    // support (see NES_PPU::AdvanceDots()'s own comment) - Tiny Toon
    // Adventures' status-bar split does exactly this. A sprite decoded
    // *before* such a mid-frame bank switch, then referenced again (same
    // tile index/palette/bank) *after* it, would incorrectly reuse the
    // stale pre-switch pixels - fragments of whatever was at that (index,
    // bank) slot earlier in the frame, stitched onto a sprite that now
    // logically points at completely different CHR data. Every mapper that
    // supports CHR banking funnels through this one function (MMC1, MMC3,
    // CNROM, UxROM, NROM - see their own WriteChrWindow() call sites), so
    // this is the single correct choke point to invalidate both caches
    // from, catching the bug for all of them rather than patching MMC3
    // alone.
    void Mapper::WriteChrWindow(int ppuStart, size_t romByteOffset, int length) const
    {
        if (HasChrRam())
            return;
        size_t normalizedOffset = romByteOffset % chr.size();
        auto cached = chrWindowCache.find(ppuStart);
        if (cached != chrWindowCache.end() && cached->second.offset == normalizedOffset && cached->second.length == length)
            return;
        chrWindowCache[ppuStart] = WindowCache{normalizedOffset, length};
        if (chr.size() % 1024 == 0 && ppuStart % 1024 == 0 && length % 1024 == 0 && ppuStart >= 0 && ppuStart + length <= 0x2000)
        {
            for (int piece = 0; piece < length / 1024; ++piece)
            {
                int k = ppuStart / 1024 + piece;
                slotTable->chr[k] = chr.data() + (normalizedOffset + static_cast<size_t>(piece) * 1024) % chr.size();
                if (!chrMapped[k])
                {
                    chrMapped[k] = true;
                    for (int i = 0; i < 1024; ++i)
                        NES_PPU_Memory::PatternTable[static_cast<size_t>(k * 1024 + i)]->MapRom(&slotTable->chr[k], static_cast<uint16_t>(i));
                }
            }
            NES_PPU::ClearFreshTileCaches();
            NES_PPU::NoteChrChanged();
            return;
        }
        for (int i = 0; i < length; ++i)
        {
            uint8_t byte = chr[(romByteOffset + static_cast<size_t>(i)) % chr.size()];
            NES_PPU_Memory::PatternTable[static_cast<size_t>(ppuStart + i)]->value(byte);
        }
        NES_PPU::ClearFreshTileCaches();
        NES_PPU::NoteChrChanged();
    }
}
