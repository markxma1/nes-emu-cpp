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
#include "NES_PPU_Palette.h"
#include "NES_PPU_Memory.h"
#include "AddressSetup.h"

namespace NES
{
    std::array<NES_PPU::Color, 0x40> NES_PPU_Palette::PPUpalettes;
    std::array<bool, 4> NES_PPU_Palette::isNewColor{};

    NES_PPU_Palette::NES_PPU_Palette(const std::string& bmpPath)
    {
        InitPalletesFromBMP(bmpPath);
    }

    NES_PPU_Color NES_PPU_Palette::getPalette(int Nr)
    {
        if (Nr < 4) return getBGColorPalette(Nr);
        if (Nr < 8) return getSpriteColorPalette(Nr - 4);
        return DefaultPalette();
    }

    NES_PPU_Color NES_PPU_Palette::DefaultPalette()
    {
        std::array<NES_PPU::Color, 4> color = { NES_PPU::Color::Black(), NES_PPU::Color::Red(), NES_PPU::Color::Green(), NES_PPU::Color::Blue() };
        isNewColor = {};
        isNewColor[0] = isNewColor[1] = isNewColor[2] = true;
        return NES_PPU_Color(color, true, isNewColor);
    }

    NES_PPU_Color NES_PPU_Palette::getBGColorPalette(int start)
    {
        std::array<NES_PPU::Color, 4> color = {
            NES_PPU::Color::Transparent(),
            getBGColorAsRGB(start * 4 + 1),
            getBGColorAsRGB(start * 4 + 2),
            getBGColorAsRGB(start * 4 + 3)
        };
        return NES_PPU_Color(color, BGIsNew(start), isNewColor);
    }

    NES_PPU_Color NES_PPU_Palette::getSpriteColorPalette(int start)
    {
        std::array<NES_PPU::Color, 4> color = {
            NES_PPU::Color::Transparent(),
            getSpriteColorAsRGB(start * 4 + 1),
            getSpriteColorAsRGB(start * 4 + 2),
            getSpriteColorAsRGB(start * 4 + 3)
        };
        return NES_PPU_Color(color, SpriteIsNew(start), isNewColor);
    }

    // Palette RAM holds 6-bit colour numbers: the PPU ignores the two upper bits, so a game may write any byte
    // (e.g. $4F = $0F). Without the mask such a value indexed past the end of the 64-entry table.
    NES_PPU::Color NES_PPU_Palette::UniversalBackgroundColor()
    {
        return PPUpalettes[NES_PPU_Memory::BGPalette[0]->Value() & 0x3F];
    }

    NES_PPU::Color NES_PPU_Palette::getBGColorAsRGB(int BGAdress)
    {
        return PPUpalettes[NES_PPU_Memory::BGPalette[static_cast<size_t>(BGAdress)]->Value() & 0x3F];
    }

    NES_PPU::Color NES_PPU_Palette::getSpriteColorAsRGB(int SpriteAdress)
    {
        return PPUpalettes[NES_PPU_Memory::SpritePalette[static_cast<size_t>(SpriteAdress)]->Value() & 0x3F];
    }

    NES_PPU::Color NES_PPU_Palette::getColorAsRGB(int Adress)
    {
        return PPUpalettes[NES_PPU_Memory::Memory[static_cast<size_t>(Adress)]->Value() & 0x3F];
    }

    int NES_PPU_Palette::OctToHex(int a)
    {
        return static_cast<int>((static_cast<double>(a) / 7) * 255);
    }
}
