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
        bool isNew;
        std::vector<uint8_t> cID;

        BitmapWithInfo(const ::NES_PPU::Picture& bitmap, std::array<std::array<uint8_t, 8>, 8> pattern,
                        std::vector<uint8_t> cID, bool isNew = true)
            : isNew(isNew), cID(std::move(cID)), bitmap(bitmap), pattern(pattern)
        {
        }

        ::NES_PPU::Picture Image() const { return bitmap; }
        void Image(const ::NES_PPU::Picture& v) { bitmap = v; }

        const std::array<std::array<uint8_t, 8>, 8>& Pattern() const { return pattern; }
        void Pattern(std::array<std::array<uint8_t, 8>, 8> v) { pattern = std::move(v); }

    private:
        ::NES_PPU::Picture bitmap;
        std::array<std::array<uint8_t, 8>, 8> pattern{};
    };
}
