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
///
/// https://en.wikibooks.org/wiki/6502_Assembly and http://nesdev.com/6502.txt
#pragma once
#include <cstdint>

namespace NES
{
    /// @brief One static method per 6502 opcode (name = MNEMONIC_HEXOPCODE).
    /// Every method here is looked up from
    /// AssemblyList's opcode table and does exactly one instruction's work;
    /// operand fetching/PC advancement happens in AssemblyList, not here.
    /// See https://en.wikibooks.org/wiki/6502_Assembly and http://nesdev.com/6502.txt
    class Assembly_6502
    {
    public:
        // ---- Load: LDA/LDX/LDY ----

        /// Load Accumulator with Memory: LDA. M -> A. Flags: N, Z.
        /// Absolute: a - a full 16-bit address; the byte there is used.
        /// Example: LDX $D010 loads the value at $D010 into X.
        static void LDA_AD(uint16_t a);
        /// LDA, Absolute Indexed with X: a,x - X is added to `ax` for the sum address.
        /// Example: ADC $C001,X - $02 in X + $C001 = $C003, whose value is used.
        static void LDA_BD(uint16_t ax);
        /// LDA, Absolute Indexed with Y: a,y - Y is added to `ay` for the sum address.
        /// Example: ADC $C001,Y - $02 in Y + $C001 = $C003, whose value is used.
        static void LDA_B9(uint16_t ay);
        /// LDA, Immediate: #(v) - the operand is used directly.
        /// Example: `LDA #$22` loads the value 0x22 into A.
        static void LDA_A9(uint8_t v);
        /// LDA, Zero Page: a single byte addresses $00xx; the byte there is used.
        /// Example: LDY $02 loads the value at $0002 into Y.
        static void LDA_A5(uint8_t zp);
        /// LDA, Zero Page Indexed Indirect: (zp,x) - X is added to `zpx` for a zero
        /// page sum address; the little-endian word stored there is the final address.
        /// Example: STA ($15,X) - $02 in X + $15 = $17; the word at $0017/$0018
        /// is where A gets stored.
        static void LDA_A1(uint8_t zpx);
        /// LDA, Zero Page Indexed with X: zp,x - X is added to `zpx` for the sum address.
        /// Example: LDA $01,X - $02 in X + $01 = $03; the value at $0003 is loaded.
        static void LDA_B5(uint8_t zpx);
        /// LDA, Zero Page Indirect Indexed with Y: (zp),y - the little-endian word
        /// at `zpy`/`zpy`+1 has Y added to it for the final address.
        /// Example: LSR ($2A),Y - $03 in Y + the word at $002A/$002B ($C235) = $C238.
        static void LDA_B1(uint8_t zpy);

        /// Load Index X with Memory: LDX. M -> X. Flags: N, Z. Absolute: a.
        static void LDX_AE(uint16_t a);
        /// LDX, Absolute Indexed with Y: a,y.
        static void LDX_BE(uint16_t ay);
        /// LDX, Immediate: #(v).
        static void LDX_A2(uint8_t v);
        /// LDX, Zero Page: zp.
        static void LDX_A6(uint8_t zp);
        /// LDX, Zero Page Indexed with Y: zp,y - Y is added to `zpy` for the sum address.
        /// Example: LDA $01,Y - $03 in Y + $01 = $04; the value at $0004 is loaded.
        static void LDX_B6(uint8_t zpy);

        /// Load Index Y with Memory: LDY. M -> Y. Flags: N, Z. Absolute: a.
        static void LDY_AC(uint16_t a);
        /// LDY, Absolute Indexed with X: a,x.
        static void LDY_BC(uint16_t ax);
        /// LDY, Immediate: #(v).
        static void LDY_A0(uint8_t v);
        /// LDY, Zero Page: zp.
        static void LDY_A4(uint8_t zp);
        /// LDY, Zero Page Indexed with X: zp,x.
        static void LDY_B4(uint8_t zpx);

