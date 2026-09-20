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
    /// @brief Mapper 3 (CNROM) - the CHR-bank-switching mirror image of
    /// UxROM: http://wiki.nesdev.com/w/index.php/CNROM
    ///
    /// PRG-ROM is fixed (16KB mirrored or 32KB direct, exactly like NROM -
    /// see Mapper_NROM - no PRG bank register at all). Any CPU write to
    /// $8000-$FFFF selects an 8KB CHR-ROM bank for the *entire* PPU pattern
    /// table ($0000-$1FFF), modulo however many 8KB CHR banks actually
    /// exist. Real CNROM boards only decode the low 2 bits (4 banks max)
    /// and have real "bus conflicts" (the CPU's write value gets AND'd with
    /// whatever byte the addressed PRG-ROM cell already holds, since the
    /// ROM chip drives the bus too) - both left unmodeled here as an
    /// accepted simplification, matching how thoroughly minor their
    /// real-world impact is (bus conflicts only matter for a handful of
    /// carts that were built assuming a specific ROM-image byte pattern at
    /// the write address).
    class Mapper_CNROM : public Mapper
    {
    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;
    };
}
