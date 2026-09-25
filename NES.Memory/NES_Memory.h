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
        /// All 65536 cells of the CPU address space, indexed by address.
        static std::vector<std::shared_ptr<AddressSetup>> Memory;
        /// Cells $0000-$00FF.
        static std::vector<std::shared_ptr<AddressSetup>> ZeroPage;
        /// Cells $0100-$01FF.
        static std::vector<std::shared_ptr<AddressSetup>> Stack;
        /// Cells $0200-$07FF.
        static std::vector<std::shared_ptr<AddressSetup>> RAM;
        /// Cells $2000-$2007 (PPU registers).
        static std::vector<std::shared_ptr<AddressSetup>> PPU;
        /// Cells $4000-$4015 (APU registers).
        static std::vector<std::shared_ptr<AddressSetup>> APU;
        /// Cells $4016-$4017 (controller ports).
        static std::vector<std::shared_ptr<AddressSetup>> Joystick;
        /// Cells $4018-$401F (normally disabled APU/IO test registers).
        static std::vector<std::shared_ptr<AddressSetup>> IO;
        /// Cells $4020-$5FFF (expansion area).
        static std::vector<std::shared_ptr<AddressSetup>> EROM;
        /// Cells $6000-$7FFF (battery/save RAM).
        static std::vector<std::shared_ptr<AddressSetup>> SRAM;
        /// Cells $8000-$FFFF (cartridge program ROM).
        static std::vector<std::shared_ptr<AddressSetup>> PRGROM;
        /// Cells $FFFA-$FFFB (NMI vector).
        static std::vector<std::shared_ptr<AddressSetup>> NMI;
        /// Cells $FFFC-$FFFD (power-on/reset vector).
        static std::vector<std::shared_ptr<AddressSetup>> POR;
        /// Cells $FFFE-$FFFF (IRQ/BRK vector).
        static std::vector<std::shared_ptr<AddressSetup>> BRK;

        /// Returns the raw values (without running hooks) of the given cells, for tests and debugging.
        static std::vector<uint8_t> MemTest(const std::vector<std::shared_ptr<AddressSetup>>& list);

        NES_Memory();

        /// Fills the region views (ZeroPage, Stack, RAM, ..., BRK) with the cells of `Memory` they alias.
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