        // ---- Store: STA/STX/STY ----

        /// Store Accumulator in Memory: STA. A -> M. Absolute: a.
        static void STA_8D(uint16_t a);
        /// STA, Absolute Indexed with X: a,x.
        static void STA_9D(uint16_t ax);
        /// STA, Absolute Indexed with Y: a,y.
        static void STA_99(uint16_t ay);
        /// STA, Zero Page: zp.
        static void STA_85(uint8_t zp);
        /// STA, Zero Page Indexed Indirect: (zp,x).
        static void STA_81(uint8_t zpx);
        /// STA, Zero Page Indexed with X: zp,x.
        static void STA_95(uint8_t zpx);
        /// STA, Zero Page Indirect Indexed with Y: (zp),y.
        static void STA_91(uint8_t zpy);

        /// Store Index X in Memory: STX. X -> M. Absolute: a.
        static void STX_8E(uint16_t a);
        /// STX, Zero Page: zp.
        static void STX_86(uint8_t zp);
        /// STX, Zero Page Indexed with Y: zp,y.
        static void STX_96(uint8_t zpy);

        /// Store Index Y in Memory: STY. Y -> M. Absolute: a.
        static void STY_8C(uint16_t a);
        /// STY, Zero Page: zp.
        static void STY_84(uint8_t zp);
        /// STY, Zero Page Indexed with X: zp,x.
        static void STY_94(uint8_t zpx);

        // ---- Arithmetic: ADC/SBC ----

        /// Add Memory to Accumulator with Carry: ADC. A + M + C -> A. Flags: N,V,Z,C. Absolute: a.
        static void ADC_6D(uint16_t a);
        /// ADC, Absolute Indexed with X: a,x.
        static void ADC_7D(uint16_t ax);
        /// ADC, Absolute Indexed with Y: a,y.
        static void ADC_79(uint16_t ay);
        /// ADC, Immediate: #(v).
        static void ADC_69(uint8_t v);
        /// ADC, Zero Page: zp.
        static void ADC_65(uint8_t zp);
        /// ADC, Zero Page Indexed Indirect: (zp,x).
        static void ADC_61(uint8_t zpx);
        /// ADC, Zero Page Indexed with X: zp,x.
        static void ADC_75(uint8_t zpx);
        /// ADC, Zero Page Indirect Indexed with Y: (zp),y.
        static void ADC_71(uint8_t zpy);

        /// Subtract Memory from Accumulator with Borrow: SBC. A - M - !C -> A. Flags: N,V,Z,C. Absolute: a.
        static void SBC_ED(uint16_t a);
        /// SBC, Absolute Indexed with X: a,x.
        static void SBC_FD(uint16_t ax);
        /// SBC, Absolute Indexed with Y: a,y.
        static void SBC_F9(uint16_t ay);
        /// SBC, Immediate: #(v).
        static void SBC_E9(uint8_t v);
        /// SBC, Zero Page: zp.
        static void SBC_E5(uint8_t zp);
        /// SBC, Zero Page Indexed Indirect: (zp,x).
        static void SBC_E1(uint8_t zpx);
        /// SBC, Zero Page Indexed with X: zp,x.
        static void SBC_F5(uint8_t zpx);
        /// SBC, Zero Page Indirect Indexed with Y: (zp),y.
        static void SBC_F1(uint8_t zpy);

        // ---- Increment/Decrement: INC/INX/INY/DEC/DEX/DEY ----

        /// Increment Memory by One: INC. M + 1 -> M. Flags: N, Z. Absolute: a.
        static void INC_EE(uint16_t a);
        /// INC, Absolute Indexed with X: a,x.
        static void INC_FE(uint16_t ax);
        /// INC, Zero Page: zp.
        static void INC_E6(uint8_t zp);
        /// INC, Zero Page Indexed with X: zp,x.
        static void INC_F6(uint8_t zpx);

