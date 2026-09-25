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
#include "Math.h"
#include "Status.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"

namespace NES
{
    // NOTE: the signed-overflow formulas below are the standard 6502 ones -
    // ADC: `~(A^B) & (A^result) & 0x80` (overflow iff A and B share a sign
    // but the result doesn't); SBC: same idea via `A - B == A + (~B) + C`,
    // which drops the leading `~` to `(A^B) & (A^result) & 0x80`. See
    // Status::OC's NOTE for why these moved here from Status - they need
    // the original operands, not just the arithmetic result.
    uint8_t Math::ADC(int A, int B)
    {
        int temp = A + B + Status::Carry();
        bool overflow = ((~(A ^ B)) & (A ^ temp) & 0x80) != 0;
        Status::NVZC(temp, overflow, temp > 0xFF);
        return static_cast<uint8_t>(temp);
    }

    // NOTE: SBC's raw `A - B - borrow` ranges -256..255 (unlike ADC's
    // 0..511), so Carry ("no borrow occurred") is `temp >= 0` here, not the
    // "9th bit set" check ADC uses - see Status::OC's NOTE. This part was
    // also wrong until nestest.nes caught it (`(number & 0x100)` on a
    // negative `temp` doesn't mean what it means for ADC's always-non-
    // negative sums).
    uint8_t Math::SBC(int A, int B)
    {
        int temp = A - B - Status::NotCarry();
        bool overflow = ((A ^ B) & (A ^ temp) & 0x80) != 0;
        Status::NVZC(temp, overflow, temp >= 0);
        return static_cast<uint8_t>(temp);
    }

    void Math::ASL(uint16_t a)
    {
        auto temp = NES_Memory::Memory[a];
        temp->Value(ASL(temp->Value()));
        Status::NZ(temp->Value());
    }

    void Math::ASL()
    {
        NES_Register::A = ASL(NES_Register::A);
        Status::NZ(NES_Register::A);
    }

    uint8_t Math::ASL(uint8_t value)
    {
        getASLCarry(value);
        return static_cast<uint8_t>(value << 1);
    }

    void Math::getASLCarry(uint8_t value)
    {
        NES_Register::P.Carry((value & 0x80) > 0);
    }

    void Math::LSR(uint16_t a)
    {
        auto temp = NES_Memory::Memory[a];
        temp->Value(LSR(temp->Value()));
        Status::NZ(temp->Value());
    }

    void Math::LSR()
    {
        NES_Register::A = LSR(NES_Register::A);
        Status::NZ(NES_Register::A);
    }

    void Math::getLSRCarry(uint8_t value)
    {
        NES_Register::P.Carry((value & 0x1) > 0);
    }

    uint8_t Math::LSR(uint8_t value)
    {
        getLSRCarry(value);
        return static_cast<uint8_t>(value >> 1);
    }

    // FIXED (found via nestest.nes): per
    // http://wiki.nesdev.com/w/index.php/Instruction_reference#ROL, ROL
    // shifts left through Carry: the *old* Carry (from before this
    // instruction) becomes the new bit 0, and the old bit 7 becomes the new
    // Carry. This previously read `Status::Carry()` *after* calling ASL() -
    // but ASL() itself already overwrites Carry with the outgoing bit 7 (see
    // getASLCarry()) as part of computing the shift, so by the time ROL read
    // it back, the old incoming Carry it needed was already gone, replaced
    // by the very carry-out value it was about to combine with. Fixed by
    // capturing the incoming Carry before the shift.
    void Math::ROL(uint16_t a)
    {
        auto temp = NES_Memory::Memory[a];
        int carryIn = Status::Carry();
        temp->Value(ASL(temp->Value()));
        temp->Value(static_cast<uint8_t>(temp->Value() | carryIn));
        Status::NZ(temp->Value());
    }

    void Math::ROL()
    {
        int carryIn = Status::Carry();
        NES_Register::A = ASL(NES_Register::A);
        NES_Register::A = static_cast<uint8_t>(NES_Register::A | carryIn);
        Status::NZ(NES_Register::A);
    }

