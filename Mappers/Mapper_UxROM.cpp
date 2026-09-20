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
#include "Mapper_UxROM.h"

namespace NES
{
    void Mapper_UxROM::OnInstall()
    {
        SwitchLowBank(0);
        // $C000-$FFFF: fixed to the last 16KB bank, for the whole session.
        size_t lastBankOffset = ((prg.size() / 16384) - 1) * 16384;
        WritePrgWindow(0xC000, lastBankOffset, 16384);
        WriteChrWindow(0x0000, 0, 0x2000); // no-op on the usual CHR-RAM boards
    }

    void Mapper_UxROM::WriteRegister(uint16_t /*address*/, uint8_t value)
    {
        SwitchLowBank(value);
    }

    void Mapper_UxROM::SwitchLowBank(uint8_t bank)
    {
        size_t numBanks = prg.size() / 16384;
        size_t offset = (static_cast<size_t>(bank) % numBanks) * 16384;
        WritePrgWindow(0x8000, offset, 16384);
    }
}
