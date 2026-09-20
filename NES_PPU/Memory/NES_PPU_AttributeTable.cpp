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
#include "NES_PPU_AttributeTable.h"
#include "AddressSetup.h"

namespace NES
{
    std::vector<int> NES_PPU_AttributeTable::AttributeTable(int NR)
    {
        return CreateAL(getTable(NR));
    }

    std::vector<int> NES_PPU_AttributeTable::CreateAL(const AddrVec& attributeTable)
    {
        std::vector<int> AL;
        for (int i = 0; i < 0x8; i++)
        {
            Repeate(AL, attributeTable, i, 0, 1);
            Repeate(AL, attributeTable, i, 2, 3);
        }
        return AL;
    }

    void NES_PPU_AttributeTable::Repeate(std::vector<int>& AL, const AddrVec& attributeTable, int i, int shift1, int shift2)
    {
        for (int r = 0; r < 2; r++)
            for (int j = 0; j < 0x8; j++)
                Block(AL, attributeTable, i, j, shift1, shift2);
    }

    void NES_PPU_AttributeTable::Block(std::vector<int>& AL, const AddrVec& attributeTable, int i, int j, int shift1, int shift2)
    {
        SubBlock(AL, attributeTable, i, j, shift1);
        SubBlock(AL, attributeTable, i, j, shift2);
    }

    void NES_PPU_AttributeTable::SubBlock(std::vector<int>& AL, const AddrVec& attributeTable, int i, int j, int shift1)
    {
        for (int r = 0; r < 2; r++)
            AL.push_back(SplitAttribute(shift1, attributeTable[static_cast<size_t>(j) + static_cast<size_t>(i) * 8]->value()));
    }

    int NES_PPU_AttributeTable::SplitAttribute(int shift1, uint8_t value)
    {
        return (value >> (shift1 * 2)) & 3;
    }

    const AddrVec& NES_PPU_AttributeTable::getTable(int NR)
    {
        return NES_PPU_Memory::AttributeTableN[static_cast<size_t>(NR)];
    }
}
