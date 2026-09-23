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
#include "NES_PPU_Memory.h"
#include <cstdint>
#include <vector>

namespace NES
{
    /// @brief Decodes a PPU attribute table (2 bits of palette index per 16x16
    /// pixel block) into one palette index per 8x8 tile.
    /// http://wiki.nesdev.com/w/index.php/PPU_attribute_tables
    class NES_PPU_AttributeTable
    {
    public:
        /// @param NR attribute table number (0-3)
        /// @return one decoded 2-bit palette index per tile position, laid out
        /// in the same order as NES_PPU_Memory::NameTableN[NR].
        static std::vector<int> AttributeTable(int NR);

    private:
        static std::vector<int> CreateAL(const AddrVec& attributeTable);
        static void Repeate(std::vector<int>& AL, const AddrVec& attributeTable, int i, int shift1, int shift2);
        static void Block(std::vector<int>& AL, const AddrVec& attributeTable, int i, int j, int shift1, int shift2);
        static void SubBlock(std::vector<int>& AL, const AddrVec& attributeTable, int i, int j, int shift1);
        static int SplitAttribute(int shift1, uint8_t value);
        static const AddrVec& getTable(int NR);
    };
}
