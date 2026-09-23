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
#include <cstdint>

namespace NES_PPU
{
    /// @brief Minimal stand-in for .NET's `System.Drawing.Color`, just the
    /// pieces Picture/NES_PPU_Palette actually use (ARGB storage, equality,
    /// and the handful of named colors referenced elsewhere in the emulator).
    ///
    /// The default constructor is all-zero (A=0,R=0,G=0,B=0) - this matters:
    /// Picture's info layer uses a default-constructed Color as its "no pixel
    /// painted here" sentinel (`info != Color()`), the same value the
    /// `infoLayer` array is initialised with. `Transparent()` is a *different*
    /// value (A=0,R=255,G=255,B=255, matching .NET's Color.Transparent) and
    /// is compared against explicitly in NES_PPU_Palette - don't conflate the
    /// two.
    struct Color
    {
        uint8_t A = 0;
        uint8_t R = 0;
        uint8_t G = 0;
        uint8_t B = 0;

        constexpr Color() = default;
        constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : A(a), R(r), G(g), B(b) {}

        friend constexpr bool operator==(const Color& l, const Color& r)
        {
            return l.A == r.A && l.R == r.R && l.G == r.G && l.B == r.B;
        }
        friend constexpr bool operator!=(const Color& l, const Color& r) { return !(l == r); }

        // .NET named-color values used by the ported source.
        static constexpr Color Transparent() { return Color(255, 255, 255, 0); }
        static constexpr Color Black() { return Color(0, 0, 0, 255); }
        static constexpr Color Red() { return Color(255, 0, 0, 255); }
        static constexpr Color Green() { return Color(0, 128, 0, 255); } // .NET Color.Green is (0,128,0), not (0,255,0)
        static constexpr Color Blue() { return Color(0, 0, 255, 255); }
    };
}
