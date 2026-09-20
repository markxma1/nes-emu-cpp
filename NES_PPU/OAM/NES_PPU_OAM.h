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
#include <cstdint>
#include <memory>
#include <vector>

namespace NES
{
    /// @brief Object Attribute Memory: the 256-byte sprite table. Port of
    /// NES_PPU/OAM/NES_PPU_OAM.cs. http://wiki.nesdev.com/w/index.php/PPU_OAM
    class NES_PPU_OAM
    {
    public:
        /// Byte 1 of a sprite's 4 bytes: tile number. This bit-0-is-bank,
        /// bits-7-1-are-index layout is only correct for 8x16 sprite mode -
        /// see http://wiki.nesdev.com/w/index.php/PPU_OAM#Byte_1 and
        /// NES_PPU.Display.cpp's InsertObect() for the 8x8-mode distinction
        /// (bank comes from PPUCTRL.S() instead, and the full byte is the
        /// tile index) that callers of this struct must handle themselves.
        struct Byte1
        {
            std::shared_ptr<AddressSetup> adress;

            /// Bank ($0000 or $1000) of tiles.
            bool Bank() const { return (adress->Value() & 0x01) > 0; }
            void Bank(bool v) { adress->Value(static_cast<uint8_t>(adress->Value() & ~0x01)); if (v) adress->Value(static_cast<uint8_t>(adress->Value() | 0x01)); }

            /// Tile number of top of sprite (0 to 254; bottom half gets the next tile).
            uint8_t Number() const { return static_cast<uint8_t>(adress->Value() & 0xFE); }
            void Number(uint8_t v) { adress->Value(static_cast<uint8_t>((adress->Value() & ~0xFE) | (v & 0xFE))); }
        };

        /// Byte 2 of a sprite's 4 bytes: attributes (bit layout VHP--- ---PP
        /// per http://wiki.nesdev.com/w/index.php/PPU_OAM#Byte_2 - the three
        /// unimplemented bits between Priority and Palette always read back
        /// as 0 on real hardware and are simply unmodeled here).
        struct Byte2
        {
            std::shared_ptr<AddressSetup> adress;

            /// Palette (4 to 7) of sprite.
            uint8_t Palette() const { return static_cast<uint8_t>(adress->Value() & 0x3); }
            void Palette(uint8_t v) { adress->Value(static_cast<uint8_t>(adress->Value() & ~0x3)); adress->Value(static_cast<uint8_t>(adress->Value() | (v & 0x3))); }

            /// Priority (0: in front of background; 1: behind background).
            bool Priority() const { return (adress->Value() & 0x20) > 0; }
            void Priority(bool v) { adress->Value(static_cast<uint8_t>(adress->Value() & ~0x20)); if (v) adress->Value(static_cast<uint8_t>(adress->Value() | 0x20)); }

            /// Flip sprite horizontally.
            bool FlipH() const { return (adress->Value() & 0x40) > 0; }
            void FlipH(bool v) { adress->Value(static_cast<uint8_t>(adress->Value() & ~0x40)); if (v) adress->Value(static_cast<uint8_t>(adress->Value() | 0x40)); }

            /// Flip sprite vertically.
            bool FlipV() const { return (adress->Value() & 0x80) > 0; }
            void FlipV(bool v) { adress->Value(static_cast<uint8_t>(adress->Value() & ~0x80)); if (v) adress->Value(static_cast<uint8_t>(adress->Value() | 0x80)); }
        };

        static std::vector<std::shared_ptr<AddressSetup>> Memory;

        /// Y position of top of sprite (delayed by one scanline; subtract 1
        /// from the sprite's actual Y before writing here).
        static std::vector<std::shared_ptr<AddressSetup>> SpriteYc;
        /// Tile index number.
        static std::vector<Byte1> SpriteTile;
        static std::vector<Byte2> SpriteAttribute;
        /// X position of left side of sprite.
        static std::vector<std::shared_ptr<AddressSetup>> SpriteXc;

        NES_PPU_OAM();

        /// Common name: OAMDMA. Uploads 256 bytes from CPU page $XX00-$XXFF
        /// into OAM (see NES_PPU_Register's $4014 hook). See NES_PPU_OAM.cpp
        /// for a bug this fixes: on real hardware this is a byte-value copy
        /// that leaves OAM decoupled from CPU RAM afterward - both this port
        /// and the C# original used to alias the same AddressSetup object
        /// instead, so OAM kept "seeing" every later CPU write to that RAM
        /// page rather than a stable per-frame snapshot.
        static void OAMDMA(uint8_t XX);

    private:
        static void InitBytes();
        static void InitArrays();
        static void InitSpriteAttribute(int i);
        static void InitSpriteTile(int i);
    };
}
