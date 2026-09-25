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
/// @brief CPU/interrupt/PPU-register regression harness, not a port of
/// anything - new test-only code (same spirit as tests/nestest/nestest_check.cpp
/// and tests/mappers/mapper_check.cpp) covering real bugs found live during
/// this session's investigation that neither of those two harnesses is the
/// right place for (nestest_check only replays a fixed reference log;
/// mapper_check is scoped to Mapper_*.cpp correctness) - direct,
/// synthetic-program tests of NES_CPU/Interrupt/NES_PPU_Register behavior
/// instead.
#include "INES.h"
#include "Interrupt.h"
#include "NES_CPU.h"
#include "NES_Console.h"
#include "NES_GamePad.h"
#include "NES_Memory.h"
#include "NES_PPU.h"
#include "NES_PPU_Memory.h"
#include "Mapper_MMC3.h"
#include "NES_PPU_AttributeTable.h"
#include "NES_PPU_OAM.h"
#include "NES_PPU_Palette.h"
#include "NES_PPU_Register.h"
#include "NES_Register.h"
#include "NES_SaveState.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

    /// The NMI line is edge-triggered and, unlike IRQ, cannot be masked by the
    /// I flag - http://wiki.nesdev.com/w/index.php/CPU_interrupts. A second
    /// vblank NMI arriving while the first handler is still running therefore
    /// interrupts that handler right away; nothing holds it back until RTI.
    /// (An earlier version blocked it, which delayed every later interrupt.)
    void TestNmiIsNotBlockedByRunningHandler()
    {
        constexpr uint16_t kHandlerStart = 0x9000;
        constexpr uint16_t kHandlerRti = 0x9040;
        for (uint16_t addr = 0x8000; addr < 0x8010; ++addr)
            NES_Memory::Memory[addr]->value(0xEA); // NOP
        for (uint16_t addr = kHandlerStart; addr < kHandlerRti; ++addr)
            NES_Memory::Memory[addr]->value(0xEA); // NOP
        NES_Memory::Memory[kHandlerRti]->value(0x40); // RTI

        NES_Memory::Memory[0xFFFA]->value(static_cast<uint8_t>(kHandlerStart));
        NES_Memory::Memory[0xFFFB]->value(static_cast<uint8_t>(kHandlerStart >> 8));

        NES_Register::PC = 0x8000;
        NES_Register::S = 0xFD;
        NES_Register::P.P = 0x24;
        Interrupt::NMI(false);
        for (int i = 0; i < 3; ++i)
            NES_CPU::Step();

        Interrupt::NMI(true);
        bool entered = false;
        for (int i = 0; i < 20 && !entered; ++i)
        {
            NES_CPU::Step();
            entered = NES_Register::PC == kHandlerStart;
        }
        Check(entered, "NMI: a requested NMI should have entered the handler ($9000)");

        // Let the handler run a few NOPs, then raise a second NMI.
        for (int i = 0; i < 5; ++i)
            NES_CPU::Step();
        Interrupt::NMI(true);
        bool reentered = false;
        for (int i = 0; i < 20 && !reentered; ++i)
        {
            NES_CPU::Step();
            reentered = NES_Register::PC == kHandlerStart;
        }
        Check(reentered,
              "NMI: a second NMI raised while the handler is still running must interrupt it "
              "immediately (NMI is not blocked by the running handler or by the I flag)");
        Interrupt::NMI(false);
    }

    /// Writing $2000 with the NMI-enable bit going 0 -> 1 while the vblank flag
    /// is set raises an NMI at once (games enable NMI late in vblank).
    void TestEnablingNmiInVblankRaisesNmi()
    {
        NES_Memory::Memory[0xFFFA]->value(0x00);
        NES_Memory::Memory[0xFFFB]->value(0x90);
        NES_Register::PC = 0x8000;
        NES_Register::S = 0xFD;
        NES_Register::P.P = 0x24;
        Interrupt::NMI(false);
        NES_PPU_Register::PPUSTATUS.V(true);
        NES_Memory::Memory[0x2000]->Value(0x00);
        NES_Memory::Memory[0x2000]->Value(0x80); // 0 -> 1 inside vblank
        Interrupt::Check(4); // end of the write instruction: too early to be seen
        Check(NES_Register::PC == 0x8000, "NMI: an NMI enabled by a $2000 write is not taken right after that write");
        Interrupt::Check(2); // end of the next instruction
        Check(NES_Register::PC == 0x9000, "NMI: it is taken after the following instruction");
        Interrupt::NMI(false);

        NES_Register::PC = 0x8000;
        NES_Register::S = 0xFD;
        NES_Memory::Memory[0x2000]->Value(0x80); // already enabled: no new edge
        Interrupt::Check(2);
        Interrupt::Check(2);
        Check(NES_Register::PC == 0x8000, "NMI: rewriting $2000 with NMI already enabled raises nothing");
        NES_Memory::Memory[0x2000]->Value(0x00);
        NES_PPU_Register::PPUSTATUS.V(false);
        NES_Memory::Memory[0x2000]->Value(0x80);
        Interrupt::Check(2);
        Interrupt::Check(2);
        Check(NES_Register::PC == 0x8000, "NMI: enabling NMI outside vblank raises nothing");
        NES_Memory::Memory[0x2000]->Value(0x00);
    }

    /// The per-tile shortcut must give exactly the palette the full attribute table lists for every tile
    /// (the renderer uses the shortcut, the viewers still use the full table).
    void TestAttributePaletteForTileMatchesFullTable()
    {
        for (int nt = 0; nt < 4; ++nt)
        {
            uint32_t seed = 12345u + static_cast<uint32_t>(nt);
            for (auto& cell : NES_PPU_Memory::AttributeTableN[static_cast<size_t>(nt)])
            {
                seed = seed * 1664525u + 1013904223u;
                cell->value(static_cast<uint8_t>(seed >> 24));
            }
            std::vector<int> full = NES_PPU_AttributeTable::AttributeTable(nt);
            bool same = true;
            for (int row = 0; row < 30 && same; ++row)
                for (int col = 0; col < 32; ++col)
                    if (NES_PPU_AttributeTable::PaletteForTile(nt, row, col) != full[static_cast<size_t>(row * 32 + col)])
                    {
                        same = false;
                        break;
                    }
            Check(same, "PaletteForTile must equal AttributeTable()[row*32+col] for every tile (nametable " + std::to_string(nt) + ")");
            for (auto& cell : NES_PPU_Memory::AttributeTableN[static_cast<size_t>(nt)])
                cell->value(0);
        }
    }

    /// A bank write that maps the same ROM bytes again must not copy them again, but a different bank must.
    void TestMapperSkipsUnchangedBankWindows()
    {
        // Never destroyed: Install() leaves hooks in the memory cells that point back at the mapper.
        static Mapper_MMC3& mmc3 = *new Mapper_MMC3();
        std::vector<uint8_t> prg(8 * 8192), chr(16 * 1024);
        for (size_t i = 0; i < prg.size(); ++i)
            prg[i] = static_cast<uint8_t>(i / 8192 + 1); // every 8 KB bank filled with its own number
        mmc3.Install(prg, chr);
        NES_Memory::Memory[0x8000]->Value(6); // bank select R6 ($8000-$9FFF)
        NES_Memory::Memory[0x8001]->Value(3); // R6 = bank 3
        Check(NES_Memory::Memory[0x9000]->value() == 4, "MMC3: $8000 window shows PRG bank 3 after the bank write");
        NES_Memory::Memory[0x8001]->Value(3); // same bank again
        Check(NES_Memory::Memory[0x9000]->value() == 4, "MMC3: writing the same bank again keeps the mapped bytes");
        Check(NES_Memory::Memory[0x8001]->value() == 4, "MMC3: the register write itself must not leave the written value in the ROM cell");
        NES_Memory::Memory[0x8001]->Value(5);
        Check(NES_Memory::Memory[0x9000]->value() == 6, "MMC3: a different bank is copied");
    }

    /// With picture rendering off (NES_PPU::SetRenderPixels(false)) the sprite-0-hit flag must still be
    /// raised on the right scanline, because games poll it (status bars, split screens).
    void TestSprite0HitWorksWithoutPictureRendering()
    {
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUCTRL.S(false);
        NES_PPU_Register::PPUCTRL.H(false);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(true);
        NES_PPU_Register::PPUMASK.m(true);
        NES_PPU_Register::PPUMASK.M(true);
        constexpr uint16_t kTile = 215;
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + i]->Value(0xFF);
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + 8 + i]->Value(0x00);
        }
        for (size_t k = 0; k < 960; ++k)
            NES_PPU_Memory::NameTableN[0][k]->Value(kTile);
        for (auto& cell : NES_PPU_Memory::AttributeTableN[0])
            cell->Value(0);
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES_PPU_Memory::SpritePalette[1]->Value(0x21);
        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;
        for (int i = 0; i < 256; ++i)
            NES_Memory::Memory[0x0200 + static_cast<size_t>(i)]->value(0xFF);
        NES_Memory::Memory[0x0200]->value(49);      // sprite 0: top row on scanline 50
        NES_Memory::Memory[0x0201]->value(kTile);
        NES_Memory::Memory[0x0202]->value(0);
        NES_Memory::Memory[0x0203]->value(20);
        NES_PPU_OAM::OAMDMA(0x02);
        NES_CPU::pendingExtraCycles = 0;

        NES::NES_PPU::SetRenderPixels(false);
        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        NES_PPU_Register::PPUSTATUS.S(false);
        while (NES::NES_PPU::CurrentScanline() != 40)
            NES::NES_PPU::AdvanceDots(1);
        Check(!NES_PPU_Register::PPUSTATUS.S(), "sprite 0 hit: not raised before sprite 0's first scanline (no picture rendering)");
        while (NES::NES_PPU::CurrentScanline() != 60)
            NES::NES_PPU::AdvanceDots(1);
        Check(NES_PPU_Register::PPUSTATUS.S(), "sprite 0 hit: still raised without picture rendering");
        NES::NES_PPU::SetRenderPixels(true);
        NES_PPU_Register::PPUSTATUS.S(false);
        NES_PPU_Memory::BGPalette[1]->Value(0x0F);
        NES_PPU_Memory::SpritePalette[1]->Value(0x0F);
    }

    /// A 6502 interrupt pushes the status register as it was *before* the
    /// interrupt and only then sets the I flag, so RTI brings the old I flag
    /// back. Setting I first left it set after every NMI, which made the
    /// MMC3 scanline IRQ wait for the next vblank (Tiny Toon Adventures'
    /// status bar never appeared).
    void TestNmiRestoresInterruptFlagOnRti()
    {
        constexpr uint16_t kHandlerStart = 0x9000;
        NES_Memory::Memory[0x8000]->value(0xEA); // NOP
        for (uint16_t addr = 0x8001; addr < 0x8010; ++addr)
            NES_Memory::Memory[addr]->value(0xEA);
        NES_Memory::Memory[kHandlerStart]->value(0xEA);
        NES_Memory::Memory[kHandlerStart + 1]->value(0x40); // RTI
        NES_Memory::Memory[0xFFFA]->value(static_cast<uint8_t>(kHandlerStart));
        NES_Memory::Memory[0xFFFB]->value(static_cast<uint8_t>(kHandlerStart >> 8));

        NES_Register::PC = 0x8000;
        NES_Register::S = 0xFD;
        NES_Register::P.P = 0x20; // I flag clear
        Interrupt::NMI(false);
        Interrupt::IRQ(false);
        NES_CPU::Step();
        Interrupt::NMI(true);
        bool entered = false;
        for (int i = 0; i < 20 && !entered; ++i)
        {
            NES_CPU::Step();
            entered = NES_Register::PC == kHandlerStart;
        }
        Check(entered, "NMI I-flag: NMI should have entered the handler");
        Check(NES_Register::P.Interrupt(), "NMI I-flag: the I flag is set while the handler runs");
        for (int i = 0; i < 4 && NES_Register::PC != 0x8000 && NES_Register::PC != 0x8001; ++i)
            NES_CPU::Step();
        Check(!NES_Register::P.Interrupt(),
              "NMI I-flag: after RTI the I flag must be back to its pre-interrupt value (clear)");
    }

    /// $3F10/$3F14/$3F18/$3F1C are mirrors of $3F00/$3F04/$3F08/$3F0C
    /// (http://wiki.nesdev.com/w/index.php/PPU_palettes): a game writing its
    /// backdrop colour through $3F10 sets the universal backdrop. Tiny Toon
    /// Adventures does, and its sky stayed black while the mirror was missing.
    void TestSpritePaletteMirrorsBackdrop()
    {
        NES_PPU_Memory::Memory[0x3F10]->Value(0x31);
        Check(NES_PPU_Memory::BGPalette[0]->Value() == 0x31,
              "PPU palette: writing $3F10 must change the universal backdrop ($3F00)");
        NES_PPU_Memory::Memory[0x3F00]->Value(0x0F);
        Check(NES_PPU_Memory::SpritePalette[0]->Value() == 0x0F,
              "PPU palette: writing $3F00 must be visible at $3F10");
        NES_PPU_Memory::Memory[0x3F1C]->Value(0x22);
        Check(NES_PPU_Memory::BGPalette[0xC]->Value() == 0x22,
              "PPU palette: $3F1C must mirror $3F0C");
        NES_PPU_Memory::Memory[0x3F11]->Value(0x05);
        Check(NES_PPU_Memory::BGPalette[1]->Value() != 0x05,
              "PPU palette: $3F11 is not a mirror of $3F01 and must keep its own value");
        NES_PPU_Memory::BGPalette[0]->Value(0x0F);
    }

    /// Background colour-0 pixels are transparent and show the universal
    /// backdrop colour, not black.
    void TestBackdropColourShowsThroughTransparentBackground()
    {
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(false);
        constexpr uint16_t kBlankTile = 200;
        for (int i = 0; i < 16; ++i)
            NES_PPU_Memory::PatternTableN[0][kBlankTile * 16 + i]->Value(0);
        for (int slot = 0; slot < 4; ++slot)
            for (auto& cell : NES_PPU_Memory::NameTableN[slot])
                cell->Value(kBlankTile);
        NES_PPU_Memory::BGPalette[0]->Value(0x21);
        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;
        NES::NES_PPU::Picture frame = NES::NES_PPU::RenderStaticSnapshot();
        Check(frame.GetPixel(5, 5) == NES_PPU_Palette::UniversalBackgroundColor(),
              "Display: a blank background must show the universal backdrop colour");
        Check(!(frame.GetPixel(5, 5) == NES::NES_PPU::Color::Black()),
              "Display: the backdrop colour must not be replaced by black");
        NES_PPU_Memory::BGPalette[0]->Value(0x0F);
    }

    /// MMC3 games switch CHR banks mid-frame (status bar). The nametable viewer
    /// must decode each tile row with the CHR state that row was drawn with, not
    /// with whatever banks are active when the viewer refreshes.
    void TestNameTableViewerUsesChrStateOfDrawnRow()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(false);
        NES_PPU_Register::PPUMASK.m(true);
        constexpr uint16_t kTile = 213;
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + i]->Value(0xFF); // colour 1
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + 8 + i]->Value(0x00);
        }
        for (size_t k = 0; k < 960; ++k)
            NES_PPU_Memory::NameTableN[0][k]->Value(kTile);
        for (auto& cell : NES_PPU_Memory::AttributeTableN[0])
            cell->Value(0);
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES_PPU_Memory::BGPalette[2]->Value(0x21);
        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;
        NES::NES_PPU::NoteChrChanged();

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        while (NES::NES_PPU::CurrentScanline() != 5)
            NES::NES_PPU::AdvanceDots(1);
        NES::NES_PPU::Color drawn = NES::NES_PPU::DecodeBackgroundTileFresh(kTile, 0).GetPixel(0, 0);
        while (NES::NES_PPU::CurrentScanline() != 30)
            NES::NES_PPU::AdvanceDots(1);

        // Later in the frame the banks change: the tile now decodes as colour 2.
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + i]->Value(0x00);
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + 8 + i]->Value(0xFF);
        }
        NES::NES_PPU::ClearFreshTileCaches();
        NES::NES_PPU::NoteChrChanged();
        NES::NES_PPU::Color current = NES::NES_PPU::DecodeBackgroundTileFresh(kTile, 0).GetPixel(0, 0);
        Check(!(drawn == current), "CHR viewer test setup: old and new bank must decode differently");
        while (NES::NES_PPU::CurrentScanline() != 33)
            NES::NES_PPU::AdvanceDots(1); // CHR state #1 is taken at the next scanline

        Check(NES::NES_PPU::NameTabele(true).GetPixel(2, 2) == drawn,
              "nametable viewer: a row drawn before a CHR switch must be shown with the old banks");
        NES::NES_PPU::SetChrView(1);
        Check(NES::NES_PPU::NameTabele(true).GetPixel(2, 2) == current,
              "nametable viewer: forcing CHR state #1 shows every row with that state");
        NES::NES_PPU::SetChrView(-1);

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        NES_PPU_Memory::BGPalette[1]->Value(0x0F);
        NES_PPU_Memory::BGPalette[2]->Value(0x0F);
    }

    /// The OAM viewer and the pattern-table window can show the CHR state that
    /// was in effect during the frame, not just the live banks.
    void TestSpriteAndPatternViewersUseFrameChrState()
    {
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUCTRL.V(false);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(false);
        NES_PPU_Register::PPUMASK.m(true);
        constexpr uint16_t kTile = 214;
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + i]->Value(0xFF); // colour 1
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + 8 + i]->Value(0x00);
        }
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES_PPU_Memory::BGPalette[2]->Value(0x21);
        NES_PPU_Memory::SpritePalette[1]->Value(0x16);
        NES_PPU_Memory::SpritePalette[2]->Value(0x21);
        NES::NES_PPU::NoteChrChanged();

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        while (NES::NES_PPU::CurrentScanline() != 10)
            NES::NES_PPU::AdvanceDots(1);
        NES::NES_PPU::Color drawn = NES::NES_PPU::DecodeSpriteTileFresh(kTile, 0, 0).GetPixel(0, 0);
        while (NES::NES_PPU::CurrentScanline() != 30)
            NES::NES_PPU::AdvanceDots(1);
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + i]->Value(0x00);
            NES_PPU_Memory::PatternTableN[0][kTile * 16 + 8 + i]->Value(0xFF);
        }
        NES::NES_PPU::ClearFreshTileCaches();
        NES::NES_PPU::NoteChrChanged();
        NES::NES_PPU::Color current = NES::NES_PPU::DecodeSpriteTileFresh(kTile, 0, 0).GetPixel(0, 0);
        Check(!(drawn == current), "viewer test setup: old and new bank must decode differently");
        while (NES::NES_PPU::CurrentScanline() != 33)
            NES::NES_PPU::AdvanceDots(1); // CHR state #1 is taken at the next scanline

        Check(NES::NES_PPU::DecodeSpriteForViewer(kTile, 0, 0, 20).GetPixel(0, 0) == drawn,
              "OAM viewer: a sprite is decoded with the CHR banks active at its scanline");
        // Wiring: the whole overlay must use the same per-scanline CHR state.
        for (int i = 0; i < 256; ++i)
            NES_Memory::Memory[0x0200 + static_cast<size_t>(i)]->value(0xFF);
        NES_Memory::Memory[0x0200]->value(19);   // Y (drawn at 20)
        NES_Memory::Memory[0x0201]->value(kTile);
        NES_Memory::Memory[0x0202]->value(0);    // palette 0
        NES_Memory::Memory[0x0203]->value(30);   // X
        NES_PPU_OAM::OAMDMA(0x02);
        NES_CPU::pendingExtraCycles = 0;
        NES_PPU_Register::PPUCTRL.H(false);
        NES_PPU_Register::PPUCTRL.S(false);
        Check(NES::NES_PPU::OAMDebugOverlay().GetPixel(30, 20) == drawn,
              "OAM viewer overlay: sprite drawn with the CHR state of its scanline");
        NES::NES_PPU::SetChrView(1);
        Check(NES::NES_PPU::OAMDebugOverlay().GetPixel(30, 20) == current,
              "OAM viewer overlay: forcing CHR state #1 draws sprites with that state");
        Check(NES::NES_PPU::DecodeSpriteForViewer(kTile, 0, 0, 20).GetPixel(0, 0) == current,
              "OAM viewer: forcing CHR state #1 uses that state");
        NES::NES_PPU::SetChrView(-1);

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        NES::NES_PPU::Display(); // what RenderFrame() does at frame end: publishes the CHR states
        NES::NES_PPU::CycleChrView();
        Check(NES::NES_PPU::ChrView() == 0, "K: first press selects CHR state #0 in all viewers");
        Check(NES::NES_PPU::PatternTable(0).GetPixel(kTile % 16 * 8, kTile / 16 * 8) == NES_PPU_Palette::getPalette(0).color[1],
              "pattern window: CHR state #0 shows the tile as it was during the frame (colour 1)");
        NES::NES_PPU::CycleChrView();
        Check(NES::NES_PPU::ChrView() == 1, "K: second press selects CHR state #1");
        while (NES::NES_PPU::ChrView() != -1)
            NES::NES_PPU::CycleChrView();
        NES_PPU_Memory::BGPalette[1]->Value(0x0F);
        NES_PPU_Memory::BGPalette[2]->Value(0x0F);
    }

    /// $2001 bit 1 clear hides the background in the leftmost 8 pixels; the
    /// backdrop colour shows there instead (Dracula relies on this).
    void TestLeftColumnMask()
    {
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(false);
        constexpr uint16_t kSolidTile = 212;
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kSolidTile * 16 + i]->Value(0xFF);
            NES_PPU_Memory::PatternTableN[0][kSolidTile * 16 + 8 + i]->Value(0x00);
        }
        for (size_t k = 0; k < 960; ++k)
            NES_PPU_Memory::NameTableN[0][k]->Value(kSolidTile);
        for (auto& cell : NES_PPU_Memory::AttributeTableN[0])
            cell->Value(0);
        NES_PPU_Memory::BGPalette[0]->Value(0x21);
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;

        NES_PPU_Register::PPUMASK.m(false);
        NES::NES_PPU::RenderStaticSnapshot();
        Check(NES::NES_PPU::BackgroundBufferPixel(3, 10).A == 0,
              "left column: with $2001 bit 1 clear, background pixels x<8 must be hidden");
        Check(NES::NES_PPU::BackgroundBufferPixel(20, 10).A != 0,
              "left column: background beyond x=8 must stay visible");
        NES_PPU_Register::PPUMASK.m(true);
        NES::NES_PPU::RenderStaticSnapshot();
        Check(NES::NES_PPU::BackgroundBufferPixel(3, 10).A != 0,
              "left column: with $2001 bit 1 set, background pixels x<8 are shown");
        NES_PPU_Memory::BGPalette[1]->Value(0x0F);
    }

    /// Controller port: all buttons released at power-up (SELECT used to start
    /// pressed), reads carry open-bus bit 6 ($40, the high address byte), and
    /// a strobe write always restarts the read sequence at button A.
    void TestControllerReadProtocol()
    {
        NES_GamePad::Controller fresh;
        for (const auto& b : fresh.Button)
            Check(!b.second, "Controller: button " + b.first + " must start released");

        for (auto& b : NES_GamePad::Player1.Button)
            b.second = false;
        NES_GamePad::Player1.Button[0].second = true; // A
        NES_Memory::Memory[0x4016]->Value(1);
        NES_Memory::Memory[0x4016]->Value(0);
        Check(NES_Memory::Memory[0x4016]->Value() == 0x41, "Controller: first read = A pressed plus open-bus bit ($41)");
        Check(NES_Memory::Memory[0x4016]->Value() == 0x40, "Controller: second read = B released plus open-bus bit ($40)");
        // Leave the sequence part-way, then strobe: the next read must be A again.
        NES_Memory::Memory[0x4016]->Value(1);
        NES_Memory::Memory[0x4016]->Value(0);
        Check(NES_Memory::Memory[0x4016]->Value() == 0x41, "Controller: a strobe write must restart the sequence at A");
        // After the 8 buttons a standard controller returns 1 on every read.
        NES_Memory::Memory[0x4016]->Value(1);
        NES_Memory::Memory[0x4016]->Value(0);
        for (int i = 0; i < 8; ++i)
            NES_Memory::Memory[0x4016]->Value();
        Check(NES_Memory::Memory[0x4016]->Value() == 0x41, "Controller: the 9th read returns 1 (plus open bus)");
        Check(NES_Memory::Memory[0x4016]->Value() == 0x41, "Controller: every further read also returns 1");
        NES_GamePad::Player1.Button[0].second = false;
    }

    /// A mid-frame double $2006 write copies t into v, so the following
    /// scanlines are drawn from the new nametable row - the way Tiny Toon
    /// Adventures and Dracula draw their status bars.
    void TestMidFrameAddressLoadSplitsTheScreen()
    {
        NES_PPU_Register::PPUCTRL.B(false);
        NES_PPU_Register::PPUCTRL.N(0);
        NES_PPU_Register::PPUMASK.b(true);
        NES_PPU_Register::PPUMASK.s(false);
        constexpr uint16_t kRedTile = 210, kBlueTile = 211;
        for (int i = 0; i < 8; ++i)
        {
            NES_PPU_Memory::PatternTableN[0][kRedTile * 16 + i]->Value(0xFF);
            NES_PPU_Memory::PatternTableN[0][kRedTile * 16 + 8 + i]->Value(0x00);
            NES_PPU_Memory::PatternTableN[0][kBlueTile * 16 + i]->Value(0x00);
            NES_PPU_Memory::PatternTableN[0][kBlueTile * 16 + 8 + i]->Value(0xFF);
        }
        for (size_t k = 0; k < 960; ++k)
            NES_PPU_Memory::NameTableN[0][k]->Value(k < 24 * 32 ? kRedTile : kBlueTile);
        for (auto& cell : NES_PPU_Memory::AttributeTableN[0])
            cell->Value(0);
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES_PPU_Memory::BGPalette[2]->Value(0x21);
        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        while (NES::NES_PPU::CurrentScanline() != 50)
            NES::NES_PPU::AdvanceDots(1);
        // $2006 = $0300: nametable 0, coarse Y 24 (the blue rows).
        NES::NES_PPU::ScrollXoY = true;
        NES_Memory::Memory[0x2006]->Value(0x03);
        NES_Memory::Memory[0x2006]->Value(0x00);
        while (NES::NES_PPU::CurrentScanline() != 100)
            NES::NES_PPU::AdvanceDots(1);

        NES::NES_PPU::Color red = NES::NES_PPU::DecodeBackgroundTileFresh(kRedTile, 0).GetPixel(0, 0);
        NES::NES_PPU::Color blue = NES::NES_PPU::DecodeBackgroundTileFresh(kBlueTile, 0).GetPixel(0, 0);
        Check(!(red == blue), "split test setup: red and blue tiles must differ");
        Check(NES::NES_PPU::BackgroundBufferPixel(20, 30) == red,
              "scanline split: rows before the $2006 load keep the original nametable rows");
        Check(NES::NES_PPU::BackgroundBufferPixel(20, 80) == blue,
              "scanline split: rows after the $2006 load are drawn from the loaded address");

        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);
        Check(!NES::NES_PPU::ScrollSplitActive(),
              "scanline split: the split ends at the pre-render line");
        NES_PPU_Memory::BGPalette[1]->Value(0x0F);
        NES_PPU_Memory::BGPalette[2]->Value(0x0F);
    }

    /// Regression test for the real IRQ-dispatch-stall bug found live via
    /// Tiny Toon Adventures (MMC3): a user-flagged screenshot showed a
    /// coherent player sprite next to a completely fragmented, garbled
    /// enemy sprite, plus a chaotic splotch mixed into otherwise-correct
    /// background tiles. Traced through MMC3's scanline-IRQ-driven CHR-bank
    /// split down to Interrupt::isIRQ() itself: a targeted trace showed
    /// IRQ() sitting pending, undispatched, for anywhere from hundreds to
    /// over nine thousand consecutive isIRQ() calls at a stretch. Root
    /// cause: isIRQ() gated re-entry on `!NES_Register::P.Interrupt()` -
    /// the CPU's real 6502 I flag - but also unconditionally set that same
    /// flag true the moment the block was first entered, before its own
    /// SevenClock countdown had a chance to reach zero and actually
    /// dispatch. Every call after the first found the flag already true
    /// and skipped the whole block, so SevenClock never finished counting
    /// down and the interrupt was silently never delivered - stuck until
    /// something external (typically the next NMI's own dispatch-then-RTI
    /// cycle) incidentally cleared P's I flag by accident. Fixed with a
    /// dedicated irqDispatchInProgress flag, matching isNMI()'s own
    /// already-correct pattern (see its own FIXED note) instead of
    /// overloading P's real status flag as a re-entry guard.
    void TestIrqDispatchDoesNotStallIndefinitely()
    {
        constexpr uint16_t kHandlerStart = 0x9100;
        // A full 4KB NOP island (not just a handful of bytes right around
        // PC's starting point) - deliberately generous so PC can never
        // wander into neighboring, *uninitialized* memory left over from
        // another test in this same shared NES_Memory::Memory[] array even
        // if dispatch unexpectedly takes many extra steps. An earlier,
        // tighter version of this test (16 NOPs) did exactly that: PC ran
        // off the end into a stray leftover 0x00 byte, which - being a real
        // BRK opcode - set Interrupt::BRK() itself (see BRK_00()), and
        // *that* dispatch (sharing $FFFE/$FFFF with IRQ) reached
        // kHandlerStart too, silently making the test pass for the wrong
        // reason without ever actually exercising isIRQ()'s own dispatch
        // path at all.
        for (uint32_t addr = 0x8000; addr < 0x9000; ++addr)
            NES_Memory::Memory[addr]->value(0xEA); // NOP
        NES_Memory::Memory[kHandlerStart]->value(0x40); // RTI

        NES_Memory::Memory[0xFFFE]->value(static_cast<uint8_t>(kHandlerStart)); // IRQ/BRK vector low
        NES_Memory::Memory[0xFFFF]->value(static_cast<uint8_t>(kHandlerStart >> 8)); // IRQ/BRK vector high

        // Explicitly clear all three interrupt lines first - a *different*
        // test earlier in this same shared binary (e.g.
        // TestNmiRestoresInterruptFlagOnRti, which runs immediately before this one)
        // could otherwise leave IRQ/NMI/BRK pending or P's I flag set,
        // silently borrowing this test's shared SevenClock countdown for
        // an unrelated dispatch and producing the exact same false-pass
        // symptom described above.
        Interrupt::IRQ(false);
        Interrupt::NMI(false);
        Interrupt::BRK(false);
        NES_Register::PC = 0x8000;
        NES_Register::S = 0xFD;
        NES_Register::P.P = 0x20;
        NES_Register::P.Interrupt(false); // interrupts enabled - a real game's normal running state

        for (int i = 0; i < 3; ++i)
            NES_CPU::Step();

        Interrupt::IRQ(true);
        // isIRQ() waits ~7 Check() calls (SevenClock) before actually
        // jumping, same real-hardware-latency approximation isNMI() uses -
        // step until entry is actually observed, with a generous but still
        // *bounded* budget: the whole point of this test is that dispatch
        // must NOT take anywhere close to the thousands of calls the real
        // bug exhibited.
        bool entered = false;
        for (int i = 0; i < 20 && !entered; ++i)
        {
            NES_CPU::Step();
            entered = NES_Register::PC == kHandlerStart;
        }
        Check(entered, "IRQ dispatch: a requested IRQ should enter the handler within a small, bounded number of "
                       "steps (~SevenClock's delay), not stall indefinitely until something unrelated happens to "
                       "clear P's I flag");

        // A *second*, later IRQ (requested only after the first handler
        // returned via RTI) must still dispatch promptly too - proving the
        // fix's own new irqDispatchInProgress flag correctly resets after a
        // completed dispatch instead of getting stuck itself, and that P's
        // I flag is correctly restored to false by RTI rather than left
        // masking future IRQs.
        for (int i = 0; i < 3; ++i)
            NES_CPU::Step();
        Interrupt::IRQ(true);
        entered = false;
        for (int i = 0; i < 20 && !entered; ++i)
        {
            NES_CPU::Step();
            entered = NES_Register::PC == kHandlerStart;
        }
        Check(entered, "IRQ dispatch: a fresh IRQ request after the previous handler returned must still enter "
                       "the handler promptly");
    }

    /// Regression test for the two PPUPCADDR overflow bugs fixed in
    /// NES_PPU/Memory/NES_PPU_Register.cpp (see INITPPUADDR()/INITPPUDATA()'s
    /// own FIXED notes) - a real, reproducible segfault found live via The
    /// Lion King (AxROM): PPUPCADDR could grow past NES_PPU_Memory::Memory's
    /// 16384-entry bounds (both from an unmasked $2006 two-write build-up,
    /// and from a $2007 auto-increment - PPUCTRL.I()'s 32-byte step - that
    /// could overshoot the $4000 wraparound point without landing on it
    /// exactly), and indexing it via operator[] is undefined behavior, not
    /// a safe bounds-checked exception. This test would
    /// crash (or, run under AddressSanitizer, be flagged immediately) if
    /// either mask regressed.
    void TestPPUAddressOverflow()
    {
        // $2006 write-side: two writes of $FF each, unmasked, would build
        // PPUPCADDR = 0xFFFF - far past NES_PPU_Memory::Memory's bounds.
        NES_Memory::Memory[0x2006]->Value(0xFF);
        NES_Memory::Memory[0x2006]->Value(0xFF);
        Check(NES_PPU_Register::PPUPCADDR == 0x3FFF,
              "PPUADDR: two $FF writes to $2006 must mask to the real 14-bit VRAM address space (got 0x" +
                  std::to_string(NES_PPU_Register::PPUPCADDR) + ", expected 0x3FFF)");

        // $2007 auto-increment side: start near the top of the address
        // space with 32-byte increments (PPUCTRL.I()) enabled, and write
        // enough times that an unmasked increment would overshoot $4000
        // without ever landing on it exactly (the old exact-equality
        // wraparound check's actual bug).
        NES_Memory::Memory[0x2000]->Value(0x04); // PPUCTRL.I() = 32-byte increment
        NES_Memory::Memory[0x2006]->Value(0x3F); // high byte -> base $3F00
        NES_Memory::Memory[0x2006]->Value(0xF0); // low byte  -> PPUPCADDR = $3FF0
        Check(NES_PPU_Register::PPUPCADDR == 0x3FF0, "PPUADDR: sanity check on the $3FF0 setup address itself");

        for (int i = 0; i < 4; ++i)
        {
            NES_Memory::Memory[0x2007]->Value(0xAB);
            Check(NES_PPU_Register::PPUPCADDR <= 0x3FFF,
                  "PPUDATA: $2007 auto-increment (32-byte step) must never leave PPUPCADDR pointing "
                  "past NES_PPU_Memory::Memory's real 16384-entry bounds (iteration " + std::to_string(i) +
                  ", got 0x" + std::to_string(NES_PPU_Register::PPUPCADDR) + ")");
        }
    }

    /// Regression test for NES_CPU::FrameCycles() (see NES_CPU.cpp's own
    /// comment on it, and NES_Console::RenderFrame()'s comment for the bug
    /// this whole mechanism fixes) - the real root cause behind Chip 'n
    /// Dale's MMC1 shift-register corruption was that this port used to
    /// trigger a new emulated frame (and therefore NMI) from the UI
    /// thread's own wall-clock polling rate rather than from a real,
    /// fixed CPU-cycle cadence. FrameCycles() is the one constant that
    /// entire fix hinges on: if a future edit fat-fingers 89342, the /3.0
    /// (or /3.2 for PAL) divisor, or the Mod::none case, NES_CPU::Run()
    /// would silently go back to firing frames/NMIs at the wrong cadence
    /// with no other test catching it (nothing else in this project
    /// exercises Run() - see its own comment - only Step() directly). Cross
    /// -referenced against http://wiki.nesdev.com/w/index.php/Cycle_reference_chart:
    /// NTSC = 341 dots/scanline * 262 scanlines/frame / 3 dots-per-CPU-cycle
    /// = 29780.6667 CPU cycles/frame; PAL = 341*312/3.2 = 33247.5.
    void TestFrameCyclesMatchesRealHardwareTiming()
    {
        const Mod savedMod = NES_CPU::mod;

        NES_CPU::mod = Mod::NTSC;
        double ntscFrameCycles = NES_CPU::FrameCycles();
        Check(ntscFrameCycles > 29780.6 && ntscFrameCycles < 29780.7,
              "FrameCycles: NTSC must be 89342/3 = 29780.667 CPU cycles/frame (got " +
                  std::to_string(ntscFrameCycles) + ")");

        NES_CPU::mod = Mod::PAL;
        double palFrameCycles = NES_CPU::FrameCycles();
        Check(palFrameCycles > 33247.4 && palFrameCycles < 33247.6,
              "FrameCycles: PAL must be 106392/3.2 = 33247.5 CPU cycles/frame (got " +
                  std::to_string(palFrameCycles) + ")");

        NES_CPU::mod = Mod::none;
        Check(NES_CPU::FrameCycles() == 0.0,
              "FrameCycles: Mod::none (used only by test harnesses calling Step() directly, never Run()) "
              "must disable the frame-cadence accumulator rather than divide by a meaningless frame length");

        NES_CPU::mod = savedMod;
    }

    /// Regression test for NES_PPU::AdvanceDots()'s dot/scanline wrap math
    /// (see its own comment in NES_PPU.Display.cpp for the full story -
    /// this is the fix for this port having no independent PPU
    /// scanline/dot clock at all, found via three separate real-game bugs:
    /// MMC1 shift-register corruption from too-frequent NMI, MMC3
    /// status-bar CHR-bank splits landing on the wrong scanline, and
    /// nametable-streaming desync during horizontal scroll). If a future
    /// edit fat-fingers the 341-dots/scanline or 262-scanlines/frame
    /// constants, or the *3 CPU-cycle-to-dot ratio, this is the only test
    /// that would catch it - nothing else in this project calls
    /// AdvanceDots() at all (only NES_CPU::Run() does, which no test
    /// exercises - see NES_CPU.cpp's own comment on Run()).
    void TestScanlineCadence()
    {
        int startScanline = NES::NES_PPU::CurrentScanline();
        int startDot = NES::NES_PPU::CurrentDot();

        // 114 CPU cycles * 3 dots/cycle = 342 dots - just over one full
        // 341-dot scanline, so this must cross exactly one scanline
        // boundary (not zero, not two) regardless of the clock's exact
        // starting dot (which may be nonzero - this test doesn't assume
        // it runs first or that the clock starts at (0,0)).
        for (int i = 0; i < 114; ++i)
            NES::NES_PPU::AdvanceDots(1);

        int totalDots = startDot + 342;
        int expectedDot = totalDots % 341;
        int expectedScanline = (startScanline + totalDots / 341) % 262;

        Check(NES::NES_PPU::CurrentDot() == expectedDot,
              "AdvanceDots: 342 dots worth of cycles must leave the dot counter at exactly "
              "(startDot + 342) % 341 (got " + std::to_string(NES::NES_PPU::CurrentDot()) +
              ", expected " + std::to_string(expectedDot) + ")");
        Check(NES::NES_PPU::CurrentScanline() == expectedScanline,
              "AdvanceDots: 342 dots worth of cycles must cross exactly floor((startDot+342)/341) "
              "scanline boundaries, wrapping at 262 (got scanline " +
              std::to_string(NES::NES_PPU::CurrentScanline()) + ", expected " + std::to_string(expectedScanline) + ")");
    }

    /// Regression test for NES_PPU::OnScanlineStart()'s vblank/NMI timing -
    /// the actual fix for the MMC1 shift-register corruption class of bug
    /// (see NES_PPU.Display.cpp's own comment): vblank (PPUSTATUS.V()) and
    /// NMI must both become true for the first time exactly at scanline
    /// 241, never before - a regression here would silently go back to
    /// firing NMI at some other (wrong) cadence, exactly the bug class
    /// this whole redesign exists to close.
    void TestVblankFiresAtScanline241()
    {
        // Seek to a known-clean starting point (scanline 261, the
        // pre-render line - OnScanlineStart(261) unconditionally clears
        // PPUSTATUS.V/S/O) one CPU cycle (3 dots) at a time, so this test
        // doesn't depend on whatever scanline TestScanlineCadence() above
        // happened to leave the clock on, and never skips past 261 (a
        // coarser step could cross two scanline boundaries in one call).
        while (NES::NES_PPU::CurrentScanline() != 261)
            NES::NES_PPU::AdvanceDots(1);

        // Arm PPUCTRL's NMI-enable bit while PPUSTATUS.V() is guaranteed
        // false (just cleared by the pre-render line above), so this
        // doesn't trip the real "already in vblank" 0->1 immediate-NMI
        // race (PPUCTRLFlags::V()'s own FIXED note) - this test wants to
        // isolate *only* the scanline-241 trigger, not that separate,
        // already-covered behavior.
        NES_PPU_Register::PPUCTRL.V(true);
        Interrupt::NMI(false);

        // Post-render line (240): still well before vblank - nothing
        // should have fired yet.
        while (NES::NES_PPU::CurrentScanline() != 240)
            NES::NES_PPU::AdvanceDots(1);
        Check(!NES_PPU_Register::PPUSTATUS.V() && !Interrupt::NMI(),
              "OnScanlineStart: vblank/NMI must not fire before scanline 241 (post-render line 240 reached)");

        while (NES::NES_PPU::CurrentScanline() != 241)
            NES::NES_PPU::AdvanceDots(1);
        Check(NES_PPU_Register::PPUSTATUS.V(),
              "OnScanlineStart(241): PPUSTATUS vblank flag must be set exactly at scanline 241");
        Check(Interrupt::NMI(),
              "OnScanlineStart(241): NMI must fire at scanline 241 when PPUCTRL.V() (NMI enable) is set");

        NES_PPU_Register::PPUCTRL.V(false);
        Interrupt::NMI(false);
    }

    /// Regression test for a real bug found live while verifying Phase C
    /// of the scanline-accurate PPU redesign (per-scanline background
    /// rendering - see NES_PPU::RenderBackgroundScanline()'s own comment):
    /// AdvanceDots() used to report "frame completed" when currentScanline
    /// wrapped to 0 - but OnScanlineStart(0) had *already run* by that
    /// point, resetting NES_PPU's backgroundBuffer to a blank canvas and
    /// re-rendering only its first row (see OnScanlineStart()'s own
    /// `scanline == 0` branch). NES_CPU::Run() calls
    /// NES_Console::RenderFrame() (which publishes backgroundBuffer)
    /// exactly when AdvanceDots() returns true - so it was publishing the
    /// *next* frame's barely-started buffer instead of the frame that had
    /// just actually finished. Symptom: Chip 'n Dale's title screen (and
    /// every other real ROM tested) rendered almost entirely black despite
    /// the renderer itself producing correct pixel data. Fixed by moving
    /// the completion signal to scanline 240 (the post-render line,
    /// immediately after the last visible scanline 239 finishes and before
    /// anything touches the buffer again).
    void TestFrameCompletionFiresAtScanline240()
    {
        // Seek to a known-clean starting point (scanline 0) one CPU cycle
        // (3 dots) at a time, same reasoning as TestVblankFiresAtScanline241().
        while (NES::NES_PPU::CurrentScanline() != 0)
            NES::NES_PPU::AdvanceDots(1);

        int scanlineWhenCompleted = -1;
        for (int i = 0; i < 262 * 115 && scanlineWhenCompleted < 0; ++i)
            if (NES::NES_PPU::AdvanceDots(1))
                scanlineWhenCompleted = NES::NES_PPU::CurrentScanline();

        Check(scanlineWhenCompleted == 240,
              "AdvanceDots: frame-completion must be signalled at scanline 240 (right after the last "
              "visible scanline 239 finishes), not at scanline 0 (which has already reset "
              "backgroundBuffer for the *next* frame by the time this fires) - got scanline " +
                  std::to_string(scanlineWhenCompleted));
    }

    /// Regression test for Phase E of the scanline-accurate PPU redesign
    /// (see NES_PPU_Register.cpp's INITPPUSTATUS()/INITPPUADDR() own FIXED
    /// notes for the full story): reading $2002 must reset only the
    /// shared PPUSCROLL/PPUADDR write-toggle (NES_PPU::ScrollXoY), never
    /// the live scroll position or an in-progress $2006 VRAM address -
    /// real hardware only ever resets the toggle. The old code wrote 0
    /// through the real $2005/$2006 write paths instead, silently zeroing
    /// a game's live scroll/address as a side effect of merely polling
    /// vblank.
    void TestPPUStatusReadOnlyResetsWriteToggle()
    {
        // A known scroll position via two real $2005 writes.
        NES_Memory::Memory[0x2005]->Value(0x11); // X
        NES_Memory::Memory[0x2005]->Value(0x22); // Y

        // A known, *completed* $2006 VRAM address via two real writes.
        NES_Memory::Memory[0x2006]->Value(0x21); // high byte
        NES_Memory::Memory[0x2006]->Value(0x08); // low byte -> PPUPCADDR = 0x2108
        uint16_t addrBefore = NES_PPU_Register::PPUPCADDR;

        // xBefore/yBefore captured *after* the $2006 write, not before: a
        // completed $2006 write landing in nametable space now legitimately
        // repositions xScroll/yScroll too (see NES_PPU::SetScrollFromPPUAddr()'s
        // own FIXED note - real hardware's $2005 and $2006 share the same
        // v/t register), so this is the real baseline the $2002 read below
        // must not disturb - not the earlier, now-superseded $2005-only value.
        int xBefore = NES::NES_PPU::xScroll;
        int yBefore = NES::NES_PPU::yScroll;

        // The real "poll vblank" idiom - must not touch scroll or address.
        NES_Memory::Memory[0x2002]->Value();

        Check(NES::NES_PPU::xScroll == xBefore && NES::NES_PPU::yScroll == yBefore,
              "$2002 read: must not alter the live PPUSCROLL X/Y position (was xScroll=" +
                  std::to_string(xBefore) + " yScroll=" + std::to_string(yBefore) + ", now xScroll=" +
                  std::to_string(NES::NES_PPU::xScroll) + " yScroll=" + std::to_string(NES::NES_PPU::yScroll) + ")");
        Check(NES_PPU_Register::PPUPCADDR == addrBefore,
              "$2002 read: must not alter a completed $2006 VRAM address (was 0x" +
                  std::to_string(addrBefore) + ", now 0x" + std::to_string(NES_PPU_Register::PPUPCADDR) + ")");

        // The toggle must still have actually been reset: the *next* $2006
        // write after a $2002 read must be treated as the first (high
        // byte) of a fresh pair, not a stray continuation of whatever
        // parity was in effect before the read.
        NES_Memory::Memory[0x2006]->Value(0x30); // first write of a new pair - must NOT commit yet
        Check(NES_PPU_Register::PPUPCADDR == addrBefore,
              "$2002 read: must reset the write-toggle so the next $2006 write starts a fresh pair "
              "(high byte alone must not commit a new address)");
        NES_Memory::Memory[0x2006]->Value(0x00); // second write - now it commits
        Check(NES_PPU_Register::PPUPCADDR == 0x3000,
              "$2002 read: after resetting the toggle, a full two-write $2006 sequence must still "
              "correctly commit (got 0x" + std::to_string(NES_PPU_Register::PPUPCADDR) + ", expected 0x3000)");
    }

    /// Regression test for the per-frame tile-decode cache added to
    /// NES_PPU::DecodeBackgroundTileFresh() (see NES_PPU.Tile.cpp's own
    /// comment for the full story): found live as a real, severe
    /// performance regression ("ultra slow" gameplay reported live) once
    /// per-scanline background rendering went fully uncached to fix the
    /// original patternArray staleness bug - a shared background tile got
    /// fully re-decoded from raw CHR bits on *every* scanline it appeared
    /// on (up to 15360 times/frame) instead of once. Caching within a
    /// single frame recovers nearly all of that performance while staying
    /// safe: the staleness bug this whole redesign fixed specifically
    /// required a decode to survive *past* a frame boundary
    /// (NES_PPU_Palette::setAllPaletesAsOld()), which a cache cleared every
    /// frame (NES_PPU::ClearFreshTileCaches(), called from
    /// OnScanlineStart()'s `scanline == 0` branch) can never do. This test
    /// proves both halves: the cache actually caches (a color change
    /// *without* clearing still returns the old color) and it's correctly
    /// bounded to one frame (clearing it makes the new color visible).
    void TestBackgroundTileCacheClearedPerFrame()
    {
        constexpr uint16_t kTileID = 5;
        constexpr int kPalette = 0;
        // Background palette 0, color 1 - NES_PPU_Memory::BGPalette holds
        // master-palette *indices* (0-63), not raw RGB - two visibly
        // different indices are all that's needed here.
        NES_PPU_Memory::BGPalette[1]->Value(0x01);
        NES::NES_PPU::Color firstColor = NES::NES_PPU::DecodeBackgroundTileFresh(kTileID, kPalette).GetPixel(0, 0);

        NES_PPU_Memory::BGPalette[1]->Value(0x30);
        NES::NES_PPU::Color stillCachedColor = NES::NES_PPU::DecodeBackgroundTileFresh(kTileID, kPalette).GetPixel(0, 0);
        Check(stillCachedColor == firstColor,
              "DecodeBackgroundTileFresh: within the same frame (no ClearFreshTileCaches() call "
              "in between), the same (tile, palette) must hit the cache and keep returning the "
              "color from the first decode, even after the underlying palette RAM changes");

        NES::NES_PPU::ClearFreshTileCaches();
        NES::NES_PPU::Color freshColor = NES::NES_PPU::DecodeBackgroundTileFresh(kTileID, kPalette).GetPixel(0, 0);
        Check(freshColor != firstColor,
              "DecodeBackgroundTileFresh: ClearFreshTileCaches() (called once per real frame) must "
              "make the next decode see the *current* palette RAM again, not keep returning a "
              "previous frame's stale cached color - this is the actual fix for the original "
              "patternArray staleness bug, now reproduced at frame granularity instead of never");
    }

    /// Regression test for NES_PPU::DrowOneNameTable()'s alpha-blend bug
    /// (see NES_PPU.NameTable.cpp's own FIXED note for the full story):
    /// found live, reported by direct side-by-side comparison of the debug
    /// Name Table window against the real game window at the same scroll
    /// position - "the black spots in the Name window are white". A
    /// background tile's palette index 0 always decodes to
    /// Color::Transparent() (A=0, R=255,G=255,B=255 - matching real
    /// hardware's "index 0 = show the universal background color", not an
    /// opaque white pixel). DrowOneNameTable() used to place each decoded
    /// tile with DrawNewImage() (plain overwrite, alpha-blind), which
    /// painted that literal opaque white straight onto the debug canvas
    /// instead of letting the canvas's already-black baseline show through
    /// - exactly like the bug RenderBackgroundScanline() (the real,
    /// on-screen renderer) was already fixed for earlier this session (see
    /// its own comment on the same DrawNewImage-vs-DrawImage distinction).
    /// With no ROM loaded, NES_PPU_Memory::PatternTableN is all-zero, so
    /// every tile decodes as fully palette-index-0 (fully transparent) -
    /// exactly the case this bug mishandled - giving a minimal repro with
    /// no synthetic CHR data needed.
    void TestNameTableViewerBlendsTransparentTilesAsBlack()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        NES::NES_PPU::Picture nameTable = NES::NES_PPU::NameTabele(true);
        NES::NES_PPU::Color pixel = nameTable.GetPixel(4, 4);
        Check(pixel.R == 0 && pixel.G == 0 && pixel.B == 0,
              "NameTabele(): a fully palette-index-0 (transparent) tile must blend to black "
              "(R=G=B=0) against the debug canvas's black baseline, not literal opaque white "
              "(got R=" + std::to_string(pixel.R) + " G=" + std::to_string(pixel.G) +
              " B=" + std::to_string(pixel.B) + ")");
    }

    /// Regression test for NES_PPU::DrowOneNameTable()'s tile-cache-source
    /// bug (see NES_PPU.NameTable.cpp's own FIXED note for the full story):
    /// found live via the real Chip and Dale ROM - once the transparent-vs-
    /// white bug above was fixed, the debug Name Table window still showed
    /// entirely black tiles at a moment the real game window showed full
    /// color, for the *same* underlying tile IDs. Root cause:
    /// DrowOneNameTable() decoded tiles via the old cached
    /// Tile()/CreateTileBitmap() path (patternArray) - a once-per-*frame*
    /// cache that can permanently freeze a tile's color from an earlier
    /// moment (e.g. before an MMC1 game's CHR-bank switch from a menu
    /// screen to real gameplay) - exactly the staleness bug
    /// DecodeBackgroundTileFresh() exists to avoid (see its own comment).
    ///
    /// Reproduces patternArray's *exact* staleness mechanism (see
    /// DecodeBackgroundTileFresh()'s own comment for the canonical
    /// description): decode once (color A, caching it), then write color B
    /// (marking the palette dirty) *without* re-decoding, then call
    /// NES_PPU_Palette::setAllPaletesAsOld() - simulating the frame
    /// boundary that clears color B's dirty flag before anything ever
    /// observed it. Tile()'s dirty-flag check (BGIsNew()) then finds
    /// nothing dirty on its *next* call and serves the stale color-A
    /// bitmap - the flag was cleared *after* the write but *before* the
    /// next access, exactly the ordering the original bug needs. (Clearing
    /// the flag *before* writing color B, instead, would leave the write
    /// itself dirty again - Tile()'s own check already handles that
    /// simpler ordering correctly, so it would not reproduce anything.)
    void TestNameTableViewerUsesFreshDecodeNotStaleTileCache()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        // Tile 0, pixel (0,0): bit 7 of the low bitplane byte set, high
        // bitplane byte left 0 -> palette index 1 at pixel (px=0, py=0) -
        // see NES_PPU::CreateNewTile()'s pixel math. Needed because with no
        // ROM loaded (an all-zero PatternTableN), every tile decodes as
        // fully palette-index-0 (transparent) by default, which would mask
        // any color difference this test needs to observe.
        NES_PPU_Memory::PatternTableN[0][0]->Value(0x80);

        constexpr int kPalette = 0;
        NES_PPU_Memory::BGPalette[1]->Value(0x01);
        NES::NES_PPU::Color staleColor = NES::NES_PPU::Tile(0, kPalette).GetPixel(0, 0); // warms patternArray

        NES_PPU_Memory::BGPalette[1]->Value(0x30); // dirty, but never observed by Tile() before the clear below
        NES::NES_PPU_Palette::setAllPaletesAsOld(); // frame boundary - clears the dirty flag from the write above
        NES::NES_PPU::ClearFreshTileCaches(); // simulates the same frame boundary; patternArray untouched

        NES::NES_PPU::Color debugViewColor = NES::NES_PPU::NameTabele(true).GetPixel(0, 0);
        Check(debugViewColor != staleColor,
              "NameTabele(): DrowOneNameTable() must decode tiles via DecodeBackgroundTileFresh(), "
              "not the old cached Tile()/patternArray path - otherwise it can permanently show a "
              "color decoded on an earlier frame even after the palette (or CHR bank) changes, "
              "exactly the 'debug window black, real game window full color' regression found "
              "live via Chip and Dale");
    }

    /// Regression test for the user-requested speed-control feature
    /// (NES_CPU::speedMultiplier - see its own comment for the full
    /// story). Only the observable contract is tested here - clamping and
    /// the real-NTSC-speed default - not Sleep()'s actual timed busy-wait,
    /// which would make this test slow/flaky for no extra coverage (the
    /// clamp bounds are exactly what stands between a stray '+'/'-' key
    /// repeat and a divide-by-zero or negative Sleep() target).
    void TestSpeedMultiplierClampsToSaneRange()
    {
        Check(NES::NES_Console::getSpeedMultiplier() == 1.0,
              "NES_Console::getSpeedMultiplier(): should default to 1.0 (real NTSC speed)");

        NES::NES_Console::setSpeedMultiplier(1000.0);
        Check(NES::NES_Console::getSpeedMultiplier() <= 64.0,
              "NES_Console::setSpeedMultiplier(): must clamp an excessive value down to "
              "NES_CPU::kMaxSpeedMultiplier, not let Sleep()'s target delay shrink unbounded");

        NES::NES_Console::setSpeedMultiplier(-5.0);
        Check(NES::NES_Console::getSpeedMultiplier() > 0.0,
              "NES_Console::setSpeedMultiplier(): must clamp a non-positive value up to "
              "NES_CPU::kMinSpeedMultiplier - a zero or negative multiplier would make "
              "Sleep()'s target delay divide-by-zero or go negative");

        NES::NES_Console::setSpeedMultiplier(1.0); // restore the default for any test that runs after this one
    }

    /// Regression test for the user-requested save/load-state feature
    /// (NES_SaveState - see its own header comment for the full story).
    /// Sets a handful of representative, easy-to-corrupt-by-accident
    /// pieces of state (a CPU register, a CPU RAM byte, a PPU palette
    /// byte, live scroll position) to known values, saves, *changes* them
    /// all to different values (simulating gameplay continuing after the
    /// save), then loads back and checks the original values return -
    /// proving the round-trip actually restores state rather than, say,
    /// silently no-op'ing on a write path that never got hooked up.
    void TestSaveStateRoundTrips()
    {
        const std::string path = "/tmp/nes_savestate_roundtrip_test.sav";

        NES::NES_Register::PC = 0x1234;
        NES::NES_Register::A = 0x42;
        NES_Memory::Memory[0x0300]->value(0x77);
        NES_PPU_Memory::BGPalette[1]->Value(0x0A);
        NES::NES_PPU::xScroll = 123;

        bool saved = NES::NES_SaveState::Save(path);
        Check(saved, "NES_SaveState::Save(): should succeed writing to a normal, writable temp path");

        // Simulate gameplay continuing after the save - every one of these
        // must differ from what was just saved for this test to mean
        // anything.
        NES::NES_Register::PC = 0x9999;
        NES::NES_Register::A = 0x00;
        NES_Memory::Memory[0x0300]->value(0x00);
        NES_PPU_Memory::BGPalette[1]->Value(0x30);
        NES::NES_PPU::xScroll = 0;

        bool loaded = NES::NES_SaveState::Load(path);
        Check(loaded, "NES_SaveState::Load(): should succeed reading back a file Save() just wrote");

        Check(NES::NES_Register::PC == 0x1234, "NES_SaveState round-trip: PC should be restored");
        Check(NES::NES_Register::A == 0x42, "NES_SaveState round-trip: A should be restored");
        Check(NES_Memory::Memory[0x0300]->value() == 0x77, "NES_SaveState round-trip: a CPU RAM byte should be restored");
        Check(NES_PPU_Memory::BGPalette[1]->Value() == 0x0A, "NES_SaveState round-trip: a PPU palette byte should be restored");
        Check(NES::NES_PPU::xScroll == 123, "NES_SaveState round-trip: live scroll position should be restored");

        // A save file must be refused as corrupt/foreign, not misread, if
        // it isn't one this class actually wrote (e.g. an empty/garbage
        // file) - Load() must leave current state untouched rather than
        // half-applying it.
        std::ofstream garbage(path, std::ios::binary | std::ios::trunc);
        garbage << "not a save file";
        garbage.close();
        bool loadedGarbage = NES::NES_SaveState::Load(path);
        Check(!loadedGarbage, "NES_SaveState::Load(): must refuse a file that isn't its own format, not misread it");
        Check(NES::NES_Register::PC == 0x1234,
              "NES_SaveState::Load(): a refused/corrupt file must leave current state completely untouched");

        std::remove(path.c_str());
    }

    /// Regression test for the swapped bankForSlot pairing bug fixed in
    /// NES_PPU_Memory::RewireNameTableMirroring() (see its own FIXED note) -
    /// found live via real gameplay ("mirroring is vertical but the game
    /// renders split left/right instead of top/bottom"), confirmed against
    /// https://www.nesdev.org/wiki/Mirroring: *vertical* mirroring pairs
    /// $2000/$2800 (left column) into one physical bank and $2400/$2C00
    /// (right column) into the other - a left/right split, needing a
    /// *vertical* divider line here; *horizontal* mirroring pairs
    /// $2000/$2400 (top row) into one bank and $2800/$2C00 (bottom row)
    /// into the other - a top/bottom split, needing a *horizontal* divider
    /// line.
    void TestNameTableDebugOverlayMirroringDividerOrientation()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        INES::Mirror savedArrangement = INES::arrangement;

        int w = NES::NES_PPU::NameTabele(true).Width();
        int h = NES::NES_PPU::NameTabele(true).Height();

        INES::arrangement = INES::Mirror::vertical;
        NES::NES_PPU::Picture verticalOverlay = NES::NES_PPU::NameTabeleDebugOverlay();
        Check(verticalOverlay.GetPixel(w / 2 - 1, h / 4) == NES::NES_PPU::Color::Green(),
              "NameTabeleDebugOverlay: vertical mirroring (left/right bank split) needs a vertical divider line");
        Check(!(verticalOverlay.GetPixel(w / 4, h / 2) == NES::NES_PPU::Color::Green()),
              "NameTabeleDebugOverlay: vertical mirroring must not also draw a horizontal divider line");

        INES::arrangement = INES::Mirror::horisontal;
        NES::NES_PPU::Picture horizontalOverlay = NES::NES_PPU::NameTabeleDebugOverlay();
        Check(horizontalOverlay.GetPixel(w / 4, h / 2 - 1) == NES::NES_PPU::Color::Green(),
              "NameTabeleDebugOverlay: horisontal mirroring (top/bottom bank split) needs a horizontal divider line");
        Check(!(horizontalOverlay.GetPixel(w / 2, h / 4) == NES::NES_PPU::Color::Green()),
              "NameTabeleDebugOverlay: horisontal mirroring must not also draw a vertical divider line");

        INES::arrangement = savedArrangement;
    }

    /// Regression test for the swapped quadrant-skip/mirror-axis bug fixed
    /// in NES_PPU::NameTabele() and NES_PPU::DrawMirror() (see their own
    /// FIXED notes) - a *second*, independent occurrence of the same
    /// vertical/horizontal axis mix-up already fixed once in
    /// RewireNameTableMirroring(), found live via real gameplay after that
    /// first fix: "the Name Table debug window still shows all 4 quadrants
    /// identical (A=B=C=D)". NameTabele() decides which of the 4 quadrants
    /// to actually decode fresh (the other two are meant to be filled in by
    /// a cheap pixel-copy from DrawMirror()) - RewireNameTableMirroring()
    /// fixing which *physical bank* each slot reads from doesn't help if
    /// this separate decision of which slots to *draw at all* still picks
    /// the wrong two.
    void TestNameTableDebugOverlayQuadrantsPairCorrectly()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        INES::Mirror savedArrangement = INES::arrangement;

        // DecodeBackgroundTileFresh() reads from PatternTableN[1] instead
        // of [0] whenever PPUCTRL.B() is set - force it false so the tile
        // data below (written to PatternTableN[0]) is actually the data
        // read back, regardless of what an earlier test in this file left
        // PPUCTRL.B() as.
        NES_PPU_Register::PPUCTRL.B(false);

        // Tile 0: pixel (0,0) = palette index 1 (low-plane bit 7 set).
        // Tile 1 (16 bytes later - 8 low-plane + 8 high-plane bytes per
        // tile): pixel (0,0) = palette index 2 (high-plane bit 7 set,
        // low-plane bit 7 clear) - see CreateNewTile()'s pixel math, same
        // reasoning TestNameTableViewerUsesFreshDecodeNotStaleTileCache()
        // uses. Two different, non-transparent indices, mapped to two
        // *visually* distinct real NES palette entries (0x01 blue-ish,
        // 0x16 red-ish - deliberately not 0x30/0x20/0x10, the palette's own
        // near-white entries, which would be indistinguishable by RGB alone
        // from Color::Transparent()'s white and defeat this test's whole
        // point).
        NES_PPU_Memory::PatternTableN[0][0]->Value(0x80);
        NES_PPU_Memory::PatternTableN[0][16]->Value(0x00);
        NES_PPU_Memory::PatternTableN[0][24]->Value(0x80);
        NES_PPU_Memory::BGPalette[1]->Value(0x01);
        NES_PPU_Memory::BGPalette[2]->Value(0x16);
        NES::NES_PPU::ClearFreshTileCaches(); // an earlier test's decode of tile 0/1 may still be cached
        // Force attribute group 0 for every physical bank, in case an
        // earlier test left a nonzero attribute byte behind that would
        // otherwise select a different (untouched, still-0x00) BGPalette
        // group for one of the two banks under test.
        for (auto& bank : NES_PPU_Memory::AttributeTablePhysicalBanks())
            for (auto& cell : bank)
                cell->Value(0);

        INES::arrangement = INES::Mirror::vertical;
        NES_PPU_Memory::RewireNameTableMirroring();
        NES_PPU_Memory::NameTableN[0][0]->Value(0); // slot 0 (top-left): tile 0
        NES_PPU_Memory::NameTableN[1][0]->Value(1); // slot 1 (top-right): tile 1
        NES::NES_PPU::Picture verticalOverlay = NES::NES_PPU::NameTabele(true);
        NES::NES_PPU::Color topLeft = verticalOverlay.GetPixel(0, 0);
        NES::NES_PPU::Color topRight = verticalOverlay.GetPixel(256, 0);
        NES::NES_PPU::Color bottomLeft = verticalOverlay.GetPixel(0, 240);
        NES::NES_PPU::Color bottomRight = verticalOverlay.GetPixel(256, 240);
        NES::NES_PPU::Color expectedTile1Color = NES::NES_PPU::DecodeBackgroundTileFresh(1, 0).GetPixel(0, 0);
        Check(!(topLeft == topRight),
              "NameTabele(): vertical mirroring - left column (real bank) and right column (a "
              "genuinely different real bank) must show different content, not collapse to A=B");
        Check(topRight == expectedTile1Color,
              "NameTabele(): vertical mirroring - slot 1 (top-right) is real, independent data "
              "under vertical mirroring and must actually be decoded/drawn, not skipped as if it "
              "were redundant (a DrawMirror() fix alone can't compensate for this - it would just "
              "copy forward whatever slot 1 was left as, drawn or not)");
        Check(topLeft == bottomLeft,
              "NameTabele()/DrawMirror(): vertical mirroring - left column's bottom half must "
              "mirror-match its own top half (same physical bank, top->bottom copy)");
        Check(topRight == bottomRight,
              "NameTabele()/DrawMirror(): vertical mirroring - right column's bottom half must "
              "mirror-match its own top half");

        INES::arrangement = INES::Mirror::horisontal;
        NES_PPU_Memory::RewireNameTableMirroring();
        NES_PPU_Memory::NameTableN[0][0]->Value(0); // slot 0 (top-left): tile 0
        NES_PPU_Memory::NameTableN[2][0]->Value(1); // slot 2 (bottom-left): tile 1
        NES::NES_PPU::Picture horizontalOverlay = NES::NES_PPU::NameTabele(true);
        topLeft = horizontalOverlay.GetPixel(0, 0);
        topRight = horizontalOverlay.GetPixel(256, 0);
        bottomLeft = horizontalOverlay.GetPixel(0, 240);
        bottomRight = horizontalOverlay.GetPixel(256, 240);
        Check(!(topLeft == bottomLeft),
              "NameTabele(): horisontal mirroring - top row (real bank) and bottom row (a "
              "genuinely different real bank) must show different content, not collapse to A=C");
        Check(bottomLeft == expectedTile1Color,
              "NameTabele(): horisontal mirroring - slot 2 (bottom-left) is real, independent data "
              "under horisontal mirroring and must actually be decoded/drawn, not skipped as if it "
              "were redundant");
        Check(topLeft == topRight,
              "NameTabele()/DrawMirror(): horisontal mirroring - top row's right half must "
              "mirror-match its own left half (same physical bank, left->right copy)");
        Check(bottomLeft == bottomRight,
              "NameTabele()/DrawMirror(): horisontal mirroring - bottom row's right half must "
              "mirror-match its own left half");

        INES::arrangement = savedArrangement;
        NES_PPU_Memory::RewireNameTableMirroring();
    }

    /// Regression test for DrawDisplayFrame()'s X-axis wraparound threshold
    /// (NES_PPU.NameTable.cpp) - a real copy-paste bug (compared XScroll()
    /// against 240, the Y-axis viewport height, instead of 256, the X-axis
    /// viewport width) found via code review while investigating a user
    /// report of the debug Name Table window's viewport rectangle "jumping
    /// entirely back to the start" near the right edge. Empirically proven
    /// (via a throwaway sweep across XScroll 200-511, since removed) that
    /// this specific threshold change has NO observable effect on the
    /// rendered marker - the wraparound copy's own offset is always
    /// `XScroll() - 512` regardless of which threshold gates it, and for
    /// every XScroll value where the two thresholds disagree (240, 256],
    /// that offset is far enough negative to fall entirely outside the
    /// 512-wide canvas either way, so the erroneously-early trigger was
    /// already a harmless no-op. Kept as the geometrically-correct value
    /// (512 - 256 = 256, matching the already-correct Y-axis derivation:
    /// 480 - 240 = 240) on correctness grounds, not because it explains the
    /// reported symptom - the real cause of that jump was found separately,
    /// via live trace: the raw PPUSCROLL X byte wrapping 0xfe->0x00 without
    /// PPUCTRL's nametable-select bit toggling alongside it (see
    /// NES_PPU.Scroll.cpp's AddxScroll() - this port's scroll model has no
    /// equivalent of real hardware's autonomous per-scanline coarse-X wrap,
    /// https://www.nesdev.org/wiki/PPU_scrolling's "Coarse X increment").
    /// This test only pins down that the corrected threshold still produces
    /// continuous (non-gapped) partial-wraparound coverage for the X range
    /// it actually does affect (XScroll > 256).
    void TestDrawDisplayFrameWraparoundIsContinuous()
    {
        NES::NES_PPU::ClearChrRowSnapshots();
        NES_PPU_Register::PPUCTRL.N(1); // X nametable select on, so AddxScroll() can reach 256-511
        NES::NES_PPU::ScrollXoY = true;
        NES_PPU_Register::PPUSCROLL->Value(static_cast<uint8_t>(300 - 256)); // XScroll() -> 300
        NES_PPU_Register::PPUSCROLL->Value(0);
        NES::NES_PPU::Picture bitmap = NES::NES_PPU::NameTabeleDebugOverlay();

        auto hasRedAt = [&](int px) {
            for (int py = 0; py < 480; py++)
                if (bitmap.GetPixel(px, py) == NES::NES_PPU::Color::Red())
                    return true;
            return false;
        };

        // Primary rectangle's left border (at XScroll()=300) and the
        // wraparound copy's right border (at XScroll()-512+255 = 43) must
        // both be present - the two halves of one continuous viewport that
        // wrapped off the right edge and back in on the left.
        Check(hasRedAt(300), "DrawDisplayFrame: primary viewport's left border must still be drawn at XScroll()");
        Check(hasRedAt(43), "DrawDisplayFrame: wraparound copy's right border must appear near the canvas start "
                             "(XScroll() - 512 + 255), proving the missing right-edge portion reappears on the left");
    }

    /// Regression test for the real horizontal-scroll "jump to origin" bug
    /// reported live in Chip 'n Dale (an MMC1 game) and root-caused via a
    /// disassembly of its own NMI handler plus a user-captured before/after
    /// save-state pair: the handler writes $2005 (X scroll) *before* $2000
    /// (PPUCTRL, whose bits 0-1 select which nametable that scroll continues
    /// into) - both perfectly legal on real hardware (different bit fields
    /// of the same shared `t` register, combined only at render time,
    /// independent of write order), but this port's old NES_PPU::XScroll(int)
    /// baked AddxScroll()'s decision in immediately using PPUCTRL's bit *at
    /// $2005-write time* - i.e. the previous frame's bit, since the game's
    /// own write to the new bit (from its zero-page $fd) always arrives a
    /// few instructions later in the same handler. Reproduces the exact
    /// write sequence found in the disassembly (STA $2005 with the wrapped
    /// raw byte 0x00, then STA $2000 with the nametable-select bit newly
    /// set) and checks XScroll() ends up 256 (continuing smoothly into the
    /// next nametable), not 0 (silently dropped back to the start) - which
    /// is exactly what a live-captured save right after the bug showed
    /// (PPUCTRL.N=1 but xScroll=0).
    void TestScrollSurvivesPPUCTRLWriteAfterPPUSCROLL()
    {
        NES_PPU_Register::PPUCTRL.N(0);
        NES::NES_PPU::ScrollXoY = true;
        NES_PPU_Register::PPUSCROLL->Value(0xfe); // raw X just before the wrap, PPUCTRL.N still 0
        NES_PPU_Register::PPUSCROLL->Value(0);    // Y write, toggles ScrollXoY back to X-next
        Check(NES::NES_PPU::xScroll == 0xfe, "sanity: xScroll should be the raw byte with PPUCTRL.N=0");

        // The wrap: game writes the new (wrapped) raw X first...
        NES_PPU_Register::PPUSCROLL->Value(0x00);
        NES_PPU_Register::PPUSCROLL->Value(0);
        // ...then PPUCTRL, with the nametable-select bit now set - exactly
        // the c08b/c09e order found in Chip 'n Dale's own NMI handler.
        NES_PPU_Register::PPUCTRL.adress->Value(0x91); // bit0 set (N&1=1), matches the live trace - Value()
                                                         // (not value()) so the real $2000 write hook fires

        Check(NES::NES_PPU::xScroll == 256,
              "xScroll: a PPUCTRL write with a new nametable-select bit, arriving *after* the $2005 X write "
              "it belongs with in the same frame, must still be reflected (expected 256, continuing into the "
              "second nametable) rather than silently staying at the raw byte's own value (0)");

        NES_PPU_Register::PPUCTRL.N(0);
    }

    /// Regression test for NES_PPU::RenderBackgroundScanline()'s missing
    /// PPUMASK.b() ("show background") check - see NES_PPU.Display.cpp's
    /// own FIXED note for the full story. Found live while investigating a
    /// user-reported Bram Stoker's Dracula symptom ("flickers between
    /// frames, sometimes normal sometimes text") by replaying the user's
    /// own recorded 3772-frame input session and diffing consecutive
    /// dumped frames against each other: one single frame (out of six
    /// dumped around the point of divergence) came back solid black, with
    /// `NES_TRACE_WRITE=2001` showing the game had written $2001=0x00
    /// (both background and sprites disabled) right beforehand. Per
    /// http://wiki.nesdev.com/w/index.php/PPU_registers ($2001 bit 3, "1:
    /// Show background") and http://wiki.nesdev.com/w/index.php/PPU_rendering
    /// ("If the background or sprites are disabled ... the backdrop color
    /// is shown"), real hardware must show the universal background color
    /// (palette index $3F00) for any scanline rendered while this bit is
    /// clear - not whatever nametable/CHR data happens to be in VRAM at
    /// that moment, which a game mid-transition may be actively rewriting.
    /// RenderSpriteScanline() right below it in the same file already
    /// correctly gated on the equivalent `PPUMASK.s()` bit; this function
    /// had no such check at all before this fix, unconditionally decoding
    /// and drawing nametable tiles on every scanline regardless of the
    /// enable bit.
    void TestBackgroundScanlineRespectsRenderEnableBit()
    {
        constexpr uint16_t kTileID = 1;

        // Forced to bank 0 explicitly - PPUCTRL.B() (background
        // pattern-table-select) is process-global state an earlier test may
        // have left at 1, which would make DecodeBackgroundTileFresh() read
        // PatternTableN[1] while this test only ever writes PatternTableN[0].
        NES_PPU_Register::PPUCTRL.B(false);

        // Tile 1, pixel (0,0): bit 7 of the low bitplane byte set -> palette
        // index 1 there (same construction TestNameTableViewerUsesFreshDecodeNotStaleTileCache
        // above uses for tile 0), so this tile visibly differs from a fully
        // transparent (index-0) tile once actually decoded. PatternTableN is
        // indexed [bank][byteOffsetWithinBank], not [tileID][byteOffset] -
        // tile kTileID's own low-bitplane row-0 byte lives at offset
        // kTileID*16 within bank 0 (16 bytes/tile), not at PatternTableN[kTileID][0]
        // (which would instead write into a whole different *bank*, k=0/1 only).
        NES_PPU_Memory::PatternTableN[0][kTileID * 16]->Value(0x80);
        // Nametable 0, row 0, col 0 (k=0) - the exact cell RenderBackgroundScanline()
        // reads for (screenY=0, xScroll=0, yScroll=0).
        NES_PPU_Memory::NameTableN[0][0]->Value(kTileID);
        // Attribute byte covering this same top-left 2x2 tile block (see
        // NES_PPU_AttributeTable::SubBlock()'s shift1=0 case for k=0) -
        // forced to 0 so the attribute-selected palette group is known
        // (group 0) regardless of what an earlier test left behind here,
        // matching the pallete=0 assumed below.
        NES_PPU_Memory::AttributeTableN[0][0]->Value(0);
        // Tile's own color (palette index 0x16 - a clearly non-white/non-black
        // red) and the backdrop (0x21, a clearly different blue) - deliberately
        // NOT 0x30/0x0F/0x00, whose RGB can coincide with Color::Transparent()'s
        // placeholder RGB (255,255,255) or Color::Black(), which would make an
        // R/G/B-only mismatch invisible to this test.
        NES_PPU_Memory::BGPalette[1]->Value(0x16);
        NES_PPU_Memory::BGPalette[0]->Value(0x21);

        NES::NES_PPU::xScroll = 0;
        NES::NES_PPU::yScroll = 0;

        NES_PPU_Register::PPUMASK.b(false);
        NES::NES_PPU::RenderBackgroundScanline(0);
        NES::NES_PPU::Color disabledColor = NES::NES_PPU::BackgroundBufferPixel(0, 0);
        Check(disabledColor == NES::NES_PPU_Palette::UniversalBackgroundColor(),
              "RenderBackgroundScanline(): with PPUMASK.b()=false, a scanline must show the "
              "universal background color, not real nametable tile data (real hardware never "
              "fetches nametable/CHR data for background rendering while this bit is clear)");

        NES_PPU_Register::PPUMASK.b(true);
        NES::NES_PPU::ClearFreshTileCaches();
        NES::NES_PPU::RenderBackgroundScanline(0);
        NES::NES_PPU::Color enabledColor = NES::NES_PPU::BackgroundBufferPixel(0, 0);
        Check(enabledColor != NES::NES_PPU_Palette::UniversalBackgroundColor(),
              "RenderBackgroundScanline(): with PPUMASK.b()=true, a scanline with real, "
              "non-transparent tile data must not fall back to the backdrop color");
        Check(enabledColor == NES::NES_PPU::DecodeBackgroundTileFresh(kTileID, 0).GetPixel(0, 0),
              "RenderBackgroundScanline(): with PPUMASK.b()=true, the rendered pixel must match "
              "the tile actually decoded from live nametable/pattern-table data");
    }

    /// Regression test for NES_CPU::completedFrames - see its own comment
    /// in NES_CPU.h for the full story (a real bug found live via
    /// non-reproducible screenshots: replaying the exact same recorded
    /// Bram Stoker's Dracula input file from a cold process start, twice,
    /// landed on visibly different game states at the same nominal frame
    /// number depending on concurrent system load). NES/main.cpp's UI loop
    /// used to number frames off its own free-running loop-iteration
    /// counter, decoupled from how many real NES frames the CPU thread had
    /// actually completed; this counter (incremented in NES_CPU::Run(),
    /// paired 1:1 with each real NES_Console::RenderFrame() call) is the
    /// fix, giving the UI thread a real, monotonic count of completed
    /// 262-scanline frames instead.
    ///
    /// Exercises the real NES_CPU::Run() loop (via NES_Console::Run(), the
    /// same entry point NES/main.cpp's own CPU thread uses) for a short,
    /// bounded slice of real time - not just the atomic in isolation -
    /// since the actual bug this guards against is a missing/misplaced
    /// increment at Run()'s one real call site, which manipulating the
    /// atomic directly wouldn't catch. speedMultiplier is set to its
    /// maximum first so the loop advances many real frames within the
    /// real-time budget below, keeping this reasonably fast and (with a
    /// loose lower bound rather than an exact count) non-flaky under real
    /// scheduling jitter. The budget/bound pair below is deliberately
    /// generous - found live: an initial 150ms/>=10 pairing (tuned against
    /// a plain Release build) failed under build-asan, whose per-memory-
    /// access instrumentation only reached 2 completed frames in that same
    /// window - a real ~5-10x-plus slowdown this test must tolerate, not a
    /// code regression (ASan/TSan builds are part of this project's own
    /// verification discipline, so this test must pass there too, not just
    /// in the fast default build).
    void TestCompletedFramesCountsRealRunLoopFrames()
    {
        NES::NES_CPU::speedMultiplier.store(NES::NES_CPU::kMaxSpeedMultiplier, std::memory_order_relaxed);
        long long before = NES::NES_CPU::completedFrames.load(std::memory_order_relaxed);

        std::thread cpuThread([]() { NES::NES_Console::Run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        NES::NES_Console::Stop();
        cpuThread.join();

        long long after = NES::NES_CPU::completedFrames.load(std::memory_order_relaxed);
        Check(after >= before + 5,
              "NES_CPU::completedFrames: running the real NES_CPU::Run() loop (via NES_Console::Run(), "
              "same entry point NES/main.cpp's CPU thread uses) for 2s at max speed must advance "
              "this counter by a meaningful amount (got " + std::to_string(after - before) +
              "), proving RenderFrame() and the counter increment are still paired at Run()'s real "
              "call site - not just that the atomic itself supports being incremented");

        NES::NES_CPU::speedMultiplier.store(1.0, std::memory_order_relaxed); // restore the default for later tests
    }

    /// Regression test for NES_CPU::Step()'s branch cycle-counting fix -
    /// see kPageCrossSensitiveOpcode's own comment in NES_CPU.cpp for the
    /// full story (kCycleTable never accounted for the real "+1 if taken,
    /// +1 more if the target crosses a page" rule -
    /// http://wiki.nesdev.com/w/index.php/CPU_addressing_modes). Writes a
    /// real BNE instruction directly into memory and checks Step()'s
    /// *returned cycle count* (not just PC) for all three real-hardware
    /// cases: not taken, taken within the same page, taken across a page
    /// boundary.
    void TestBranchCycleCounting()
    {
        constexpr uint8_t kBNE = 0xD0;
        constexpr int kBaseCycles = 2; // kCycleTable[0xD0]

        // Not taken: Z=1 (BNE branches only when Z=0).
        NES_Register::PC = 0x8000;
        NES_Memory::Memory[0x8000]->value(kBNE);
        NES_Memory::Memory[0x8001]->value(0x05);
        NES_Register::P.Zero(true);
        int cyclesNotTaken = NES_CPU::Step();
        Check(cyclesNotTaken == kBaseCycles,
              "BNE not taken: Step() must return exactly kCycleTable[0xD0]=2, no extra cycles "
              "(got " + std::to_string(cyclesNotTaken) + ")");

        // Taken, same page: target 0x8007 (2-byte instruction + 0x05 offset
        // from 0x8002) stays within page 0x80.
        NES_Register::PC = 0x8000;
        NES_Memory::Memory[0x8000]->value(kBNE);
        NES_Memory::Memory[0x8001]->value(0x05);
        NES_Register::P.Zero(false);
        int cyclesTakenSamePage = NES_CPU::Step();
        Check(cyclesTakenSamePage == kBaseCycles + 1,
              "BNE taken, same page: Step() must return kCycleTable[0xD0]+1=3 "
              "(got " + std::to_string(cyclesTakenSamePage) + ")");

        // Taken, crosses a page: start near the end of page 0x80 with an
        // offset large enough that target 0x80F2+0x14=0x8106 lands in page
        // 0x81.
        NES_Register::PC = 0x80F0;
        NES_Memory::Memory[0x80F0]->value(kBNE);
        NES_Memory::Memory[0x80F1]->value(0x14);
        NES_Register::P.Zero(false);
        int cyclesTakenPageCross = NES_CPU::Step();
        Check(cyclesTakenPageCross == kBaseCycles + 2,
              "BNE taken, crosses page: Step() must return kCycleTable[0xD0]+2=4 "
              "(got " + std::to_string(cyclesTakenPageCross) + ")");
    }

    /// Regression test for NES_CPU::Step()'s indexed-addressing page-cross
    /// cycle-counting fix - see kPageCrossSensitiveOpcode's own comment in
    /// NES_CPU.cpp. Confirms the dynamic +1 applies to a *read* instruction
    /// (LDA abs,X) when it crosses a page, is absent when it doesn't, and -
    /// critically, since Parameter::ax()/ay()/zpy1() are shared by
    /// read/write/RMW opcodes alike - is *never* applied to a write (STA
    /// abs,X), which real hardware always charges the same fixed cost
    /// either way (already correct as a flat kCycleTable value).
    void TestPageCrossOnlyAppliesToReadInstructions()
    {
        constexpr uint8_t kLDA_absX = 0xBD;
        constexpr uint8_t kSTA_absX = 0x9D;
        constexpr int kLdaBase = 4; // kCycleTable[0xBD]
        constexpr int kStaBase = 5; // kCycleTable[0x9D]

        // LDA $30F0,X with X=0x20 -> 0x3110, crosses from page 0x30 to 0x31.
        NES_Register::PC = 0x8200;
        NES_Register::X = 0x20;
        NES_Memory::Memory[0x8200]->value(kLDA_absX);
        NES_Memory::Memory[0x8201]->value(0xF0);
        NES_Memory::Memory[0x8202]->value(0x30);
        int ldaCrossing = NES_CPU::Step();
        Check(ldaCrossing == kLdaBase + 1,
              "LDA abs,X crossing a page: Step() must return kCycleTable[0xBD]+1=5 "
              "(got " + std::to_string(ldaCrossing) + ")");

        // LDA $3000,X with X=0x20 -> 0x3020, same page - no extra cycle.
        NES_Register::PC = 0x8200;
        NES_Register::X = 0x20;
        NES_Memory::Memory[0x8200]->value(kLDA_absX);
        NES_Memory::Memory[0x8201]->value(0x00);
        NES_Memory::Memory[0x8202]->value(0x30);
        int ldaSamePage = NES_CPU::Step();
        Check(ldaSamePage == kLdaBase,
              "LDA abs,X same page: Step() must return kCycleTable[0xBD]=4, no extra cycle "
              "(got " + std::to_string(ldaSamePage) + ")");

        // STA $30F0,X with X=0x20 -> 0x3110, crosses a page too, but STA is
        // a *write* - real hardware charges the same fixed cost regardless.
        NES_Register::PC = 0x8200;
        NES_Register::X = 0x20;
        NES_Memory::Memory[0x8200]->value(kSTA_absX);
        NES_Memory::Memory[0x8201]->value(0xF0);
        NES_Memory::Memory[0x8202]->value(0x30);
        int staCrossing = NES_CPU::Step();
        Check(staCrossing == kStaBase,
              "STA abs,X crossing a page: Step() must still return kCycleTable[0x9D]=5, no extra "
              "cycle - the page-cross penalty is read-only-instruction-specific on real hardware "
              "(got " + std::to_string(staCrossing) + ")");
    }

    /// Regression test for OAM DMA's real CPU stall - see
    /// NES_PPU_OAM::OAMDMA()'s own comment for the full story ($4014 stalls
    /// the CPU for 513 or 514 cycles on real hardware, a long-documented
    /// explicit non-goal of this port's scanline-accurate redesign, found
    /// directly relevant while root-causing a real CPU-state divergence
    /// from a reference emulator - http://wiki.nesdev.com/w/index.php/PPU_OAM,
    /// "DMA"). Writes `STA $4014` directly and checks Step()'s returned
    /// cycle count is the base STA-absolute cost (4) plus *either* 513 or
    /// 514 - parity-agnostic, since NES_CPU::totalCyclesEver is a real
    /// global counter shared by every test in this file (order-dependent,
    /// not something this test can predict or reset without disturbing
    /// every other test's own cycle accounting).
    void TestOAMDMAStallsCPU()
    {
        constexpr uint8_t kSTA_abs = 0x8D;
        constexpr int kStaBase = 4; // kCycleTable[0x8D]

        NES_Register::PC = 0x8300;
        NES_Register::A = 0x02; // DMA source page $02
        NES_Memory::Memory[0x8300]->value(kSTA_abs);
        NES_Memory::Memory[0x8301]->value(0x14);
        NES_Memory::Memory[0x8302]->value(0x40); // operand = $4014
        int cycles = NES_CPU::Step();
        Check(cycles == kStaBase + 513 || cycles == kStaBase + 514,
              "STA $4014 (OAM DMA trigger): Step() must return kCycleTable[0x8D] (4) plus the real "
              "513-or-514-cycle CPU stall real hardware imposes, not the bare instruction cost alone "
              "(got " + std::to_string(cycles) + ", expected " + std::to_string(kStaBase + 513) +
              " or " + std::to_string(kStaBase + 514) + ")");
    }
}

int main()
{
    NES_Console::INIT();

    // Power-on RAM is all zero by default: some programs read RAM they never wrote (a Super Mario Bros.
    // dump starts in the hidden 0-1 world if $013E holds $FF).
    Check(NES_Memory::Memory[0x013E]->value() == 0 && NES_Memory::Memory[0x075F]->value() == 0 && NES_Memory::Memory[0x0004]->value() == 0,
          "RAM: CPU RAM must be zero at power-on");

    TestNmiIsNotBlockedByRunningHandler();
    TestNmiRestoresInterruptFlagOnRti();
    TestEnablingNmiInVblankRaisesNmi();
    TestAttributePaletteForTileMatchesFullTable();
    TestSprite0HitWorksWithoutPictureRendering();
    TestSpritePaletteMirrorsBackdrop();
    TestBackdropColourShowsThroughTransparentBackground();
    TestLeftColumnMask();
    TestNameTableViewerUsesChrStateOfDrawnRow();
    TestSpriteAndPatternViewersUseFrameChrState();
    TestControllerReadProtocol();
    TestMidFrameAddressLoadSplitsTheScreen();
    TestIrqDispatchDoesNotStallIndefinitely();
    TestPPUAddressOverflow();
    TestFrameCyclesMatchesRealHardwareTiming();
    TestScanlineCadence();
    TestVblankFiresAtScanline241();
    TestFrameCompletionFiresAtScanline240();
    TestPPUStatusReadOnlyResetsWriteToggle();
    TestNameTableViewerBlendsTransparentTilesAsBlack();
    TestSpeedMultiplierClampsToSaneRange();
    TestSaveStateRoundTrips();
    TestNameTableViewerUsesFreshDecodeNotStaleTileCache();
    TestBackgroundTileCacheClearedPerFrame();
    TestNameTableDebugOverlayMirroringDividerOrientation();
    TestNameTableDebugOverlayQuadrantsPairCorrectly();
    TestDrawDisplayFrameWraparoundIsContinuous();
    TestScrollSurvivesPPUCTRLWriteAfterPPUSCROLL();
    TestBackgroundScanlineRespectsRenderEnableBit();
    TestCompletedFramesCountsRealRunLoopFrames();
    TestBranchCycleCounting();
    TestPageCrossOnlyAppliesToReadInstructions();
    TestOAMDMAStallsCPU();

    TestMapperSkipsUnchangedBankWindows(); // last: it installs a mapper over the whole $8000-$FFFF area

    if (failures == 0)
    {
        std::cout << "PASS: all CPU/interrupt/PPU-register checks passed" << std::endl;
        return 0;
    }
    std::cerr << failures << " CPU/interrupt/PPU-register check(s) FAILED" << std::endl;
    return 1;
}
