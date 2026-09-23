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
/// https://en.wikibooks.org/wiki/NES_Programming
#pragma once
#include "AddressSetup.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace NES
{
    /// @brief The NES's full 64KB CPU address space.
    ///
    /// Address Range (Hex)   Size    Notes (page size is 256 bytes)
    /// $0000-$00FF  256 B   Zero Page - fast Zero Page addressing modes
    /// $0100-$01FF  256 B   Stack memory
    /// $0200-$07FF  1536 B  RAM
    /// $0800-$1FFF  6144 B  Mirrors of $0000-$07FF (three times)
    /// $2000-$2007  8 B     PPU I/O registers
    /// $2008-$3FFF  Mirror of $2000-$2007 (multiple times)
    /// $4000-$401F  32 B    APU / I/O registers
    /// $4020-$5FFF  Expansion ROM (used by e.g. MMC5 to expand VRAM capabilities)
    /// $6000-$7FFF  SRAM - save RAM used to save data between play sessions
    /// $8000-$FFFF  32768 B PRG-ROM
    /// $FFFA-$FFFB  Address of the Non-Maskable Interrupt (NMI) handler
    /// $FFFC-$FFFD  Address of the power-on reset handler
    /// $FFFE-$FFFF  Address of the Break (BRK instruction) handler
    /// See http://wiki.nesdev.com/w/index.php/CPU_memory_map
    ///
    /// `Memory` holds one AddressSetup per address, shared (aliased) across
    /// mirrored ranges - by aliasing the same
    /// AddressSetup object into multiple slots, so a
    /// write through any mirror is visible through all of them.
    class NES_Memory
    {
    public:
        static std::vector<std::shared_ptr<AddressSetup>> Memory;
        static std::vector<std::shared_ptr<AddressSetup>> ZeroPage;
        static std::vector<std::shared_ptr<AddressSetup>> Stack;
        static std::vector<std::shared_ptr<AddressSetup>> RAM;
        static std::vector<std::shared_ptr<AddressSetup>> PPU;
        static std::vector<std::shared_ptr<AddressSetup>> APU;
        static std::vector<std::shared_ptr<AddressSetup>> Joystick;
        static std::vector<std::shared_ptr<AddressSetup>> IO;
        static std::vector<std::shared_ptr<AddressSetup>> EROM;
        static std::vector<std::shared_ptr<AddressSetup>> SRAM;
        static std::vector<std::shared_ptr<AddressSetup>> PRGROM;
        static std::vector<std::shared_ptr<AddressSetup>> NMI;
        static std::vector<std::shared_ptr<AddressSetup>> POR;
        static std::vector<std::shared_ptr<AddressSetup>> BRK;

        static std::vector<uint8_t> MemTest(const std::vector<std::shared_ptr<AddressSetup>>& list);

        NES_Memory();

        static void ResetBlocks();

    private:
        static void InitMemory();
        static void InitZeroPage();
        static void InitStack();
        static void InitRAM();
        static void InitPPU();
        static void InitAPU();
        static void InitJoystick();
        static void InitIO();
        static void InitEROM();
        static void InitSRAM();
        static void InitPRGROM();
        static void InitNMI();
        static void InitPOR();
        static void InitBRK();
    };
}
