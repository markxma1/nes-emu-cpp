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
#include "Mapper_NROM.h"
#include <stdexcept>

namespace NES
{
    void Mapper_NROM::OnInstall()
    {
        if (prg.size() != 16384 && prg.size() != 32768)
        {
            throw std::runtime_error("Mapper_NROM: ROM declares mapper 0 (NROM) but has " +
                std::to_string(prg.size()) + " bytes of PRG-ROM - NROM only supports 16KB or 32KB "
                "(header's mapper number is likely wrong for this file)");
        }
        // WritePrgWindow's wraparound (romByteOffset % prg.size()) is exactly
        // NROM's 16KB mirroring for free: a 16KB prg written across the full
        // 32KB $8000-$FFFF window naturally repeats once.
        WritePrgWindow(0x8000, 0, 0x8000);
        WriteChrWindow(0x0000, 0, 0x2000);
    }

    void Mapper_NROM::WriteRegister(uint16_t /*address*/, uint8_t /*value*/)
    {
        // No real register here (see the class comment) - genuinely a
        // no-op. This used to manually repaint the written address with its
        // real PRG-ROM byte (real, undecoded PRG-ROM hardware ignores CPU
        // writes entirely - see Mapper::Install()'s FIXED note for the
        // nesdev citation), but Mapper::Install()'s write hook now does
        // exactly that generically for every mapper, so doing it again here
        // would just be redundant.
    }
}