        /// Increment Index X by One: INX. X + 1 -> X. Flags: N, Z.
        static void INX_E8();
        /// Increment Index Y by One: INY. Y + 1 -> Y. Flags: N, Z.
        static void INY_C8();

        /// Decrement Memory by One: DEC. M - 1 -> M. Flags: N, Z. Absolute: a.
        static void DEC_CE(uint16_t a);
        /// DEC, Absolute Indexed with X: a,x.
        static void DEC_DE(uint16_t ax);
        /// DEC, Zero Page: zp.
        static void DEC_C6(uint8_t zp);
        /// DEC, Zero Page Indexed with X: zp,x.
        static void DEC_D6(uint8_t zpx);

        /// Decrement Index X by One: DEX. X - 1 -> X. Flags: N, Z.
        static void DEX_CA();
        /// Decrement Index Y by One: DEY. Y - 1 -> Y. Flags: N, Z.
        static void DEY_88();

        // ---- Shift: ASL/LSR ----

        /// Arithmetic Shift Left One Bit: ASL. C<-76543210<-0. Flags: N,Z,C. Absolute: a.
        static void ASL_0E(uint16_t a);
        /// ASL, Absolute Indexed with X: a,x.
        static void ASL_1E(uint16_t ax);
        /// ASL, Accumulator - no operand, shifts A itself.
        static void ASL_0A();
        /// ASL, Zero Page: zp.
        static void ASL_06(uint8_t zp);
        /// ASL, Zero Page Indexed with X: zp,x.
        static void ASL_16(uint8_t zpx);

        /// Logical Shift Right One Bit: LSR. 0->76543210->C. Flags: N,Z,C. Absolute: a.
        static void LSR_4E(uint16_t a);
        /// LSR, Absolute Indexed with X: a,x.
        static void LSR_5E(uint16_t ax);
        /// LSR, Accumulator.
        static void LSR_4A();
        /// LSR, Zero Page: zp.
        static void LSR_46(uint8_t zp);
        /// LSR, Zero Page Indexed with X: zp,x.
        static void LSR_56(uint8_t zpx);

        // ---- Rotate: ROL/ROR ----

        /// Rotate Left One Bit: ROL. C<-76543210<-C. Flags: N,Z,C. Absolute: a.
        static void ROL_2E(uint16_t a);
        /// ROL, Absolute Indexed with X: a,x.
        static void ROL_3E(uint16_t ax);
        /// ROL, Accumulator.
        static void ROL_2A();
        /// ROL, Zero Page: zp.
        static void ROL_26(uint8_t zp);
        /// ROL, Zero Page Indexed with X: zp,x.
        static void ROL_36(uint8_t zpx);

        /// Rotate Right One Bit: ROR. C->76543210->C. Flags: N,Z,C. Absolute: a.
        static void ROR_6E(uint16_t a);
        /// ROR, Absolute Indexed with X: a,x.
        static void ROR_7E(uint16_t ax);
        /// ROR, Accumulator.
        static void ROR_6A();
        /// ROR, Zero Page: zp.
        static void ROR_66(uint8_t zp);
        /// ROR, Zero Page Indexed with X: zp,x.
        static void ROR_76(uint8_t zpx);

        // ---- Logic: AND/ORA/EOR ----

        /// AND Memory with Accumulator: AND. A & M -> A. Flags: N, Z. Absolute: a.
        static void AND_2D(uint16_t a);
        /// AND, Absolute Indexed with X: a,x.
        static void AND_3D(uint16_t ax);
        /// AND, Absolute Indexed with Y: a,y.
        static void AND_39(uint16_t ay);
        /// AND, Immediate: #(v).
        static void AND_29(uint8_t v);
        /// AND, Zero Page: zp.
        static void AND_25(uint8_t zp);
        /// AND, Zero Page Indexed Indirect: (zp,x).
        static void AND_21(uint8_t zpx);
        /// AND, Zero Page Indexed with X: zp,x.
        static void AND_35(uint8_t zpx);
        /// AND, Zero Page Indirect Indexed with Y: (zp),y.
        static void AND_31(uint8_t zpy);

