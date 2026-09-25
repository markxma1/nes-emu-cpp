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
#include <string>
#include <vector>

namespace NES
{
    /// @brief Parses an iNES ROM header.
    /// http://wiki.nesdev.com/w/index.php/INES
    class INES
    {
    public:
        /// TV system flag from the iNES header (NTSC, PAL or dual-compatible).
        enum class TV { NTS, PAL, DUAL };

        /// 0xx0: vertical arrangement/horizontal mirroring (CIRAM A10 = PPU A11)
        /// 0xx1: horizontal arrangement/vertical mirroring (CIRAM A10 = PPU A10)
        /// 1xxx: four-screen VRAM
        /// single_screen_a/b: all four nametables show the same physical 1KB
        /// page (bank A or B) - not an iNES header value, only ever set at
        /// runtime by a mapper's own mirroring-control register (MMC1,
        /// AxROM - see the Mappers/ folder). New, not part of the original
        /// port - http://wiki.nesdev.com/w/index.php/Mirroring
        /// ("Single-screen mirroring").
        enum class Mirror { vertical, horisontal, four_screen, single_screen_a, single_screen_b };

        /// Current nametable mirroring; set from the header and possibly changed at runtime by a mapper.
        static Mirror arrangement;

        /// Size of PRG ROM in 16 KB units.
        static int PRGROMSize;
        /// Size of CHR ROM in 8 KB units (0 means the board uses CHR RAM).
        static int CHRROMSize;
        /// Size of PRG RAM in 8 KB units (0 infers 8 KB for compatibility).
        static int PRGRAMSize;

        /// Some ROM images additionally contain a 128 (or 127) byte title at the end of the file.
        static std::string title;

        /// Cartridge contains battery-backed PRG RAM ($6000-7FFF) or other persistent memory.
        static bool battery_backed;
        /// 512-byte trainer at $7000-$71FF (stored before PRG data).
        static bool trainer;

        // LEARNING NOTE: per http://wiki.nesdev.com/w/index.php/INES
        // ("Flags 6"/"Flags 7"), the mapper number's low/high nybbles are
        // bits 7-4 of header bytes 6/7 - a full 4-bit nybble each
        // (`(b & 0xF0) >> 4`). LMapper()/HMapper() (INES.cpp) instead mask
        // with 0xE0 (bits 7-5, only 3 bits) shifted by 5 - an easy off-by-one
        // to make when hand-rolling a bitmask from the spec, and one that
        // silently drops bit 4 of each nybble. It has no observable effect
        // today, since nothing in this port ever reads Lmapper/Hmapper (see
        // NES_ROM's LEARNING NOTE) - but it's worth knowing about before
        // building real mapper support on top of these two fields.
        /// Lower nybble of mapper number.
        static int Lmapper;
        /// Upper nybble of mapper number.
        static int Hmapper;

        /// Cartridge is for the Vs. UniSystem arcade hardware.
        static bool VSUnisystem;
        /// PlayChoice-10 (8KB of Hint Screen data stored after CHR data).
        static bool PlayChoice;
        /// If true, flags 8-15 are in NES 2.0 format.
        static bool NES2;

        /// TV system (0: NTSC; 2: PAL; 1/3: dual compatible).
        static TV TVsystem;
        /// PRG RAM ($6000-$7FFF) present.
        static bool present;
        /// Board has bus conflicts.
        static bool Boardconflicts;

        /// Parses the 16-byte iNES header in `b` and fills the static fields of this class.
        static void ReadeHeader(const std::vector<uint8_t>& b);

    private:
        static void ReadeSize(const std::vector<uint8_t>& b);
        static void Flags(const std::vector<uint8_t>& b);

        static void Flag6(uint8_t b);
        static void LMapper(uint8_t b);
        static void Trainer(uint8_t b);
        static void BatteryBacked(uint8_t b);
        static void VRAMMirroring(uint8_t b);

        static void Flag7(uint8_t b);
        static void HMapper(uint8_t b);
        static void NES2Format(uint8_t b);
        static void PlayChoice_10(uint8_t b);
        static void VS_Unisystem(uint8_t b);

        static void Flag9(uint8_t b);

        static void Flag10(uint8_t b);
        static void BoardConflicts(uint8_t b);
        static void Present(uint8_t b);
        static void InitTVsystem(uint8_t b);
    };
}
