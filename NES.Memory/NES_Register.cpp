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
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"

namespace NES
{
    std::string PFlags::ToString() const
    {
        return std::to_string(P) + ": "
            + (Negative() ? "N" : "")
            + (Overflow() ? "V" : "")
            + (U() ? "U" : "")
            + (B() ? "B" : "")
            + (Decimal() ? "D" : "")
            + (Interrupt() ? "I" : "")
            + (Zero() ? "Z" : "")
            + (Carry() ? "C" : "");
    }

    uint8_t NES_Register::A = 0;
    uint8_t NES_Register::X = 0;
    uint8_t NES_Register::Y = 0;
    PFlags NES_Register::P = PFlags();
    uint8_t NES_Register::S = 0xFD;
    uint16_t NES_Register::PC = 0;

    void NES_Register::RessetPointer()
    {
        auto* lo = static_cast<AddressSetup*>(NES_Memory::POR[0].get());
        auto* hi = static_cast<AddressSetup*>(NES_Memory::POR[1].get());
        PC = static_cast<uint16_t>(lo->Value() | (hi->Value() << 8));
    }
}
