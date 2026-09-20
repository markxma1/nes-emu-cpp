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
#include "Stack.h"
#include "NES_Memory.h"
#include "NES_Register.h"

namespace NES
{
    void Stack::ProcessorstatusToStack(bool b, bool u)
    {
        PFlags PStack = NES_Register::P;
        PStack.B(b);
        PStack.U(u);
        PushToStack(PStack.P);
    }

    // FIXED (was a preserved C# bug, now corrected - found via nestest.nes):
    // per http://wiki.nesdev.com/w/index.php/Status_flags, bits 4 (B) and 5
    // (unused) "do not represent a register that can hold a value" - the
    // real 6502 has no physical flip-flop for either, so PLP/RTI "ignored
    // when pulling flags from the stack; there are no corresponding
    // registers for them in the CPU". The C# original (NES.Memory/Stack.cs
    // StackToProcessorstatus) copied the popped byte's bits 4/5 straight
    // into P.P instead, so e.g. `PHA; PLP` (pushing an arbitrary value, then
    // popping it as if it were flags - exactly what nestest.nes's PLP test
    // does) would leave a phantom B flag set depending on that value's bit
    // 4, something no real 6502 program could ever observe. Fixed by
    // discarding the popped byte's bits 4/5 and using the conventional
    // always-0 (B)/always-1 (U) values instead, matching nestest.log's
    // (captured from Nintendulator, a known-correct reference) expected
    // flags after this exact sequence.
    void Stack::StackToProcessorstatus()
    {
        uint8_t popped = PopFromStack();
        NES_Register::P.P = static_cast<uint8_t>((popped & 0xCF) | 0x20);
    }

    void Stack::PcToStack()
    {
        NES_Register::PC--;
        PushToStack(static_cast<uint8_t>(NES_Register::PC >> 8));
        PushToStack(static_cast<uint8_t>(NES_Register::PC));
    }

    // FIXED (was a preserved C# bug, now corrected - found via nestest.nes):
    // RTS_60 and RTI_40 both called this unconditionally applying the `+1`
    // that only RTS needs. JSR pushes `return_address - 1` (see
    // PcToStack()'s own `PC--`), so RTS must add 1 back; a BRK/IRQ/NMI push
    // (see Interrupt::ReplacePC/PcToStack, same push path) is already the
    // exact address execution should resume at, so RTI incrementing it too
    // skips one byte of whatever instruction follows - silently corrupting
    // execution after every single interrupt return. http://wiki.nesdev.com/
    // w/index.php/RTI, http://wiki.nesdev.com/w/index.php/RTS.
    void Stack::StackToPc(bool incrementAfter)
    {
        uint8_t lo = PopFromStack();
        uint8_t hi = PopFromStack();
        NES_Register::PC = static_cast<uint16_t>(lo | (hi << 8));
        if (incrementAfter)
            NES_Register::PC++;
    }

    void Stack::PushToStack(uint8_t value)
    {
        NES_Memory::Stack[NES_Register::S--]->Value(value);
    }

    uint8_t Stack::PopFromStack()
    {
        return NES_Memory::Stack[++NES_Register::S]->Value();
    }
}
