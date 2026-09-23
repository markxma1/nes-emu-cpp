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

namespace NES
{
    /// @brief Sets 6502 status flags after an operation.
    /// http://wiki.nesdev.com/w/index.php/Status_flags
    class Status
    {
    public:
        /// Sets Negative/Zero from the low byte of `number` (used after loads, INC/DEC, ...).
        static void NZ(int number);

        /// Sets Overflow and Carry from caller-computed conditions (used
        /// after ADC/SBC - see Math::ADC/SBC's NOTE for why neither can be
        /// derived from `number` alone with one shared formula: ADC's raw
        /// sum ranges 0..511 (carry = bit 8 set) but SBC's raw
        /// `A - B - borrow` ranges -256..255 (carry = "no borrow" = result
        /// >= 0), so each needs its own carry condition, not just its own
        /// overflow condition).
        static void OC(int number, bool overflow, bool carry);

        /// Sets Negative, Overflow, Zero and Carry from an arithmetic result.
        static void NVZC(int number, bool overflow, bool carry);

        static int Carry();
        static int NotCarry();
    };
}
