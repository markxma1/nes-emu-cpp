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
#pragma once
#include "AddressSetup.h"
#include <array>
#include <memory>
#include <vector>

namespace NES
{
    /// A run of PPU/OAM address cells; each entry is one shared byte cell, so mirrored slots can alias the same cell.
    using AddrVec = std::vector<std::shared_ptr<AddressSetup>>;

    /// @brief The PPU's 16KB video address space ($0000-$3FFF).
    /// https://en.wikibooks.org/wiki/NES_Programming/Memory_Map
    /// http://wiki.nesdev.com/w/index.php/PPU_memory_map
    ///
    /// Address  Size   Description
    /// $0000    $1000  Pattern Table 0
    /// $1000    $1000  Pattern Table 1
    /// $2000    $3C0   Name Table 0
    /// $23C0    $40    Attribute Table 0
    /// $2400    $3C0   Name Table 1
    /// $27C0    $40    Attribute Table 1
    /// $2800    $3C0   Name Table 2
    /// $2BC0    $40    Attribute Table 2
    /// $2C00    $3C0   Name Table 3
    /// $2FC0    $40    Attribute Table 3
    /// $3000    $F00   Mirror of $2000-$2EFF
    /// $3F00    $10    BG Palette
    /// $3F10    $10    Sprite Palette
    /// $3F20    $E0    Mirror of $3F00-$3F1F
    class NES_PPU_Memory
    {
    public:
        /// The whole 16KB PPU address space ($0000-$3FFF), one cell per address.
        static AddrVec Memory;
        /// Both pattern tables ($0000-$1FFF, the CHR data) as one view of `Memory`.
        static AddrVec PatternTable;
        /// Pattern table 0 ($0000) and 1 ($1000) as separate 4KB views.
        static std::array<AddrVec, 2> PatternTableN;
        /// All name-table bytes (the tile-index parts of the four name tables) as one view.
        static AddrVec NameTable;
        /// The four logical name tables (960 bytes each) as separate views.
        static std::array<AddrVec, 4> NameTableN;
        /// All attribute-table bytes (the palette-select parts of the four name tables) as one view.
        static AddrVec AttributeTable;
        /// The four logical attribute tables (64 bytes each) as separate views.
        static std::array<AddrVec, 4> AttributeTableN;
        /// Background palette RAM ($3F00-$3F0F).
        static AddrVec BGPalette;
        /// Sprite palette RAM ($3F10-$3F1F); slots 0/4/8/12 alias the matching background entries.
        static AddrVec SpritePalette;

        NES_PPU_Memory();

        /// Builds the pattern-table views (`PatternTable`, `PatternTableN`) over `Memory`.
        static void InitPatternTable();

        /// Re-applies INES::arrangement to the CPU/PPU-visible nametable and
        /// attribute-table address ranges. Public (unlike the rest of this
        /// class's setup) so a Mapper (see the Mappers/ folder) can call it
        /// again whenever its own mirroring-control register changes - see
        /// this function's definition in the .cpp for why that needs to be
        /// safely re-callable, which the original one-shot-at-load design
        /// wasn't.
        static void RewireNameTableMirroring();

        // New: user-requested save/load-state feature
        // (see NES_SaveState's own comment). Public specifically for that -
        // a save must capture all 4 *physical* banks regardless of which
        // ones are currently aliased into the logical NameTableN/
        // AttributeTableN slots (see RewireNameTableMirroring() above),
        // since a mirroring-mode change between save and load could
        // otherwise leave a physical bank's real content never captured at
        // all - the public logical arrays alone aren't enough for this.
        /// The four permanent physical name-table banks (see the comment above).
        static std::array<AddrVec, 4>& NameTablePhysicalBanks() { return NameTablePhysical; }
        /// The four permanent physical attribute-table banks (see the comment above).
        static std::array<AddrVec, 4>& AttributeTablePhysicalBanks() { return AttributeTablePhysical; }

    private:
        static void InitPaletteRAMIndexes();

        static void InitNameTable();
        /// Four permanent, independent 1KB (960B nametable + 64B attribute)
        /// physical VRAM pages - real hardware only has 2 onboard (CIRAM),
        /// plus up to 2 more on four-screen carts; kept as 4 uniformly here
        /// so RewireNameTableMirroring() never has to special-case
        /// four-screen. Never reassigned after creation - only *which* of
        /// these each logical nametable slot currently points its
        /// Memory[]/NameTableN[]/AttributeTableN[] view at changes.
        static std::array<AddrVec, 4> NameTablePhysical;
        static std::array<AddrVec, 4> AttributeTablePhysical;
        static void CreatePhysicalNameTableBanks();

        static void CreatePatternTable();
        static void MemoryToPatternTable(int i, int TableNr);

        static void CreateMemory();
        static void UpdateMemory();
        /// Mirrors memory addresses; uses the same mirroring approach as NES_Memory
        /// (aliasing the same AddressSetup into every mirrored slot).
        static void MemoryMirror(int i);
    };
}
