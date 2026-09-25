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
    /// @brief Shared arithmetic/logic/shift/compare implementations used by the
    /// opcode table in Assembly_6502.
    /// Named `Math` - safe in C++ since it lives in namespace NES and nothing
    /// here collides with `<cmath>`.
    class Math
    {
    public:
        /// Add Memory to Accumulator with Carry: ADC. A + M + C -> A. Flags: N,V,Z,C.
        static uint8_t ADC(int A, int B);
        /// Subtract Memory from Accumulator with Borrow: SBC. A - M - !C -> A. Flags: N,V,Z,C.
        static uint8_t SBC(int A, int B);

        /// Arithmetic Shift Left One Bit: ASL (memory operand). Flags: N,Z,C.
        static void ASL(uint16_t a);
        /// Arithmetic Shift Left One Bit: ASL (accumulator operand). Flags: N,Z,C.
        static void ASL();

        /// Logical Shift Right One Bit: LSR (memory operand). Flags: N,Z,C.
        static void LSR(uint16_t a);
        /// Logical Shift Right One Bit: LSR (accumulator operand). Flags: N,Z,C.
        static void LSR();

        /// Rotate Left One Bit: ROL (memory operand). Flags: N,Z,C.
        static void ROL(uint16_t a);
        /// Rotate Left One Bit: ROL (accumulator operand). Flags: N,Z,C.
        static void ROL();

        /// Rotate Right One Bit: ROR (memory operand). Flags: N,Z,C.
        static void ROR(uint16_t a);
        /// Rotate Right One Bit: ROR (accumulator operand). Flags: N,Z,C.
        static void ROR();

        /// AND Memory with Accumulator: AND. A & M -> A. Flags: N,Z.
        static void AND(uint8_t value);
        /// OR Memory with Accumulator: ORA. A | M -> A. Flags: N,Z.
        static void ORA(uint8_t value);
        /// Exclusive-OR Memory with Accumulator: EOR. A ^ M -> A. Flags: N,Z.
        static void EOR(uint8_t value);

        /// Compare Memory and Accumulator: CMP. A - M. Flags: N,Z,C.
        static void CMP(uint8_t value);
        /// Compare Memory and Index X: CPX. X - M. Flags: N,Z,C.
        static void CPX(uint8_t value);
        /// Compare Memory with Index Y: CPY. Y - M. Flags: N,Z,C.
        static void CPY(uint8_t value);

        /// Test Bits in Memory with Accumulator: BIT. A & M. Flags: N=M7, V=M6, Z.
        static void BIT(uint8_t value);

        /// Relative branch: adds the signed 8-bit offset `r` to PC.
        static void Branch(int8_t r);

        // See NES_CPU.cpp's own comment (the "kCycleTable's known
        // undercounting" fix) for the full story. Real
        // 6502 hardware takes one extra cycle whenever a conditional branch
        // is actually taken, and one more on top of that if the branch
        // target lands on a different page than the instruction after the
        // branch - http://wiki.nesdev.com/w/index.php/CPU_addressing_modes
        // ("+1 cycle if branch is taken, +1 cycle if the branch is taken and
        // the target is on a different page"). Every BCC/BCS/.../BVS handler
        // only ever calls Branch() when its own condition is true (see
        // Assembly_6502.cpp: `if (cond) Math::Branch(r);`), so every call
        // here unconditionally represents a taken branch - no separate
        // "was it taken" check needed, unlike Parameter::pageCrossed (which
        // several non-page-cross-sensitive instructions also trigger and
        // must be filtered by opcode in Step()). Reset once per Step() call,
        // before dispatch.
        /// Set when a branch instruction was taken (costs one extra cycle).
        static bool branchTaken;
        /// Set when a taken branch crossed a page boundary (costs another extra cycle).
        static bool branchPageCrossed;
        /// Clears branchTaken and branchPageCrossed; called once per CPU step.
        static void ResetBranchFlags() { branchTaken = false; branchPageCrossed = false; }

        // --- Unofficial/illegal 6502 opcodes.
        // See AssemblyList.cpp's NOTE on UnofficialNOPs()/UnofficialOpcodes()
        // for why these exist: none of them were implemented before, which
        // was fine until fixing the official-opcode bugs (found via
        // nestest.nes) let the CPU actually reach code that uses them.
        // http://wiki.nesdev.com/w/index.php/Programming_with_unofficial_opcodes

        /// LAX: LDA value then TAX in one instruction. Flags: N,Z.
        static void LAX(uint8_t value);
        /// SAX: stores A & X to memory. No flags.
        static void SAX(uint16_t a);
        /// DCP: DEC value then CMP value. Flags: N,Z,C.
        static void DCP(uint16_t a);
        /// ISC/ISB: INC value then SBC value. Flags: N,V,Z,C.
        static void ISC(uint16_t a);
        /// SLO: ASL value then ORA value. Flags: N,Z,C.
        static void SLO(uint16_t a);
        /// RLA: ROL value then AND value. Flags: N,Z,C.
        static void RLA(uint16_t a);
        /// SRE: LSR value then EOR value. Flags: N,Z,C.
        static void SRE(uint16_t a);
        /// RRA: ROR value then ADC value. Flags: N,V,Z,C.
        static void RRA(uint16_t a);
        /// ANC: AND `#imm`, then copies the result's Negative flag into Carry. Flags: N,Z,C.
        static void ANC(uint8_t value);
        /// ALR/ASR: AND `#imm` then LSR A. Flags: N,Z,C.
        static void ALR(uint8_t value);
        /// ARR: AND `#imm` then ROR A, but C/V are derived from bits 6/5 of the
        /// result rather than the normal ROR carry-out. Flags: N,Z,C,V.
        static void ARR(uint8_t value);
        /// SBX/AXS: X = (A & X) - imm, unsigned, no borrow-in. Flags: N,Z,C.
        static void SBX(uint8_t value);

    private:
        static uint8_t ASL(uint8_t value);
        static void getASLCarry(uint8_t value);
        static uint8_t LSR(uint8_t value);
        static void getLSRCarry(uint8_t value);
    };
}
