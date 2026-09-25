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
#include "EnvFlag.h"
#include "NES_Memory.h"
#include <cstdlib>

namespace NES
{
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::Memory;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::ZeroPage;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::Stack;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::RAM;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::PPU;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::APU;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::Joystick;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::IO;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::EROM;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::SRAM;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::PRGROM;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::NMI;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::POR;
    std::vector<std::shared_ptr<AddressSetup>> NES_Memory::BRK;

    std::vector<uint8_t> NES_Memory::MemTest(const std::vector<std::shared_ptr<AddressSetup>>& list)
    {
        std::vector<uint8_t> result;
        result.reserve(list.size());
        for (const auto& a : list)
            result.push_back(a->value());
        return result;
    }

    NES_Memory::NES_Memory()
    {
        InitMemory();
        ResetBlocks();
    }

    void NES_Memory::ResetBlocks()
    {
        InitZeroPage();
        InitStack();
        InitRAM();
        InitPPU();
        InitAPU();
        InitJoystick();
        InitIO();
        InitEROM();
        InitSRAM();
        InitPRGROM();
        InitNMI();
        InitPOR();
        InitBRK();
    }

    void NES_Memory::InitIO()
    {
        for (int i = 0x4018; i <= 0x401F; i++)
        {
            IO.push_back(Memory[i]);
            Memory[i]->value(0xFF);
        }
    }

    void NES_Memory::InitJoystick()
    {
        for (int i = 0x4016; i <= 0x4017; i++)
        {
            Joystick.push_back(Memory[i]);
            Memory[i]->value(0xFF);
        }
    }

    void NES_Memory::InitAPU()
    {
        for (int i = 0x4000; i <= 0x4015; i++)
        {
            APU.push_back(Memory[i]);
            Memory[i]->value(0xFF);
        }
    }

    void NES_Memory::InitBRK()
    {
        for (int i = 0xFFFE; i <= 0xFFFF; i++)
            BRK.push_back(Memory[i]);
    }

    void NES_Memory::InitPOR()
    {
        for (int i = 0xFFFC; i <= 0xFFFD; i++)
            POR.push_back(Memory[i]);
    }

    void NES_Memory::InitNMI()
    {
        for (int i = 0xFFFA; i <= 0xFFFB; i++)
            NMI.push_back(Memory[i]);
    }

    void NES_Memory::InitPRGROM()
    {
        for (int i = 0x8000; i <= 0xFFFF; i++)
            PRGROM.push_back(Memory[i]);
    }

    void NES_Memory::InitSRAM()
    {
        for (int i = 0x6000; i <= 0x7FFF; i++)
        {
            SRAM.push_back(Memory[i]);
            Memory[i]->value(0);
        }
    }

    void NES_Memory::InitEROM()
    {
        for (int i = 0x4020; i <= 0x5FFF; i++)
        {
            EROM.push_back(Memory[i]);
            if (i < 0x5000)
                Memory[i]->value(0xFF);
            else
                Memory[i]->value(0);
        }
    }

    void NES_Memory::InitPPU()
    {
        // NOTE: mirrors only $2000-$2007 into $2008-$200F, not the full
        // $2008-$3FFF range - this is a known quirk
        // kept as-is.
        for (int i = 0x2000; i <= 0x2007; i++)
        {
            PPU.push_back(Memory[i]);
            Memory[i + 0x08] = Memory[i];
            Memory[i]->value(0);
        }
    }

    void NES_Memory::InitRAM()
    {
        for (int i = 0x0200; i <= 0x07FF; i++)
            RAM.push_back(Memory[i]);
    }

    void NES_Memory::InitStack()
    {
        for (int i = 0x0100; i <= 0x01FF; i++)
            Stack.push_back(Memory[i]);
    }

    void NES_Memory::InitZeroPage()
    {
        for (int i = 0x00; i <= 0x00FF; i++)
            ZeroPage.push_back(Memory[i]);
    }

    void NES_Memory::InitMemory()
    {
        Memory.clear();
        Memory.reserve(0x10000);
        for (int i = 0; i <= 0xFFFF; i++)
        {
            if (i >= 0x0800 && i <= 0x0FFF)
                Memory.push_back(Memory[i - 0x800]);
            else if (i >= 0x1000 && i <= 0x17FF)
                Memory.push_back(Memory[i - 0x1000]);
            else if (i >= 0x1800 && i <= 0x1FFF)
                Memory.push_back(Memory[i - 0x1800]);
            else if ((i & 4) == 0 || (i < 0x800 && NES_GETENV("NES_RAM_ZERO")))
                Memory.push_back(std::make_shared<AddressSetup>(static_cast<uint8_t>(0x00), i));
            else
                Memory.push_back(std::make_shared<AddressSetup>(static_cast<uint8_t>(0xFF), i));
        }
    }
}
