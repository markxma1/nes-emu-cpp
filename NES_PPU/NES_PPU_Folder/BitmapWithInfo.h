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
#include "Picture.h"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace NES
{
    /// @brief Tile-cache entry: the rasterised 8x8 picture, the raw 2-bit-per-pixel
    /// pattern data it was built from (so a palette-only change can retint
    /// without re-reading PPU memory), and which of the 4 palette slots (cID)
    /// the pattern actually uses.
    ///
    /// The `pattern` field is written once (in CreateNewTile) and only ever read
    /// afterwards (in UpdateTile) - never mutated in place - so it is stored by
    /// value (`std::array<std::array<uint8_t,8>,8>`) rather than shared; this class is a
    /// private rendering-cache helper, not core emulation state, so this is
    /// the same kind of structural freedom already used for Picture.
    class BitmapWithInfo
    {
    public:
        /// True if this tile was (re)built since it was last drawn.
        bool isNew;
        /// The palette slots (0-3) that the pattern actually uses.
        std::vector<uint8_t> cID;

        /// Creates a cache entry from the rendered tile, its raw 2-bit pattern data and the used palette slots.
        BitmapWithInfo(const ::NES_PPU::Picture& bitmap, std::array<std::array<uint8_t, 8>, 8> pattern,
                        std::vector<uint8_t> cID, bool isNew = true)
            : isNew(isNew), cID(std::move(cID)), bitmap(bitmap), pattern(pattern)
        {
        }

        /// Returns a copy of the rendered 8x8 tile picture.
        ::NES_PPU::Picture Image() const { return bitmap; }
        /// Replaces the rendered tile picture.
        void Image(const ::NES_PPU::Picture& v) { bitmap = v; }

        /// The raw 2-bit-per-pixel pattern data (8 rows of 8 pixels).
        const std::array<std::array<uint8_t, 8>, 8>& Pattern() const { return pattern; }
        /// Replaces the raw pattern data.
        void Pattern(std::array<std::array<uint8_t, 8>, 8> v) { pattern = std::move(v); }

    private:
        ::NES_PPU::Picture bitmap;
        std::array<std::array<uint8_t, 8>, 8> pattern{};
    };
}
