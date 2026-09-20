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
#include "Mapper_CNROM.h"

namespace NES
{
    void Mapper_CNROM::OnInstall()
    {
        WritePrgWindow(0x8000, 0, 0x8000); // fixed for the whole session - wraps for 16KB PRG, same as NROM
        WriteChrWindow(0x0000, 0, 0x2000); // bank 0 by default
    }

    void Mapper_CNROM::WriteRegister(uint16_t /*address*/, uint8_t value)
    {
        size_t numBanks = chr.size() / 8192;
        if (numBanks == 0)
            return; // CHR-RAM board using mapper 3 for some other reason - nothing to switch
        size_t offset = (static_cast<size_t>(value) % numBanks) * 8192;
        WriteChrWindow(0x0000, offset, 0x2000);
    }
}
