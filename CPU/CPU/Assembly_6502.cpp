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
#include "Assembly_6502.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"
#include "Parameter.h"
#include "Math.h"
#include "Status.h"
#include "Stack.h"
#include "Interrupt.h"

namespace NES
{
    // ---- Load: LDA/LDX/LDY ----

    void Assembly_6502::LDA_AD(uint16_t a) { NES_Register::A = NES_Memory::Memory[a]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_BD(uint16_t ax) { NES_Register::A = NES_Memory::Memory[Parameter::ax(ax)]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_B9(uint16_t ay) { NES_Register::A = NES_Memory::Memory[Parameter::ay(ay)]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_A9(uint8_t v) { NES_Register::A = v; Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_A5(uint8_t zp) { NES_Register::A = NES_Memory::Memory[Parameter::zp(zp)]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_A1(uint8_t zpx) { NES_Register::A = NES_Memory::Memory[Parameter::zpx1(zpx)]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_B5(uint8_t zpx) { NES_Register::A = NES_Memory::Memory[Parameter::zpx2(zpx)]->Value(); Status::NZ(NES_Register::A); }
    void Assembly_6502::LDA_B1(uint8_t zpy) { NES_Register::A = NES_Memory::Memory[Parameter::zpy1(zpy)]->Value(); Status::NZ(NES_Register::A); }

    void Assembly_6502::LDX_AE(uint16_t a) { NES_Register::X = NES_Memory::Memory[a]->Value(); Status::NZ(NES_Register::X); }
    void Assembly_6502::LDX_BE(uint16_t ay) { NES_Register::X = NES_Memory::Memory[Parameter::ay(ay)]->Value(); Status::NZ(NES_Register::X); }
    void Assembly_6502::LDX_A2(uint8_t v) { NES_Register::X = v; Status::NZ(NES_Register::X); }
    void Assembly_6502::LDX_A6(uint8_t zp) { NES_Register::X = NES_Memory::Memory[Parameter::zp(zp)]->Value(); Status::NZ(NES_Register::X); }
    void Assembly_6502::LDX_B6(uint8_t zpy) { NES_Register::X = NES_Memory::Memory[Parameter::zpy2(zpy)]->Value(); Status::NZ(NES_Register::X); }

    void Assembly_6502::LDY_AC(uint16_t a) { NES_Register::Y = NES_Memory::Memory[a]->Value(); Status::NZ(NES_Register::Y); }
    void Assembly_6502::LDY_BC(uint16_t ax) { NES_Register::Y = NES_Memory::Memory[Parameter::ax(ax)]->Value(); Status::NZ(NES_Register::Y); }
    void Assembly_6502::LDY_A0(uint8_t v) { NES_Register::Y = v; Status::NZ(NES_Register::Y); }
    void Assembly_6502::LDY_A4(uint8_t zp) { NES_Register::Y = NES_Memory::Memory[Parameter::zp(zp)]->Value(); Status::NZ(NES_Register::Y); }
    void Assembly_6502::LDY_B4(uint8_t zpx) { NES_Register::Y = NES_Memory::Memory[Parameter::zpx2(zpx)]->Value(); Status::NZ(NES_Register::Y); }

    // ---- Store: STA/STX/STY ----

    void Assembly_6502::STA_8D(uint16_t a) { NES_Memory::Memory[a]->Value(NES_Register::A); }
    void Assembly_6502::STA_9D(uint16_t ax) { NES_Memory::Memory[Parameter::ax(ax)]->Value(NES_Register::A); }
    void Assembly_6502::STA_99(uint16_t ay) { NES_Memory::Memory[Parameter::ay(ay)]->Value(NES_Register::A); }
    void Assembly_6502::STA_85(uint8_t zp) { NES_Memory::Memory[Parameter::zp(zp)]->Value(NES_Register::A); }
    void Assembly_6502::STA_81(uint8_t zpx) { NES_Memory::Memory[Parameter::zpx1(zpx)]->Value(NES_Register::A); }
    void Assembly_6502::STA_95(uint8_t zpx) { NES_Memory::Memory[Parameter::zpx2(zpx)]->Value(NES_Register::A); }
    void Assembly_6502::STA_91(uint8_t zpy) { NES_Memory::Memory[Parameter::zpy1(zpy)]->Value(NES_Register::A); }

    void Assembly_6502::STX_8E(uint16_t a) { NES_Memory::Memory[a]->Value(NES_Register::X); }
    void Assembly_6502::STX_86(uint8_t zp) { NES_Memory::Memory[Parameter::zp(zp)]->Value(NES_Register::X); }
    void Assembly_6502::STX_96(uint8_t zpy) { NES_Memory::Memory[Parameter::zpy2(zpy)]->Value(NES_Register::X); }

    void Assembly_6502::STY_8C(uint16_t a) { NES_Memory::Memory[a]->Value(NES_Register::Y); }
    void Assembly_6502::STY_84(uint8_t zp) { NES_Memory::Memory[Parameter::zp(zp)]->Value(NES_Register::Y); }
    void Assembly_6502::STY_94(uint8_t zpx) { NES_Memory::Memory[Parameter::zpx2(zpx)]->Value(NES_Register::Y); }

    // ---- Arithmetic: ADC/SBC ----

    void Assembly_6502::ADC_6D(uint16_t a) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::ADC_7D(uint16_t ax) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::ADC_79(uint16_t ay) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::ADC_69(uint8_t v) { NES_Register::A = Math::ADC(NES_Register::A, v); }
    void Assembly_6502::ADC_65(uint8_t zp) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[zp]->Value()); }
    void Assembly_6502::ADC_61(uint8_t zpx) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    // FIXED (found via nestest.nes):
    // this previously called Parameter.zpy2(zpx)
    // instead of zpx2(zpx) - opcode $75 ("ADC zp,X") was adding Y instead of
    // X to the zero-page address.
    void Assembly_6502::ADC_75(uint8_t zpx) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::ADC_71(uint8_t zpy) { NES_Register::A = Math::ADC(NES_Register::A, NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    void Assembly_6502::SBC_ED(uint16_t a) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[a]->Value()); }
    // FIXED (found via nestest.nes,
    // same bug class as ADC_75/ROR_76 above): this previously
    // called Parameter.ay(ax) instead of ax(ax) -
    // opcode $FD ("SBC abs,X") was adding Y instead of X.
    void Assembly_6502::SBC_FD(uint16_t ax) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::SBC_F9(uint16_t ay) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::SBC_E9(uint8_t v) { NES_Register::A = Math::SBC(NES_Register::A, v); }
    void Assembly_6502::SBC_E5(uint8_t zp) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::zp(zp)]->Value()); }
    void Assembly_6502::SBC_E1(uint8_t zpx) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    void Assembly_6502::SBC_F5(uint8_t zpx) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::SBC_F1(uint8_t zpy) { NES_Register::A = Math::SBC(NES_Register::A, NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    // ---- Increment/Decrement ----

    void Assembly_6502::INC_EE(uint16_t a) { auto m = NES_Memory::Memory[a]; m->Value(static_cast<uint8_t>(m->Value() + 1)); Status::NZ(m->Value()); }
    void Assembly_6502::INC_FE(uint16_t ax) { auto m = NES_Memory::Memory[Parameter::ax(ax)]; m->Value(static_cast<uint8_t>(m->Value() + 1)); Status::NZ(m->Value()); }
    void Assembly_6502::INC_E6(uint8_t zp) { auto m = NES_Memory::Memory[Parameter::zp(zp)]; m->Value(static_cast<uint8_t>(m->Value() + 1)); Status::NZ(m->Value()); }
    void Assembly_6502::INC_F6(uint8_t zpx) { auto m = NES_Memory::Memory[Parameter::zpx2(zpx)]; m->Value(static_cast<uint8_t>(m->Value() + 1)); Status::NZ(m->Value()); }

    void Assembly_6502::INX_E8() { NES_Register::X = static_cast<uint8_t>(NES_Register::X + 1); Status::NZ(NES_Register::X); }
    void Assembly_6502::INY_C8() { NES_Register::Y = static_cast<uint8_t>(NES_Register::Y + 1); Status::NZ(NES_Register::Y); }

    void Assembly_6502::DEC_CE(uint16_t a) { auto m = NES_Memory::Memory[a]; m->Value(static_cast<uint8_t>(m->Value() - 1)); Status::NZ(m->Value()); }
    void Assembly_6502::DEC_DE(uint16_t ax) { auto m = NES_Memory::Memory[Parameter::ax(ax)]; m->Value(static_cast<uint8_t>(m->Value() - 1)); Status::NZ(m->Value()); }
    void Assembly_6502::DEC_C6(uint8_t zp) { auto m = NES_Memory::Memory[Parameter::zp(zp)]; m->Value(static_cast<uint8_t>(m->Value() - 1)); Status::NZ(m->Value()); }
    void Assembly_6502::DEC_D6(uint8_t zpx) { auto m = NES_Memory::Memory[Parameter::zpx2(zpx)]; m->Value(static_cast<uint8_t>(m->Value() - 1)); Status::NZ(m->Value()); }

    void Assembly_6502::DEX_CA() { NES_Register::X = static_cast<uint8_t>(NES_Register::X - 1); Status::NZ(NES_Register::X); }
    void Assembly_6502::DEY_88() { NES_Register::Y = static_cast<uint8_t>(NES_Register::Y - 1); Status::NZ(NES_Register::Y); }

    // ---- Shift: ASL/LSR ----

    void Assembly_6502::ASL_0E(uint16_t a) { Math::ASL(a); }
    void Assembly_6502::ASL_1E(uint16_t ax) { Math::ASL(Parameter::ax(ax)); }
    void Assembly_6502::ASL_0A() { Math::ASL(); }
    void Assembly_6502::ASL_06(uint8_t zp) { Math::ASL(Parameter::zp(zp)); }
    void Assembly_6502::ASL_16(uint8_t zpx) { Math::ASL(Parameter::zpx2(zpx)); }

    void Assembly_6502::LSR_4E(uint16_t a) { Math::LSR(a); }
    void Assembly_6502::LSR_5E(uint16_t ax) { Math::LSR(Parameter::ax(ax)); }
    void Assembly_6502::LSR_4A() { Math::LSR(); }
    void Assembly_6502::LSR_46(uint8_t zp) { Math::LSR(Parameter::zp(zp)); }
    void Assembly_6502::LSR_56(uint8_t zpx) { Math::LSR(Parameter::zpx2(zpx)); }

    // ---- Rotate: ROL/ROR ----

    void Assembly_6502::ROL_2E(uint16_t a) { Math::ROL(a); }
    void Assembly_6502::ROL_3E(uint16_t ax) { Math::ROL(Parameter::ax(ax)); }
    void Assembly_6502::ROL_2A() { Math::ROL(); }
    void Assembly_6502::ROL_26(uint8_t zp) { Math::ROL(Parameter::zp(zp)); }
    void Assembly_6502::ROL_36(uint8_t zpx) { Math::ROL(Parameter::zpx2(zpx)); }

    void Assembly_6502::ROR_6E(uint16_t a) { Math::ROR(a); }
    void Assembly_6502::ROR_7E(uint16_t ax) { Math::ROR(Parameter::ax(ax)); }
    void Assembly_6502::ROR_6A() { Math::ROR(); }
    // Calls Math::ROR(zp) directly (no Parameter::zp() wrapper);
    // Parameter::zp() is the identity function, so this is equivalent.
    void Assembly_6502::ROR_66(uint8_t zp) { Math::ROR(zp); }
    // FIXED (found via nestest.nes,
    // same bug class as ADC_75 above): this previously called Parameter.zpy2(zpx) instead of zpx2(zpx) - opcode $76
    // ("ROR zp,X") was adding Y instead of X.
    void Assembly_6502::ROR_76(uint8_t zpx) { Math::ROR(Parameter::zpx2(zpx)); }

    // ---- Logic: AND/ORA/EOR ----

    void Assembly_6502::AND_2D(uint16_t a) { Math::AND(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::AND_3D(uint16_t ax) { Math::AND(NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::AND_39(uint16_t ay) { Math::AND(NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::AND_29(uint8_t v) { Math::AND(v); }
    void Assembly_6502::AND_25(uint8_t zp) { Math::AND(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }
    void Assembly_6502::AND_21(uint8_t zpx) { Math::AND(NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    void Assembly_6502::AND_35(uint8_t zpx) { Math::AND(NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::AND_31(uint8_t zpy) { Math::AND(NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    void Assembly_6502::ORA_0D(uint16_t a) { Math::ORA(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::ORA_1D(uint16_t ax) { Math::ORA(NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::ORA_19(uint16_t ay) { Math::ORA(NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::ORA_09(uint8_t v) { Math::ORA(v); }
    void Assembly_6502::ORA_05(uint8_t zp) { Math::ORA(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }
    void Assembly_6502::ORA_01(uint8_t zpx) { Math::ORA(NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    void Assembly_6502::ORA_15(uint8_t zpx) { Math::ORA(NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::ORA_11(uint8_t zpy) { Math::ORA(NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    void Assembly_6502::EOR_4D(uint16_t a) { Math::EOR(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::EOR_5D(uint16_t ax) { Math::EOR(NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::EOR_59(uint16_t ay) { Math::EOR(NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::EOR_49(uint8_t v) { Math::EOR(v); }
    void Assembly_6502::EOR_45(uint8_t zp) { Math::EOR(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }
    void Assembly_6502::EOR_41(uint8_t zpx) { Math::EOR(NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    void Assembly_6502::EOR_55(uint8_t zpx) { Math::EOR(NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::EOR_51(uint8_t zpy) { Math::EOR(NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    // ---- Compare / Test Bit ----

    void Assembly_6502::CMP_CD(uint16_t a) { Math::CMP(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::CMP_DD(uint16_t ax) { Math::CMP(NES_Memory::Memory[Parameter::ax(ax)]->Value()); }
    void Assembly_6502::CMP_D9(uint16_t ay) { Math::CMP(NES_Memory::Memory[Parameter::ay(ay)]->Value()); }
    void Assembly_6502::CMP_C9(uint8_t v) { Math::CMP(v); }
    void Assembly_6502::CMP_C5(uint8_t zp) { Math::CMP(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }
    void Assembly_6502::CMP_C1(uint8_t zpx) { Math::CMP(NES_Memory::Memory[Parameter::zpx1(zpx)]->Value()); }
    void Assembly_6502::CMP_D5(uint8_t zpx) { Math::CMP(NES_Memory::Memory[Parameter::zpx2(zpx)]->Value()); }
    void Assembly_6502::CMP_D1(uint8_t zpy) { Math::CMP(NES_Memory::Memory[Parameter::zpy1(zpy)]->Value()); }

    void Assembly_6502::CPX_EC(uint16_t a) { Math::CPX(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::CPX_E0(uint8_t v) { Math::CPX(v); }
    void Assembly_6502::CPX_E4(uint8_t zp) { Math::CPX(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }

    void Assembly_6502::CPY_CC(uint16_t a) { Math::CPY(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::CPY_C0(uint8_t v) { Math::CPY(v); }
    void Assembly_6502::CPY_C4(uint8_t zp) { Math::CPY(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }

    void Assembly_6502::BIT_2C(uint16_t a) { Math::BIT(NES_Memory::Memory[a]->Value()); }
    void Assembly_6502::BIT_89(uint8_t v) { Math::BIT(v); }
    void Assembly_6502::BIT_24(uint8_t zp) { Math::BIT(NES_Memory::Memory[Parameter::zp(zp)]->Value()); }

    // ---- Branch ----

    void Assembly_6502::BCC_90(int8_t r) { if (!NES_Register::P.Carry()) Math::Branch(r); }
    void Assembly_6502::BCS_B0(int8_t r) { if (NES_Register::P.Carry()) Math::Branch(r); }
    void Assembly_6502::BEQ_F0(int8_t r) { if (NES_Register::P.Zero()) Math::Branch(r); }
    void Assembly_6502::BMI_30(int8_t r) { if (NES_Register::P.Negative()) Math::Branch(r); }
    void Assembly_6502::BNE_D0(int8_t r) { if (!NES_Register::P.Zero()) Math::Branch(r); }
    void Assembly_6502::BPL_10(int8_t r) { if (!NES_Register::P.Negative()) Math::Branch(r); }
    // FIXED (found via nestest.nes,
    // the standard 6502 core-conformance ROM: this port's CPU trace matched
    // nestest.log's expected register/flag state exactly for the first 39
    // instructions, then diverged in PC only, at the very first BVS in the
    // test - see tests/nestest). BVC_50/BVS_70 previously
    // tested the Carry flag instead of Overflow, despite
    // being BVC/BVS ("Branch on oVerflow Clear/Set" -
    // http://wiki.nesdev.com/w/index.php/Status_flags). BIT+BVS/BVC (test a
    // memory byte's bit 6 via BIT, then branch on it) is one of the most
    // common 6502 idioms for polling a single hardware status bit without
    // touching the accumulator - PPUSTATUS's sprite-0-hit and vblank flags
    // are exactly the kind of bit this pattern is used for - so this bug
    // could silently misroute *any* such poll based on whatever the Carry
    // flag happened to be left at, a very plausible root cause behind
    // hard-to-pin-down "sometimes works, sometimes doesn't" behavior
    // throughout the emulator. Fixed to test Overflow(), matching the
    // opcode names and nestest.log.
    void Assembly_6502::BVC_50(int8_t r) { if (!NES_Register::P.Overflow()) Math::Branch(r); }
    void Assembly_6502::BVS_70(int8_t r) { if (NES_Register::P.Overflow()) Math::Branch(r); }

    // ---- Transfer ----

    void Assembly_6502::TAX_AA() { NES_Register::X = NES_Register::A; Status::NZ(NES_Register::X); }
    void Assembly_6502::TXA_8A() { NES_Register::A = NES_Register::X; Status::NZ(NES_Register::A); }
    void Assembly_6502::TAY_A8() { NES_Register::Y = NES_Register::A; Status::NZ(NES_Register::A); }
    void Assembly_6502::TYA_98() { NES_Register::A = NES_Register::Y; Status::NZ(NES_Register::Y); }
    void Assembly_6502::TSX_BA() { NES_Register::X = NES_Register::S; Status::NZ(NES_Register::S); }
    void Assembly_6502::TXS_9A() { NES_Register::S = NES_Register::X; }

    // ---- Stack ----

    void Assembly_6502::PHA_48() { Stack::PushToStack(NES_Register::A); }
    // FIXED (found via nestest.nes):
    // PLA_68 previously never updated the N/Z flags
    // from the popped value despite its own doc comment above ("Flags: N,
    // Z") and http://www.masswerk.at/6502/6502_instruction_set.html#PLA
    // agreeing PLA sets both - it just assigned A and stopped. Fixed by
    // adding the same Status::NZ(NES_Register::A) call every other
    // load-into-A opcode (LDA, TXA, TYA, ...) already makes.
    void Assembly_6502::PLA_68() { NES_Register::A = Stack::PopFromStack(); Status::NZ(NES_Register::A); }
    void Assembly_6502::PHP_08() { Stack::PushToStack(static_cast<uint8_t>(NES_Register::P.P | 0x30)); }
    void Assembly_6502::PLP_28() { Stack::StackToProcessorstatus(); }

    // ---- Subroutines and Jump ----

    void Assembly_6502::JMP_4C(uint16_t a) { NES_Register::PC = a; }
    void Assembly_6502::JMP_6C(uint16_t a) { NES_Register::PC = Parameter::MemoryValueToAdress(a); }

    void Assembly_6502::JSR_20(uint16_t a) { Stack::PcToStack(); NES_Register::PC = a; }
    void Assembly_6502::RTS_60() { Stack::StackToPc(); }
    // FIXED (found via nestest.nes,
    // together with the matching push-order fix in Interrupt::ReplacePC):
    // pops in the real 6502 order (flags first, then PC - see
    // ReplacePC's NOTE) and, per Stack::StackToPc's NOTE, without RTS's `+1`
    // (an interrupt push is already the exact resume address).
    void Assembly_6502::RTI_40() { Stack::StackToProcessorstatus(); Stack::StackToPc(false); }

    // ---- Set / Clear flags ----

    void Assembly_6502::SEC_38() { NES_Register::P.Carry(true); }
    void Assembly_6502::SED_F8() { NES_Register::P.Decimal(true); }
    void Assembly_6502::SEI_78() { NES_Register::P.Interrupt(true); }

    void Assembly_6502::CLC_18() { NES_Register::P.Carry(false); }
    void Assembly_6502::CLD_D8() { NES_Register::P.Decimal(false); }
    void Assembly_6502::CLI_58() { NES_Register::P.Interrupt(false); }
    void Assembly_6502::CLV_B8() { NES_Register::P.Overflow(false); }

    // ---- Miscellaneous ----

    void Assembly_6502::NOP_EA() {}
    void Assembly_6502::BRK_00() { Interrupt::BRK(true); }
}
