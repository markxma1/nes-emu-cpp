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
#include "NES_PPU_Memory.h"
#include "INES.h"

namespace NES
{
    AddrVec NES_PPU_Memory::Memory;
    AddrVec NES_PPU_Memory::PatternTable;
    std::array<AddrVec, 2> NES_PPU_Memory::PatternTableN;
    AddrVec NES_PPU_Memory::NameTable;
    std::array<AddrVec, 4> NES_PPU_Memory::NameTableN;
    AddrVec NES_PPU_Memory::AttributeTable;
    std::array<AddrVec, 4> NES_PPU_Memory::AttributeTableN;
    AddrVec NES_PPU_Memory::BGPalette;
    AddrVec NES_PPU_Memory::SpritePalette;

    std::array<AddrVec, 4> NES_PPU_Memory::NameTablePhysical;
    std::array<AddrVec, 4> NES_PPU_Memory::AttributeTablePhysical;

    NES_PPU_Memory::NES_PPU_Memory()
    {
        CreateMemory();
        InitPatternTable();
        InitNameTable(); // also wires up the attribute tables - see RewireNameTableMirroring()
        InitPaletteRAMIndexes();
        UpdateMemory();
    }

    // Background palette gets $3F00-$3F0F, sprite palette $3F10-$3F1F - but
    // per http://wiki.nesdev.com/w/index.php/PPU_palettes ("Addresses
    // $3F10/$3F14/$3F18/$3F1C are mirrors of $3F00/$3F04/$3F08/$3F0C"), those
    // four sprite slots have no storage of their own: writing $3F10 is
    // electrically the same as writing $3F00 (the universal backdrop colour).
    // MemoryMirror() aliases them, so SpritePalette[0]/[4]/[8]/[0xC] below are
    // the very same cells as BGPalette[0]/[4]/[8]/[0xC]. FIXED: they used to be
    // separate cells, so a game writing its backdrop colour through $3F10
    // (Tiny Toon Adventures does) left the backdrop black.
    void NES_PPU_Memory::InitPaletteRAMIndexes()
    {
        for (int i = 0x3F00; i < 0x3F10; i++)
        {
            BGPalette.push_back(Memory[i]);
            SpritePalette.push_back(Memory[i + 0x10]);
        }
    }

    // --- NameTable + AttributeTable ---
    //
    // LEARNING NOTE: new design. Nametable/attribute-table mirroring
    // used to be built the way a much smaller version of this file still
    // could: by directly reassigning which AddressSetup a CPU/PPU address's
    // Memory[] slot points at, computed *once* from INES::arrangement right
    // after loading a ROM. That's fine as long as arrangement can never
    // change again after load - true for every ROM this project supported
    // until real mapper hardware was added (see the Mappers/ folder), but
    // several real mappers (MMC1, MMC3, AxROM) have a register that changes
    // mirroring *while the game is running* (e.g. switching which 1KB page
    // single-screen mode shows). The old one-shot approach can't be safely
    // re-run: once a slot's Memory[] pointer has been overwritten to alias
    // another slot, nothing keeps a *stable* handle on "this slot's own
    // storage, independent of whatever's currently displayed there" for a
    // second rewiring pass to find again.
    //
    // CreatePhysicalNameTableBanks() below creates that stable handle
    // explicitly: four permanent, independent 1KB pages
    // (NameTablePhysical/AttributeTablePhysical) that are never reassigned
    // themselves. RewireNameTableMirroring() only ever changes which of
    // those four banks each of the four *logical* nametable slots currently
    // points its Memory[]/NameTableN[]/AttributeTableN[] view at - so it can
    // be called again at any time (e.g. from a mapper's mirroring register)
    // without ever losing a bank's contents. http://wiki.nesdev.com/w/index.php/Mirroring
    void NES_PPU_Memory::CreatePhysicalNameTableBanks()
    {
        for (int bank = 0; bank < 4; bank++)
        {
            NameTablePhysical[bank].clear();
            for (int i = 0; i < 0x3C0; i++)
                NameTablePhysical[bank].push_back(std::make_shared<AddressSetup>(0));

            AttributeTablePhysical[bank].clear();
            for (int i = 0; i < 0x40; i++)
                AttributeTablePhysical[bank].push_back(std::make_shared<AddressSetup>(0));
        }
    }

    void NES_PPU_Memory::InitNameTable()
    {
        CreatePhysicalNameTableBanks();
        RewireNameTableMirroring();
    }

