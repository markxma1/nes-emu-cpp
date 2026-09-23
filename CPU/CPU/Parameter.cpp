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
#include "Parameter.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"

namespace NES
{
    bool Parameter::pageCrossed = false;

    uint16_t Parameter::ax(uint16_t ax)
    {
        uint16_t result = static_cast<uint16_t>(ax + NES_Register::X);
        if ((ax & 0xFF00) != (result & 0xFF00))
            pageCrossed = true;
        return result;
    }

    uint16_t Parameter::ay(uint16_t ay)
    {
        uint16_t result = static_cast<uint16_t>(ay + NES_Register::Y);
        if ((ay & 0xFF00) != (result & 0xFF00))
            pageCrossed = true;
        return result;
    }

    uint16_t Parameter::zp(uint8_t zp)
    {
        return zp;
    }

    // FIXED (found via nestest.nes): per
    // http://wiki.nesdev.com/w/index.php/Addressing_modes ("Indexed
    // Indirect"), (zp,X) reads its 16-bit target pointer from *two zero-page
    // bytes*, and "The pointer must be somewhere in the zero page, and X is
    // used to add and wraparound" - both the zp+X sum *and* the high-byte
    // read the byte after it wrap within 0x00-0xFF, since both pointer bytes
    // are fetched via zero-page addressing on real hardware (there's no
    // 16-bit increment involved). This previously added zp+X and read
    // `address+1` unwrapped - so e.g. zp=$FF,X=$00 read the pointer's high
    // byte from real address $0100 (deep in the stack page) instead of
    // wrapping back to zero page's $00, producing a completely wrong
    // effective address whenever the pointer straddled the $FF/$00 boundary.
    uint16_t Parameter::zpx1(int zpx)
    {
        uint8_t address = static_cast<uint8_t>(zpx + NES_Register::X);
        return static_cast<uint16_t>(
            NES_Memory::Memory[address]->Value() | (NES_Memory::Memory[static_cast<uint8_t>(address + 1)]->Value() << 8));
    }

    // FIXED (found via nestest.nes): per
    // http://wiki.nesdev.com/w/index.php/Addressing_modes ("Zero Page
    // Indexed"), "The address calculation wraps around if the sum of the
    // base address and the register exceed $FF" - zp,X always stays within
    // the zero page. This previously returned the unwrapped 16-bit sum, so
    // e.g. zp=$FF,X=$8A read real address $0189 (deep in RAM) instead of
    // wrapping back to $89.
    uint16_t Parameter::zpx2(uint8_t zpx)
    {
        return static_cast<uint8_t>(zpx + NES_Register::X);
    }

    // FIXED (found via nestest.nes, same bug as zpx1 above, same fix):
    // (zp),Y reads its 16-bit base
    // pointer from zero-page bytes `zp` and `zp+1`, wrapping the latter
    // within 0x00-0xFF (only the *final* `+ Y` is a real, unwrapped 16-bit
    // addition - that part was already correct here).
    uint16_t Parameter::zpy1(uint8_t zpy)
    {
        int temp = NES_Memory::Memory[zpy]->Value() | (NES_Memory::Memory[static_cast<uint8_t>(zpy + 1)]->Value() << 8);
        uint16_t result = static_cast<uint16_t>(temp + NES_Register::Y);
        if ((temp & 0xFF00) != (result & 0xFF00))
            pageCrossed = true;
        return result;
    }

    // FIXED (found via nestest.nes, same bug as zpx2 above, same fix):
    // zp,Y also always stays within the zero page.
    uint16_t Parameter::zpy2(uint8_t zpy)
    {
        return static_cast<uint8_t>(zpy + NES_Register::Y);
    }

    // FIXED (found via nestest.nes; unlike this session's other fixes, this
    // one *adds* a hardware quirk rather than removing a mistake): per
    // http://wiki.nesdev.com/w/index.php/Errata ("JMP indirect ... does not
    // correctly fetch the target address if the indirect vector falls on a
    // page boundary ... the low byte ... is fetched from ... $xxFF as
    // expected, but the high byte is fetched from $xx00 instead of
    // $(xx+1)00"), real 6502 hardware has a genuine erratum in JMP's
    // indirect addressing mode: when the pointer's low byte is $FF, the
    // high-byte fetch wraps back to the *start* of the same page instead of
    // advancing into the next one. nestest.nes explicitly exercises this
    // (`JMP ($02FF)`, expecting the high byte from $0200, not $0300) since
    // real NES software occasionally has to work around - or deliberately
    // exploits - this exact bug, so an accurate emulator has to reproduce
    // it, not "fix" it. This previously always used the hardware-non-buggy
    // `a + 1`. Fixed to wrap within the page when `a`'s low byte is $FF,
    // matching real hardware.
    uint16_t Parameter::MemoryValueToAdress(uint16_t a)
    {
        uint16_t hi = ((a & 0xFF) == 0xFF) ? static_cast<uint16_t>(a & 0xFF00) : static_cast<uint16_t>(a + 1);
        return static_cast<uint16_t>(
            NES_Memory::Memory[a]->Value() | (NES_Memory::Memory[hi]->Value() << 8));
    }
}
