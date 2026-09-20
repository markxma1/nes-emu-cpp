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
#include "NES_APU_Register.h"
#include "NES_APU.h"
#include "NES_Memory.h"
#include <cstdint>

namespace NES
{
    NES_APU_Register::NES_APU_Register()
    {
        for (uint16_t address = 0x4000; address <= 0x4013; ++address)
        {
            NES_Memory::Memory[address]->AfterSet(
                [address](uint8_t value) { NES_APU::WriteRegister(address, value); });
        }

        // $4015: write = channel enable flags, read = channel-active/IRQ
        // status (two different registers sharing one address, like $2002's
        // relationship to $2005/$2006 on the PPU side).
        NES_Memory::Memory[0x4015]->AfterSet([](uint8_t value) { NES_APU::WriteRegister(0x4015, value); });
        NES_Memory::Memory[0x4015]->BeforGet([]() { NES_Memory::Memory[0x4015]->value(NES_APU::ReadStatus()); });

        // $4017 is write-only for the frame counter on real hardware; a read
        // there is the (unimplemented in this port - see NES_GamePad.h)
        // Player 2 controller port, so only a write hook is needed here.
        NES_Memory::Memory[0x4017]->AfterSet([](uint8_t value) { NES_APU::WriteRegister(0x4017, value); });
    }
}
