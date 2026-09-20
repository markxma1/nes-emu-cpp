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
    /// @brief Mapper 0 (NROM) - no registers at all: real NROM boards don't
    /// even decode CPU writes to $8000-$FFFF, so this class's
    /// WriteRegister() just re-paints whatever byte was already there
    /// (undoing the write, the same as what real, undecoded hardware would
    /// read back). http://wiki.nesdev.com/w/index.php/NROM
    ///
    /// 16KB PRG-ROM is mirrored into both $8000-$BFFF and $C000-$FFFF; 32KB
    /// fills the whole range directly. Up to 8KB CHR-ROM (or CHR-RAM) - see
    /// Mapper::HasChrRam(). This is exactly the logic NES_ROM.cpp's
    /// LoadPRGROM()/LoadPatternTable()/PRGROMMirror() used to hard-code for
    /// *every* ROM regardless of mapper (see the LEARNING NOTE this
    /// replaced) - moved here unchanged, now used only when the header
    /// actually declares mapper 0.
    class Mapper_NROM : public Mapper
    {
    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;
    };
}