    // FIXED (found via nestest.nes): ROR is ROL's mirror (see ROL's NOTE
    // above) - old Carry becomes the new bit 7, old bit 0 becomes the new
    // Carry - so it has the same read-carry-after-the-shift-already-
    // clobbered-it timing bug as ROL *plus* a second, independent bug: the
    // (already-wrong) carry was shifted left by 8 instead of 7 before
    // OR-ing it in, so it landed outside the byte and was discarded by
    // truncation to 8 bits - ROR never rotated anything into bit 7 at all,
    // it was just an LSR. Fixed by capturing the incoming Carry before the
    // shift and placing it at bit 7 (`<< 7`).
    void Math::ROR(uint16_t a)
    {
        auto temp = NES_Memory::Memory[a];
        int carryIn = Status::Carry();
        temp->Value(LSR(temp->Value()));
        temp->Value(static_cast<uint8_t>(temp->Value() | (carryIn << 7)));
        Status::NZ(temp->Value());
    }

    void Math::ROR()
    {
        int carryIn = Status::Carry();
        NES_Register::A = LSR(NES_Register::A);
        NES_Register::A = static_cast<uint8_t>(NES_Register::A | (carryIn << 7));
        Status::NZ(NES_Register::A);
    }

    void Math::AND(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A & value);
        Status::NZ(NES_Register::A);
    }

    void Math::ORA(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A | value);
        Status::NZ(NES_Register::A);
    }

    void Math::EOR(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A ^ value);
        Status::NZ(NES_Register::A);
    }

    // FIXED (found via nestest.nes): per
    // http://wiki.nesdev.com/w/index.php/Status_flags, CMP/CPX/CPY
    // compute `register - value` and set Negative/Zero from that result
    // like any other subtraction, with Carry as "no borrow" (register >=
    // value, unsigned). This previously hardcoded Negative to false
    // whenever register > value and true whenever register < value,
    // regardless of the actual result's bit 7 - e.g. A=0x80 vs value=0x00
    // is "register is bigger" but the subtraction result (0x80) still has
    // bit 7 set, so Negative should be true, not false. Fixed by computing
    // the real result and reusing Status::NZ (Negative/Zero) directly.
    /// Shared CMP/CPX/CPY helper: sets Carry if `reg >= value` and N/Z from `reg - value`.
    static void Compare(uint8_t reg, uint8_t value)
    {
        NES_Register::P.Carry(reg >= value);
        Status::NZ(reg - value);
    }

    void Math::CMP(uint8_t value) { Compare(NES_Register::A, value); }
    void Math::CPX(uint8_t value) { Compare(NES_Register::X, value); }
    void Math::CPY(uint8_t value) { Compare(NES_Register::Y, value); }

    // FIXED (found via nestest.nes, and very likely one of the
    // highest-impact bugs in this whole session): per
    // http://wiki.nesdev.com/w/index.php/Instruction_reference#BIT and this
    // class's own header doc on Math::BIT ("Flags: N = M7, V = M6, Z"), only
    // Zero is supposed to come from `value & A`; Negative and Overflow are
    // supposed to be copied directly from bits 7 and 6 of the raw memory
    // `value`, completely independent of A. This previously computed all
    // three from `value & A` instead - the header doc comment already said
    // the right thing, the code just didn't do it. `BIT $addr` immediately
    // followed by a branch on N or V
    // (BPL/BMI/BVC/BVS) is one of the most common 6502 idioms for polling a
    // single hardware status bit (e.g. PPUSTATUS's vblank/sprite-0-hit
    // flags via BIT $2002) specifically *because* it doesn't touch A - with
    // this bug, that poll would silently depend on whatever value A
    // happened to hold, breaking it whenever A didn't coincidentally share
    // the polled bit.
    void Math::BIT(uint8_t value)
    {
        NES_Register::P.Negative((value & 0x80) > 0);
        NES_Register::P.Overflow((value & 0x40) > 0);
        NES_Register::P.Zero((value & NES_Register::A) == 0);
    }

    bool Math::branchTaken = false;
    bool Math::branchPageCrossed = false;

    void Math::Branch(int8_t r)
    {
        uint16_t oldPC = NES_Register::PC;
        uint16_t newPC = static_cast<uint16_t>(oldPC + r);
        NES_Register::PC = newPC;
        branchTaken = true;
        if ((oldPC & 0xFF00) != (newPC & 0xFF00))
            branchPageCrossed = true;
    }

    // --- Unofficial/illegal 6502 opcodes - see Math.h's NOTE. Each combo
    // instruction (SLO/RLA/SRE/RRA/DCP/ISC) is documented on nesdev as
    // "exactly like the two official instructions it's named after, just one
    // instruction, supporting more addressing modes" - implemented here as
    // literally that: the same shift/rotate helpers and Compare()/ADC()/SBC()
    // this file already uses for the official versions.

    void Math::LAX(uint8_t value)
    {
        NES_Register::A = value;
        NES_Register::X = value;
        Status::NZ(value);
    }

    void Math::SAX(uint16_t a)
    {
        NES_Memory::Memory[a]->Value(static_cast<uint8_t>(NES_Register::A & NES_Register::X));
    }

    void Math::DCP(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        uint8_t value = static_cast<uint8_t>(m->Value() - 1);
        m->Value(value);
        Compare(NES_Register::A, value);
    }

    void Math::ISC(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        uint8_t value = static_cast<uint8_t>(m->Value() + 1);
        m->Value(value);
        NES_Register::A = SBC(NES_Register::A, value);
    }

    void Math::SLO(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        uint8_t value = ASL(m->Value()); // sets Carry as a side effect
        m->Value(value);
        NES_Register::A = static_cast<uint8_t>(NES_Register::A | value);
        Status::NZ(NES_Register::A);
    }

    void Math::RLA(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        int carryIn = Status::Carry();
        uint8_t value = static_cast<uint8_t>(ASL(m->Value()) | carryIn);
        m->Value(value);
        NES_Register::A = static_cast<uint8_t>(NES_Register::A & value);
        Status::NZ(NES_Register::A);
    }

    void Math::SRE(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        uint8_t value = LSR(m->Value()); // sets Carry as a side effect
        m->Value(value);
        NES_Register::A = static_cast<uint8_t>(NES_Register::A ^ value);
        Status::NZ(NES_Register::A);
    }

    void Math::RRA(uint16_t a)
    {
        auto m = NES_Memory::Memory[a];
        int carryIn = Status::Carry();
        uint8_t value = static_cast<uint8_t>(LSR(m->Value()) | (carryIn << 7));
        m->Value(value);
        NES_Register::A = ADC(NES_Register::A, value);
    }

    void Math::ANC(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A & value);
        Status::NZ(NES_Register::A);
        NES_Register::P.Carry((NES_Register::A & 0x80) != 0);
    }

    void Math::ALR(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A & value);
        NES_Register::A = LSR(NES_Register::A); // sets Carry as a side effect
        Status::NZ(NES_Register::A);
    }

    void Math::ARR(uint8_t value)
    {
        NES_Register::A = static_cast<uint8_t>(NES_Register::A & value);
        int carryIn = Status::Carry();
        NES_Register::A = static_cast<uint8_t>((NES_Register::A >> 1) | (carryIn << 7));
        Status::NZ(NES_Register::A);
        bool bit6 = (NES_Register::A & 0x40) != 0;
        bool bit5 = (NES_Register::A & 0x20) != 0;
        NES_Register::P.Carry(bit6);
        NES_Register::P.Overflow(bit6 != bit5);
    }

    void Math::SBX(uint8_t value)
    {
        uint8_t ax = static_cast<uint8_t>(NES_Register::A & NES_Register::X);
        int temp = ax - value;
        NES_Register::X = static_cast<uint8_t>(temp);
        Status::NZ(temp);
        NES_Register::P.Carry(ax >= value);
    }
}
