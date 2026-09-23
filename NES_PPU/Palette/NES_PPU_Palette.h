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
#include "NES_PPU_Color.h"
#include "Color.h"
#include <array>
#include <string>
#include <opencv2/core.hpp>

namespace NES
{
    /// @brief Resolves PPU palette RAM entries to actual RGB colors, using a
    /// reference NES palette image as the 64-colour lookup table (reading
    /// pixel values out of a small bitmap instead of hand-typing 64 RGB
    /// triplets). Split across three source files: NES_PPU_Palette.cpp (this
    /// class), .Check.cpp (change tracking) and .Load.cpp (the bitmap loader).
    /// http://wiki.nesdev.com/w/index.php/PPU_palettes
    class NES_PPU_Palette
    {
    public:
        static std::array<NES_PPU::Color, 0x40> PPUpalettes;

        /// `bmpPath` defaults to the relative path of the bundled palette image
        /// ("./Palletes/2C03and2C05.bmp", i.e. next to the running executable).
        explicit NES_PPU_Palette(const std::string& bmpPath = "./Palletes/2C03and2C05.bmp");

        static NES_PPU_Color getPalette(int Nr);
        static NES_PPU_Color DefaultPalette();
        static NES_PPU_Color getBGColorPalette(int start);
        static NES_PPU_Color getSpriteColorPalette(int start);
        static NES_PPU::Color UniversalBackgroundColor();

        // --- NES_PPU_Palette.Check.cpp ---
        static bool BGIsNew(int start);
        static bool SpriteIsNew(int start);
        static void setAllPaletesAsOld();

    private:
        static std::array<bool, 4> isNewColor;

        static NES_PPU::Color getBGColorAsRGB(int BGAdress);
        static NES_PPU::Color getSpriteColorAsRGB(int SpriteAdress);
        static NES_PPU::Color getColorAsRGB(int Adress);
        static int OctToHex(int a);

        // --- NES_PPU_Palette.Load.cpp ---
        static void InitPalletesFromBMP(const std::string& path);
        static void LoadPallete(const cv::Mat& palette);
        static void LoadRow(const cv::Mat& palette, int j);
    };
}
