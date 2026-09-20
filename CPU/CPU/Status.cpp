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
#include "Status.h"
#include "NES_Register.h"

namespace NES
{
    // FIXED (was a preserved C# bug, now corrected - found via nestest.nes):
    // Negative only ever looks at bit 7, so it's truncation-agnostic
    // regardless of whether `number` is a plain 0-255 byte or (from
    // Math::ADC/SBC) a raw pre-truncation sum that can reach 9 bits - but
    // the C# original (CPU/CPU/Status.cs NZ) compared the whole untruncated
    // `number` to 0 for Zero, so e.g. 0x7F + 0x80 + 1 = 0x100 (truncates to
    // the correct A=0x00) incorrectly left Zero cleared, since 0x100 != 0
    // even though the real 8-bit result is zero. Fixed to truncate first.
    void Status::NZ(int number)
    {
        NES_Register::P.Negative((number & 0x80) != 0);
        NES_Register::P.Zero((number & 0xFF) == 0);
    }

    // FIXED (was a preserved C# bug, now corrected - found via nestest.nes):
    // per http://wiki.nesdev.com/w/index.php/Status_flags, ADC/SBC's
    // Overflow flag is *signed* overflow (both operands share a sign but
    // the result's sign differs), not "did the raw sum spill past 8 bits" -
    // that second thing is what Carry already means. The C# original
    // (CPU/CPU/Status.cs OC) computed Overflow with the same
    // `(number & ~0xFF) > 0` test as Carry, which happens to equal Carry's
    // value in every case rather than actual signed overflow (e.g. 0x7F +
    // 0x7F + 1 = 0xFF: no 9th bit set, so this always reported V=0, when
    // two positive operands producing a negative-looking result is exactly
    // the case V is supposed to flag). Math::ADC/SBC now compute the real
    // signed-overflow condition themselves (they have the two original
    // operands, which this function no longer does) and pass it in here.
    void Status::OC(int number, bool overflow, bool carry)
    {
        NES_Register::P.Overflow(overflow);
        NES_Register::P.Carry(carry);
    }

    void Status::NVZC(int number, bool overflow, bool carry)
    {
        OC(number, overflow, carry);
        NZ(number);
    }

    int Status::Carry()
    {
        return NES_Register::P.Carry() ? 1 : 0;
    }

    int Status::NotCarry()
    {
        return NES_Register::P.Carry() ? 0 : 1;
    }
}
