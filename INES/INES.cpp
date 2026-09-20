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
#include "INES.h"

namespace NES
{
    INES::Mirror INES::arrangement = INES::Mirror::vertical;
    int INES::PRGROMSize = 0;
    int INES::CHRROMSize = 0;
    int INES::PRGRAMSize = 0;
    std::string INES::title;
    bool INES::battery_backed = false;
    bool INES::trainer = false;
    int INES::Lmapper = 0;
    int INES::Hmapper = 0;
    bool INES::VSUnisystem = false;
    bool INES::PlayChoice = false;
    bool INES::NES2 = false;
    INES::TV INES::TVsystem = INES::TV::NTS;
    bool INES::present = false;
    bool INES::Boardconflicts = false;

    void INES::ReadeHeader(const std::vector<uint8_t>& b)
    {
        ReadeSize(b);
        Flags(b);
    }

    void INES::ReadeSize(const std::vector<uint8_t>& b)
    {
        PRGROMSize = 16384 * b[4];
        CHRROMSize = 8192 * b[5];
        PRGRAMSize = 8192 * b[8];
    }

    void INES::Flags(const std::vector<uint8_t>& b)
    {
        Flag6(b[6]);
        Flag7(b[7]);
        Flag9(b[9]);
        Flag10(b[10]);
    }

    // In the iNES format, cartridge boards are divided into "mappers" (0-255)
    // based on similar board hardware/behavior; see http://wiki.nesdev.com/w/index.php/INES
    void INES::Flag6(uint8_t b)
    {
        VRAMMirroring(b);
        BatteryBacked(b);
        Trainer(b);
        LMapper(b);
    }

    // FIXED (was inert until now - see the LEARNING NOTE in INES.h - now
    // load-bearing for real mapper dispatch, see the Mappers/ folder):
    // masked with 0xE0 (bits 7-5, 3 bits) shifted by 5 instead of the full
    // 4-bit nybble 0xF0 shifted by 4 (http://wiki.nesdev.com/w/index.php/INES,
    // "Flags 6" bits 7-4) - silently dropped bit 4, e.g. mapper 1 (MMC1)
    // read back as mapper 0 (NROM) whenever bit 4 was the only set bit.
    /// (7-4): Lower nybble of mapper number.
    void INES::LMapper(uint8_t b) { Lmapper = (b & 0xF0) >> 4; }

    /// (2): 1 = 512-byte trainer at $7000-$71FF (stored before PRG data).
    void INES::Trainer(uint8_t b) { if ((b & 0x4) > 0) trainer = true; }

    /// (1): 1 = cartridge contains battery-backed PRG RAM ($6000-7FFF) or other persistent memory.
    void INES::BatteryBacked(uint8_t b) { if ((b & 0x2) > 0) battery_backed = true; }

    // FIXED (was a preserved bug, now corrected - a second, independent
    // occurrence of the exact vertical/horizontal mix-up already fixed in
    // NES_PPU_Memory::RewireNameTableMirroring(), found live via real
    // gameplay: fixing that one silently broke Galaga (an NROM game, whose
    // mirroring only ever comes from this header bit, never from a
    // runtime-switchable mapper register) even though it fixed Chip and
    // Dale (an MMC1 game, whose mirroring comes from
    // Mapper_MMC1::ApplyMirroring() instead - a separate, already-correct
    // code path). The two bugs had been silently cancelling each other out
    // for every header-mirrored ROM the whole time; fixing only one of them
    // exposed the other. Verified against
    // https://www.nesdev.org/wiki/INES ("Flags 6", bit 0): "0: vertical
    // arrangement ('horizontally mirrored') ... 1: horizontal arrangement
    // ('vertically mirrored')" - i.e. bit 0 = 0 means *horizontal*
    // mirroring (INES::Mirror::horisontal), bit 0 = 1 means *vertical*
    // mirroring (INES::Mirror::vertical). This had the two cases swapped.
    /// 76543210: (3+0) 0xx0=vertical arrangement/horizontal mirroring,
    /// 0xx1=horizontal arrangement/vertical mirroring, 1xxx=four-screen VRAM.
    void INES::VRAMMirroring(uint8_t b)
    {
        switch (b & 0x9)
        {
            case 0: arrangement = Mirror::horisontal; break;
            case 1: arrangement = Mirror::vertical; break;
            default: arrangement = Mirror::four_screen; break;
        }
    }

    void INES::Flag7(uint8_t b)
    {
        VS_Unisystem(b);
        PlayChoice_10(b);
        NES2Format(b);
        HMapper(b);
    }

    // FIXED: same bug as LMapper() above, same fix.
    /// Upper nybble of mapper number.
    void INES::HMapper(uint8_t b) { Hmapper = (b & 0xF0) >> 4; }

    /// If equal to 2, flags 8-15 are in NES 2.0 format.
    void INES::NES2Format(uint8_t b) { if ((b & 0xC) == 0x4) NES2 = true; }

    /// PlayChoice-10 (8KB of Hint Screen data stored after CHR data).
    void INES::PlayChoice_10(uint8_t b) { if ((b & 0x2) > 0) PlayChoice = true; }

    void INES::VS_Unisystem(uint8_t b) { if ((b & 0x1) > 0) VSUnisystem = true; }

    /// Very few emulators honor this bit; virtually no ROMs use it.
    void INES::Flag9(uint8_t b)
    {
        if ((b & 0x1) > 0)
            TVsystem = TV::PAL;
        switch (b & 0x1)
        {
            case 0: TVsystem = TV::NTS; break;
            case 1: TVsystem = TV::PAL; break;
        }
    }

    void INES::Flag10(uint8_t b)
    {
        InitTVsystem(b);
        Present(b);
        BoardConflicts(b);
    }

    /// 0: no bus conflicts; 1: board has bus conflicts.
    void INES::BoardConflicts(uint8_t b) { if ((b & 0xC) > 0) Boardconflicts = true; }

    /// PRG RAM ($6000-$7FFF): 0 present, 1 not present.
    void INES::Present(uint8_t b) { if ((b & 0xA) > 0) present = true; }

    /// TV system: 0 NTSC, 2 PAL, else dual-compatible.
    void INES::InitTVsystem(uint8_t b)
    {
        switch (b & 0x3)
        {
            case 0: TVsystem = TV::NTS; break;
            case 2: TVsystem = TV::PAL; break;
            default: TVsystem = TV::DUAL; break;
        }
    }
}
