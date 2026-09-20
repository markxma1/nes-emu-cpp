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
    /// @brief Mapper 1 (MMC1): http://wiki.nesdev.com/w/index.php/MMC1
    ///
    /// MMC1's registers sit behind a *serial* port at $8000-$FFFF, not a
    /// normal parallel one - real hardware only has one CPU data-bus pin
    /// wired to the mapper's shift register, to save pins on the chip.
    /// Loading one of its 4 actual registers (control, CHR bank 0, CHR
    /// bank 1, PRG bank) takes 5 separate CPU writes, each contributing one
    /// bit (LSB first); which of the 4 internal registers receives the
    /// fully-shifted-in 5-bit value is decided by address bits 13-14 of
    /// the *fifth* write (i.e. which $2000-sized quarter of $8000-$FFFF it
    /// landed in). Writing a value with bit 7 set, at any point, resets the
    /// shift register - used by game code to guarantee a clean starting
    /// state regardless of what a previous, possibly-interrupted write
    /// sequence left behind.
    ///
    /// Unlike every other mapper here, MMC1's control register also picks
    /// the nametable mirroring mode *and* one of two different PRG bank
    /// layouts *and* one of two CHR bank layouts - see WriteControl()/
    /// ApplyBanks() in the .cpp.
    ///
    /// Not modeled: the real chip's "consecutive-cycle writes are ignored"
    /// quirk (relevant only to read-modify-write instructions like INC on a
    /// memory operand, which write twice in a row - this port has no
    /// per-cycle write timing to detect that with) and PRG-RAM (bit 4 of
    /// the PRG bank register, MMC1B revision) - neither affects whether a
    /// game's graphics/bank-switching itself works correctly.
    class Mapper_MMC1 : public Mapper
    {
    public:
        // NEW, no C# equivalent - see Mapper::SerializeState()'s own
        // comment. Captures the 4 registers a subsequent WriteRegister()
        // would otherwise recompute banks from using stale (power-on
        // default) values.
        void SerializeState(std::vector<uint8_t>& out) const override;
        void DeserializeState(const uint8_t*& in, const uint8_t* end) override;

    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;

    private:
        uint8_t shiftRegister = 0;
        int writeCount = 0;

        uint8_t control = 0x0C; // power-on default: PRG mode 3 (fix last bank at $C000)
        uint8_t chrBank0 = 0;
        uint8_t chrBank1 = 0;
        uint8_t prgBank = 0;

        void WriteControl(uint8_t value);
        void WriteChrBank0(uint8_t value);
        void WriteChrBank1(uint8_t value);
        void WritePrgBank(uint8_t value);

        void ApplyMirroring();
        void ApplyPrgBanks();
        void ApplyChrBanks();
    };
}