        /// OR Memory with Accumulator: ORA. A | M -> A. Flags: N, Z. Absolute: a.
        static void ORA_0D(uint16_t a);
        /// ORA, Absolute Indexed with X: a,x.
        static void ORA_1D(uint16_t ax);
        /// ORA, Absolute Indexed with Y: a,y.
        static void ORA_19(uint16_t ay);
        /// ORA, Immediate: #(v).
        static void ORA_09(uint8_t v);
        /// ORA, Zero Page: zp.
        static void ORA_05(uint8_t zp);
        /// ORA, Zero Page Indexed Indirect: (zp,x).
        static void ORA_01(uint8_t zpx);
        /// ORA, Zero Page Indexed with X: zp,x.
        static void ORA_15(uint8_t zpx);
        /// ORA, Zero Page Indirect Indexed with Y: (zp),y.
        static void ORA_11(uint8_t zpy);

        /// Exclusive-OR Memory with Accumulator: EOR. A ^ M -> A. Flags: N, Z. Absolute: a.
        static void EOR_4D(uint16_t a);
        /// EOR, Absolute Indexed with X: a,x.
        static void EOR_5D(uint16_t ax);
        /// EOR, Absolute Indexed with Y: a,y.
        static void EOR_59(uint16_t ay);
        /// EOR, Immediate: #(v).
        static void EOR_49(uint8_t v);
        /// EOR, Zero Page: zp.
        static void EOR_45(uint8_t zp);
        /// EOR, Zero Page Indexed Indirect: (zp,x).
        static void EOR_41(uint8_t zpx);
        /// EOR, Zero Page Indexed with X: zp,x.
        static void EOR_55(uint8_t zpx);
        /// EOR, Zero Page Indirect Indexed with Y: (zp),y.
        static void EOR_51(uint8_t zpy);

        // ---- Compare / Test Bit: CMP/CPX/CPY/BIT ----
        // For all compare instructions: Register<Memory -> N=1,Z=0,C=0;
        // Register=Memory -> N=0,Z=1,C=1; Register>Memory -> N=0,Z=0,C=1.

        /// Compare Memory and Accumulator: CMP. A - M. Flags: N,Z,C. Absolute: a.
        static void CMP_CD(uint16_t a);
        /// CMP, Absolute Indexed with X: a,x.
        static void CMP_DD(uint16_t ax);
        /// CMP, Absolute Indexed with Y: a,y.
        static void CMP_D9(uint16_t ay);
        /// CMP, Immediate: #(v).
        static void CMP_C9(uint8_t v);
        /// CMP, Zero Page: zp.
        static void CMP_C5(uint8_t zp);
        /// CMP, Zero Page Indexed Indirect: (zp,x).
        static void CMP_C1(uint8_t zpx);
        /// CMP, Zero Page Indexed with X: zp,x.
        static void CMP_D5(uint8_t zpx);
        /// CMP, Zero Page Indirect Indexed with Y: (zp),y.
        static void CMP_D1(uint8_t zpy);

        /// Compare Memory and Index X: CPX. X - M. Flags: N,Z,C. Absolute: a.
        static void CPX_EC(uint16_t a);
        /// CPX, Immediate: #(v).
        static void CPX_E0(uint8_t v);
        /// CPX, Zero Page: zp.
        static void CPX_E4(uint8_t zp);

        /// Compare Memory with Index Y: CPY. Y - M. Flags: N,Z,C. Absolute: a.
        static void CPY_CC(uint16_t a);
        /// CPY, Immediate: #(v).
        static void CPY_C0(uint8_t v);
        /// CPY, Zero Page: zp.
        static void CPY_C4(uint8_t zp);

