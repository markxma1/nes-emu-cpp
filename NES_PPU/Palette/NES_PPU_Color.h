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
#include "Color.h"
#include <array>

namespace NES
{
    /// @brief A resolved 4-colour palette (index 0 is always transparent),
    /// plus per-colour and whole-palette "changed since last frame" flags used
    /// for the tile-cache.
    class NES_PPU_Color
    {
    public:
        /// The four RGB colours of the palette (entry 0 is transparent).
        std::array<NES_PPU::Color, 4> color{};
        /// True if any of colours 1-3 changed since the last frame.
        bool isNewPalette = false;
        /// Per-colour "changed since last frame" flags.
        std::array<bool, 4> isNewColor{};

        /// Creates a resolved palette from its colours and change flags.
        NES_PPU_Color(std::array<NES_PPU::Color, 4> color, bool isNewPalette, std::array<bool, 4> isNewColor)
            : color(color), isNewPalette(isNewPalette), isNewColor(isNewColor)
        {
        }
    };
}
