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
#include "Mapper_AxROM.h"
#include "INES.h"
#include "NES_PPU_Memory.h"

namespace NES
{
    void Mapper_AxROM::OnInstall()
    {
        WritePrgWindow(0x8000, 0, 0x8000); // bank 0 by default
        // AxROM boards are always CHR-RAM, so WriteChrWindow() would be a
        // no-op anyway - nothing to do for CHR here.
    }

    void Mapper_AxROM::WriteRegister(uint16_t /*address*/, uint8_t value)
    {
        size_t numBanks = prg.size() / 32768;
        size_t offset = (static_cast<size_t>(value & 0x07) % numBanks) * 32768;
        WritePrgWindow(0x8000, offset, 0x8000);

        INES::arrangement = (value & 0x10) ? INES::Mirror::single_screen_b : INES::Mirror::single_screen_a;
        NES_PPU_Memory::RewireNameTableMirroring();
    }
}