        /// Test Bits in Memory with Accumulator: BIT. A & M. Flags: N=M7,V=M6,Z. Absolute: a.
        static void BIT_2C(uint16_t a);
        /// BIT, Immediate: #(v).
        static void BIT_89(uint8_t v);
        /// BIT, Zero Page: zp.
        static void BIT_24(uint8_t zp);

        // ---- Branch (relative, signed 8-bit offset r, -128..+127) ----

        /// Branch on Carry Clear: BCC. Branch if C = 0.
        static void BCC_90(int8_t r);
        /// Branch on Carry Set: BCS. Branch if C = 1.
        static void BCS_B0(int8_t r);
        /// Branch on Result Zero: BEQ. Branch if Z = 1.
        static void BEQ_F0(int8_t r);
        /// Branch on Result Minus: BMI. Branch if N = 1.
        static void BMI_30(int8_t r);
        /// Branch on Result not Zero: BNE. Branch if Z = 0.
        static void BNE_D0(int8_t r);
        /// Branch on Result Plus: BPL. Branch if N = 0.
        static void BPL_10(int8_t r);
        /// Branch on Overflow Clear: BVC. Branch if V = 0.
        static void BVC_50(int8_t r);
        /// Branch on Overflow Set: BVS. Branch if V = 1.
        static void BVS_70(int8_t r);

        // ---- Transfer ----

        /// Transfer Accumulator to Index X: TAX. A -> X. Flags: N, Z.
        static void TAX_AA();
        /// Transfer Index X to Accumulator: TXA. X -> A. Flags: N, Z.
        static void TXA_8A();
        /// Transfer Accumulator to Index Y: TAY. A -> Y. Flags: N, Z.
        static void TAY_A8();
        /// Transfer Index Y to Accumulator: TYA. Y -> A. Flags: N, Z.
        static void TYA_98();
        /// Transfer Stack Pointer to Index X: TSX. S -> X. Flags: N, Z.
        static void TSX_BA();
        /// Transfer Index X to Stack Pointer: TXS. X -> S.
        static void TXS_9A();

        // ---- Stack ----

        /// Push Accumulator on Stack: PHA. A -> S.
        static void PHA_48();
        /// Pull Accumulator from Stack: PLA. S -> A. Flags: N, Z.
        static void PLA_68();
        /// Push Processor Status on Stack: PHP. P -> S (NV-BDIZC, high to low).
        static void PHP_08();
        /// Pull Processor Status from Stack: PLP. S -> P. Flags: ALL.
        /// Setting the processor status from the stack is the only way to clear
        /// the B (Break) flag.
        static void PLP_28();

        // ---- Subroutines and Jump ----

        /// Jump to New Location: JMP. Absolute: a - jumps straight to `a`.
        static void JMP_4C(uint16_t a);
        /// JMP, Indirect: (a) - the little-endian word stored at `a`/`a`+1 is the target.
        static void JMP_6C(uint16_t a);

        /// Jump to New Location Saving Return Address: JSR. Absolute: a.
        static void JSR_20(uint16_t a);
        /// Return from Subroutine: RTS.
        static void RTS_60();
        /// Return from Interrupt: RTI. Flags: all.
        static void RTI_40();

        // ---- Set / Clear flags ----

        /// Set Carry Flag: SEC. 1 -> C.
        static void SEC_38();
        /// Set Decimal Flag: SED. 1 -> D.
        static void SED_F8();
        /// Set Interrupt Disable Status: SEI. 1 -> I.
        static void SEI_78();

        /// Clear Carry Flag: CLC. 0 -> C.
        static void CLC_18();
        /// Clear Decimal Mode: CLD. 0 -> D.
        static void CLD_D8();
        /// Clear Interrupt Disable Status: CLI. 0 -> I.
        static void CLI_58();
        /// Clear Overflow Flag: CLV. 0 -> V.
        static void CLV_B8();

        // ---- Miscellaneous ----

        /// No Operation: NOP.
        static void NOP_EA();
        /// Break: BRK. Forces an interrupt. Flags: B = 1, I = 1.
        static void BRK_00();
    };
}
