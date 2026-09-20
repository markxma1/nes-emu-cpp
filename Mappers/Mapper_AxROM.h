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
#include "Mapper.h"

namespace NES
{
    /// @brief Mapper 7 (AxROM): http://wiki.nesdev.com/w/index.php/AxROM
    ///
    /// Any CPU write to $8000-$FFFF is `xxxM xPPP`: bits 0-2 select a 32KB
    /// PRG-ROM bank for the *entire* CPU $8000-$FFFF window (no fixed
    /// half - unlike UxROM/MMC1/MMC3), and bit 4 selects which of the two
    /// physical 1KB nametable pages single-screen mirroring shows (see
    /// NES_PPU_Memory::RewireNameTableMirroring() - this is the register
    /// that made that function need to be safely re-callable at runtime in
    /// the first place). Always 8KB CHR-RAM, never bank-switched - see
    /// Mapper::HasChrRam().
    class Mapper_AxROM : public Mapper
    {
    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;
    };
}
