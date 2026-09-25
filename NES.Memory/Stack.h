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
#include <cstdint>

namespace NES
{
    /// @brief The 6502 hardware stack at $0100-$01FF.
    /// http://wiki.nesdev.com/w/index.php/Stack
    class Stack
    {
    public:
        /// Pushes the P register with the B and U bits forced to `b` and `u` (PHP/BRK push B=1, IRQ/NMI push B=0).
        static void ProcessorstatusToStack(bool b, bool u);
        /// Pops P from the stack (PLP/RTI); bits 4 and 5 are ignored, as on real hardware.
        static void StackToProcessorstatus();
        /// Pushes PC minus one, high byte first (as JSR does).
        static void PcToStack();
        /// Pops PC from the stack. `incrementAfter` matches RTS's `+1` (it
        /// compensates JSR's `return_address - 1` push, see PcToStack()) -
        /// RTI must pass false, since an interrupt push has no such offset.
        /// See Stack.cpp for the bug this parameter fixes.
        static void StackToPc(bool incrementAfter = true);
        /// Writes `value` at the current stack pointer, then decrements S.
        static void PushToStack(uint8_t value);
        /// Increments S, then returns the byte stored there.
        static uint8_t PopFromStack();
    };
}
