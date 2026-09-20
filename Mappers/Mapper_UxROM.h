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
#pragma once
#include "Mapper.h"

namespace NES
{
    /// @brief Mapper 2 (UxROM) - the simplest PRG bank-switching board:
    /// http://wiki.nesdev.com/w/index.php/UxROM
    ///
    /// Any CPU write to $8000-$FFFF selects a 16KB PRG-ROM bank at
    /// $8000-$BFFF; $C000-$FFFF is permanently fixed to the *last* 16KB
    /// bank (so the reset/IRQ/NMI vectors, which live at the very end of
    /// PRG-ROM, are always reachable regardless of which bank is switched
    /// in). Real boards only decode 3-4 of the written byte's bits, but
    /// (matching common emulator practice, since extra high bits are always
    /// 0 on a real cartridge with few enough banks to need them) this just
    /// uses the whole byte, modulo however many 16KB banks actually exist
    /// (see Mapper::WritePrgWindow()). Almost always CHR-RAM, never
    /// bank-switched - see Mapper::HasChrRam().
    class Mapper_UxROM : public Mapper
    {
    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;

    private:
        void SwitchLowBank(uint8_t bank);
    };
}