    void NES_PPU_Memory::RewireNameTableMirroring()
    {
        static const int kNameTableStart[4] = { 0x2000, 0x2400, 0x2800, 0x2C00 };
        static const int kAttributeTableStart[4] = { 0x23C0, 0x27C0, 0x2BC0, 0x2FC0 };

        // FIXED (was a preserved bug, now corrected - found live via real
        // gameplay: "mirroring is set to vertical but the game renders as
        // if it were split left/right instead of top/bottom", eventually
        // traced to nametable-streaming writes landing in a physically
        // correct bank that this table then displayed in the wrong
        // quadrant). Verified against https://www.nesdev.org/wiki/Mirroring
        // ("To configure a cartridge board for horizontal mirroring,
        // connect PPU A11 to CIRAM A10" / "...for vertical mirroring,
        // connect PPU A10 to CIRAM A10"), which states plainly: "A vertical
        // arrangement of the nametables results in horizontal mirroring...
        // A horizontal arrangement of the nametables results in vertical
        // mirroring" - i.e. *vertical* mirroring pairs $2000/$2800 (left
        // column) and $2400/$2C00 (right column) - a left/right split -
        // while *horizontal* mirroring pairs $2000/$2400 (top row) and
        // $2800/$2C00 (bottom row) - a top/bottom split. This table
        // (and the comment it replaces) had the two cases' pairings
        // completely swapped.
        int bankForSlot[4];
        switch (INES::arrangement)
        {
            case INES::Mirror::vertical:
                bankForSlot[0] = 0; bankForSlot[1] = 1; bankForSlot[2] = 0; bankForSlot[3] = 1;
                break;
            case INES::Mirror::horisontal:
                bankForSlot[0] = 0; bankForSlot[1] = 0; bankForSlot[2] = 1; bankForSlot[3] = 1;
                break;
            case INES::Mirror::single_screen_a:
                bankForSlot[0] = bankForSlot[1] = bankForSlot[2] = bankForSlot[3] = 0;
                break;
            case INES::Mirror::single_screen_b:
                bankForSlot[0] = bankForSlot[1] = bankForSlot[2] = bankForSlot[3] = 1;
                break;
            case INES::Mirror::four_screen:
            default:
                bankForSlot[0] = 0; bankForSlot[1] = 1; bankForSlot[2] = 2; bankForSlot[3] = 3;
                break;
        }

        NameTable.clear();
        AttributeTable.clear();
        for (int slot = 0; slot < 4; slot++)
        {
            int bank = bankForSlot[slot];
            NameTableN[slot] = NameTablePhysical[bank];
            AttributeTableN[slot] = AttributeTablePhysical[bank];
            for (int j = 0; j < 0x3C0; j++)
            {
                Memory[static_cast<size_t>(kNameTableStart[slot] + j)] = NameTablePhysical[bank][static_cast<size_t>(j)];
                NameTable.push_back(NameTablePhysical[bank][static_cast<size_t>(j)]);
            }
            for (int j = 0; j < 0x40; j++)
            {
                Memory[static_cast<size_t>(kAttributeTableStart[slot] + j)] = AttributeTablePhysical[bank][static_cast<size_t>(j)];
                AttributeTable.push_back(AttributeTablePhysical[bank][static_cast<size_t>(j)]);
            }
        }
    }

    // --- PatternTable ---
    void NES_PPU_Memory::InitPatternTable()
    {
        CreatePatternTable();
    }

    void NES_PPU_Memory::CreatePatternTable()
    {
        for (int i = 0; i < 0x1000; i++)
            MemoryToPatternTable(i, 0);
        for (int i = 0; i < 0x1000; i++)
            MemoryToPatternTable(0x1000 + i, 1);
    }

    void NES_PPU_Memory::MemoryToPatternTable(int i, int TableNr)
    {
        PatternTable.push_back(Memory[i]);
        PatternTableN[TableNr].push_back(Memory[i]);
    }

    // --- Memory ---
    void NES_PPU_Memory::CreateMemory()
    {
        Memory.clear();
        for (int i = 0; i <= 0x3FFF; i++)
        {
            Memory.push_back(std::make_shared<AddressSetup>(i));
            MemoryMirror(i);
        }
    }

    void NES_PPU_Memory::UpdateMemory()
    {
        for (int i = 0; i < 0x3FFF; i++)
            MemoryMirror(i);
    }

    void NES_PPU_Memory::MemoryMirror(int i)
    {
        if (i >= 0x3000 && i < 0x3F00)
            Memory[i] = Memory[i - 0x1000];
        else if (i == 0x3F10 || i == 0x3F14 || i == 0x3F18 || i == 0x3F1C)
            Memory[i] = Memory[i - 0x10];
        else if (i >= 0x3F20 && i < 0x3F20 + 0xE0)
            Memory[i] = Memory[i - 0x20];
    }
}
