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
    /// @brief The 6502 hardware stack at $0100-$01FF. Port of NES.Memory/Stack.cs.
    /// http://wiki.nesdev.com/w/index.php/Stack
    class Stack
    {
    public:
        static void ProcessorstatusToStack(bool b, bool u);
        static void StackToProcessorstatus();
        static void PcToStack();
        /// Pops PC from the stack. `incrementAfter` matches RTS's `+1` (it
        /// compensates JSR's `return_address - 1` push, see PcToStack()) -
        /// RTI must pass false, since an interrupt push has no such offset.
        /// See Stack.cpp for the bug this parameter fixes.
        static void StackToPc(bool incrementAfter = true);
        static void PushToStack(uint8_t value);
        static uint8_t PopFromStack();
    };
}
