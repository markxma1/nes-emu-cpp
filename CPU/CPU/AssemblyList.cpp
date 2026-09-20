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
#include "AssemblyList.h"
#include "Assembly_6502.h"
#include "Math.h"
#include "Parameter.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"
#include "../Exception/NoAssemby.h"

namespace NES
{
    std::vector<AssemblyList::Func> AssemblyList::Assembly;
    std::vector<std::string> AssemblyList::debug;

    AssemblyList::AssemblyList()
    {
        CreateAdressMemory();
        NoImputToList();
        U8BitToList();
        BranchToList();
        U16ByteToList();
    }

    void AssemblyList::CreateAdressMemory()
    {
        Assembly.clear();
        // FIXED (was a preserved C# bug, now corrected - this one crashed
        // the whole program rather than just misbehaving): the C# original
        // only built 255 entries (indices 0x00-0xFE), leaving opcode byte
        // 0xFF with no slot at all. In C# that was harmless - ArrayList
        // bounds-checks, so indexing it with 0xFF threw a normal .NET
        // exception, caught by the same try/catch every other unimplemented
        // opcode already relies on (see NES_CPU::Step()'s NOTE). This port's
        // std::vector, ported 1:1 to the same 255-entry size, does NOT
        // bounds-check operator[] - so a ROM byte of exactly 0xFF fetched as
        // an opcode was undefined behavior (a hard segfault, not a caught
        // exception). $FF is a real, definable opcode (ISC abs,X - see
        // Math::ISC and this file's UnofficialOpcodes()), and every uint8_t
        // value 0x00-0xFF must have a slot for NES_CPU::Step()'s
        // `Assembly.assembly()[opcode]` to ever be safe, so this is fixed by
        // building the full 256-entry table rather than trying to special-
        // case 0xFF.
        for (int i = 0; i <= 0xFF; i++)
            Assembly.push_back([]() { throw NoAssemby(); });
    }

    void AssemblyList::Debug(uint16_t PC, int b)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0X%X: 0X%X", PC, b);
        debug.emplace_back(buf);
        if (debug.size() > 20)
            debug.erase(debug.begin());
    }

    void AssemblyList::NoInput(const Func& func)
    {
        uint16_t PC = NES_Register::PC;
        NES_Register::PC++;
        Debug(PC, 0);
        func();
    }

    void AssemblyList::Input8Byte(const Func8& func)
    {
        uint16_t PC = NES_Register::PC;
        uint8_t b = NES_Memory::Memory[++NES_Register::PC]->Value();
        NES_Register::PC++;
        Debug(PC, b);
        func(b);
    }

    void AssemblyList::Inputs8Byte(const Funcs8& func)
    {
        uint16_t PC = NES_Register::PC;
        int8_t b = static_cast<int8_t>(NES_Memory::Memory[++NES_Register::PC]->Value());
        NES_Register::PC++;
        Debug(PC, b);
        func(b);
    }

    void AssemblyList::Input16Byte(const Func16& func)
    {
        uint16_t PC = NES_Register::PC;
        uint16_t b = NES_Memory::Memory[++NES_Register::PC]->Value();
        b = static_cast<uint16_t>((NES_Memory::Memory[++NES_Register::PC]->Value() << 8) | (b & 0xFF));
        NES_Register::PC++;
        Debug(PC, b);
        func(b);
    }

    // --- Branch: functions with an r (+-127) byte input. ---
    void AssemblyList::BranchToList()
    {
        Assembly[0x90] = [this]() { Inputs8Byte(Assembly_6502::BCC_90); };
        Assembly[0xB0] = [this]() { Inputs8Byte(Assembly_6502::BCS_B0); };
        Assembly[0xF0] = [this]() { Inputs8Byte(Assembly_6502::BEQ_F0); };
        Assembly[0x30] = [this]() { Inputs8Byte(Assembly_6502::BMI_30); };
        Assembly[0xD0] = [this]() { Inputs8Byte(Assembly_6502::BNE_D0); };
        Assembly[0x10] = [this]() { Inputs8Byte(Assembly_6502::BPL_10); };
        Assembly[0x50] = [this]() { Inputs8Byte(Assembly_6502::BVC_50); };
        Assembly[0x70] = [this]() { Inputs8Byte(Assembly_6502::BVS_70); };
    }

    // --- U8: 8-bit input, either a number or a zero page address (0x0022). ---
    void AssemblyList::U8BitToList()
    {
        LoadU8();
        StoreU8();
        ArithmeticU8();
        ShiftRotateU8();
        Logic();
        CompareTestBit();
    }

    void AssemblyList::CompareTestBit() { CMPU8(); CPXU8(); CPYU8(); BITU8(); }
    void AssemblyList::Logic() { ANDU8(); ORAU8(); EORU8(); }
    void AssemblyList::ShiftRotateU8() { ASLU8(); LSRU8(); ROLU8(); RORU8(); }
    void AssemblyList::ArithmeticU8() { ADCU8(); SBCU8(); INCU8(); DECU8(); }
    void AssemblyList::StoreU8() { STAU8(); STXU8(); STYU8(); }
    void AssemblyList::LoadU8() { LDAU8(); LDXU8(); LDYU8(); }

    void AssemblyList::BITU8()
    {
        Assembly[0x89] = [this]() { Input8Byte(Assembly_6502::BIT_89); };
        Assembly[0x24] = [this]() { Input8Byte(Assembly_6502::BIT_24); };
    }

    void AssemblyList::CPYU8()
    {
        Assembly[0xC0] = [this]() { Input8Byte(Assembly_6502::CPY_C0); };
        Assembly[0xC4] = [this]() { Input8Byte(Assembly_6502::CPY_C4); };
    }

    void AssemblyList::CPXU8()
    {
        Assembly[0xE0] = [this]() { Input8Byte(Assembly_6502::CPX_E0); };
        Assembly[0xE4] = [this]() { Input8Byte(Assembly_6502::CPX_E4); };
    }

    void AssemblyList::CMPU8()
    {
        Assembly[0xC5] = [this]() { Input8Byte(Assembly_6502::CMP_C5); };
        Assembly[0xC1] = [this]() { Input8Byte(Assembly_6502::CMP_C1); };
        Assembly[0xC9] = [this]() { Input8Byte(Assembly_6502::CMP_C9); };
        Assembly[0xD5] = [this]() { Input8Byte(Assembly_6502::CMP_D5); };
        Assembly[0xD1] = [this]() { Input8Byte(Assembly_6502::CMP_D1); };
    }

    void AssemblyList::EORU8()
    {
        Assembly[0x45] = [this]() { Input8Byte(Assembly_6502::EOR_45); };
        Assembly[0x41] = [this]() { Input8Byte(Assembly_6502::EOR_41); };
        Assembly[0x49] = [this]() { Input8Byte(Assembly_6502::EOR_49); };
        Assembly[0x55] = [this]() { Input8Byte(Assembly_6502::EOR_55); };
        Assembly[0x51] = [this]() { Input8Byte(Assembly_6502::EOR_51); };
    }

    void AssemblyList::ORAU8()
    {
        Assembly[0x09] = [this]() { Input8Byte(Assembly_6502::ORA_09); };
        Assembly[0x05] = [this]() { Input8Byte(Assembly_6502::ORA_05); };
        // FIXED (was a preserved C# bug, now corrected - found via
        // nestest.nes): the C# original (CPU/CPU/AssemblyList.cs) wired
        // ORA_01 (opcode $01, "ORA (zp,X)") into table slot 0x21 instead of
        // 0x01 - a copy-paste typo (0x21 is really AND (zp,X)'s slot, whose
        // own correct registration two lines below then silently overwrote
        // this one). Net effect: opcode $01 had no handler at all (any ROM
        // executing it hit this port's NoAssemby fallback and silently
        // stalled - see NES_CPU::Step()'s catch block), while ORA_01 itself
        // was simply never reachable from any opcode byte.
        Assembly[0x01] = [this]() { Input8Byte(Assembly_6502::ORA_01); };
        // FIXED (was a preserved C# bug, now corrected - found via
        // nestest.nes, same copy-paste-typo pattern as opcode $01 above):
        // ORA_15 ($15, "ORA zp,X") and ORA_11 ($11, "ORA (zp),Y") were both
        // wired into AND's slots (each exactly +0x20, AND's column in the
        // opcode matrix) instead of their own, silently overwritten by the
        // correct AND_35/AND_31 registrations two lines below and leaving
        // $15/$11 unimplemented.
        Assembly[0x15] = [this]() { Input8Byte(Assembly_6502::ORA_15); };
        Assembly[0x11] = [this]() { Input8Byte(Assembly_6502::ORA_11); };
    }

    void AssemblyList::ANDU8()
    {
        Assembly[0x25] = [this]() { Input8Byte(Assembly_6502::AND_25); };
        Assembly[0x21] = [this]() { Input8Byte(Assembly_6502::AND_21); };
        Assembly[0x29] = [this]() { Input8Byte(Assembly_6502::AND_29); };
        Assembly[0x35] = [this]() { Input8Byte(Assembly_6502::AND_35); };
        Assembly[0x31] = [this]() { Input8Byte(Assembly_6502::AND_31); };
    }

    void AssemblyList::RORU8()
    {
        Assembly[0x66] = [this]() { Input8Byte(Assembly_6502::ROR_66); };
        Assembly[0x76] = [this]() { Input8Byte(Assembly_6502::ROR_76); };
    }

    void AssemblyList::ROLU8()
    {
        Assembly[0x26] = [this]() { Input8Byte(Assembly_6502::ROL_26); };
        Assembly[0x36] = [this]() { Input8Byte(Assembly_6502::ROL_36); };
    }

    void AssemblyList::LSRU8()
    {
        Assembly[0x46] = [this]() { Input8Byte(Assembly_6502::LSR_46); };
        Assembly[0x56] = [this]() { Input8Byte(Assembly_6502::LSR_56); };
    }

    void AssemblyList::ASLU8()
    {
        Assembly[0x06] = [this]() { Input8Byte(Assembly_6502::ASL_06); };
        Assembly[0x16] = [this]() { Input8Byte(Assembly_6502::ASL_16); };
    }

    void AssemblyList::DECU8()
    {
        Assembly[0xC6] = [this]() { Input8Byte(Assembly_6502::DEC_C6); };
        Assembly[0xD6] = [this]() { Input8Byte(Assembly_6502::DEC_D6); };
    }

    void AssemblyList::INCU8()
    {
        Assembly[0xE6] = [this]() { Input8Byte(Assembly_6502::INC_E6); };
        Assembly[0xF6] = [this]() { Input8Byte(Assembly_6502::INC_F6); };
    }

    void AssemblyList::SBCU8()
    {
        Assembly[0xE9] = [this]() { Input8Byte(Assembly_6502::SBC_E9); };
        Assembly[0xE5] = [this]() { Input8Byte(Assembly_6502::SBC_E5); };
        Assembly[0xE1] = [this]() { Input8Byte(Assembly_6502::SBC_E1); };
        Assembly[0xF5] = [this]() { Input8Byte(Assembly_6502::SBC_F5); };
        Assembly[0xF1] = [this]() { Input8Byte(Assembly_6502::SBC_F1); };
    }

    void AssemblyList::ADCU8()
    {
        Assembly[0x69] = [this]() { Input8Byte(Assembly_6502::ADC_69); };
        Assembly[0x65] = [this]() { Input8Byte(Assembly_6502::ADC_65); };
        Assembly[0x61] = [this]() { Input8Byte(Assembly_6502::ADC_61); };
        Assembly[0x75] = [this]() { Input8Byte(Assembly_6502::ADC_75); };
        Assembly[0x71] = [this]() { Input8Byte(Assembly_6502::ADC_71); };
    }

    void AssemblyList::STYU8()
    {
        Assembly[0x84] = [this]() { Input8Byte(Assembly_6502::STY_84); };
        Assembly[0x94] = [this]() { Input8Byte(Assembly_6502::STY_94); };
    }

    void AssemblyList::STXU8()
    {
        Assembly[0x86] = [this]() { Input8Byte(Assembly_6502::STX_86); };
        Assembly[0x96] = [this]() { Input8Byte(Assembly_6502::STX_96); };
    }

    void AssemblyList::STAU8()
    {
        Assembly[0x85] = [this]() { Input8Byte(Assembly_6502::STA_85); };
        Assembly[0x81] = [this]() { Input8Byte(Assembly_6502::STA_81); };
        Assembly[0x95] = [this]() { Input8Byte(Assembly_6502::STA_95); };
        Assembly[0x91] = [this]() { Input8Byte(Assembly_6502::STA_91); };
    }

    void AssemblyList::LDYU8()
    {
        Assembly[0xA0] = [this]() { Input8Byte(Assembly_6502::LDY_A0); };
        Assembly[0xA4] = [this]() { Input8Byte(Assembly_6502::LDY_A4); };
        Assembly[0xB4] = [this]() { Input8Byte(Assembly_6502::LDY_B4); };
    }

    void AssemblyList::LDXU8()
    {
        Assembly[0xA2] = [this]() { Input8Byte(Assembly_6502::LDX_A2); };
        Assembly[0xA6] = [this]() { Input8Byte(Assembly_6502::LDX_A6); };
        Assembly[0xB6] = [this]() { Input8Byte(Assembly_6502::LDX_B6); };
    }

    void AssemblyList::LDAU8()
    {
        Assembly[0xA9] = [this]() { Input8Byte(Assembly_6502::LDA_A9); };
        Assembly[0xA5] = [this]() { Input8Byte(Assembly_6502::LDA_A5); };
        Assembly[0xA1] = [this]() { Input8Byte(Assembly_6502::LDA_A1); };
        Assembly[0xB5] = [this]() { Input8Byte(Assembly_6502::LDA_B5); };
        Assembly[0xB1] = [this]() { Input8Byte(Assembly_6502::LDA_B1); };
    }

    // --- U16: 16-bit input, mostly full addresses. ---
    void AssemblyList::U16ByteToList()
    {
        LoadU16();
        StoreU16();
        ArithmeticU16();
        ShiftRotateU16();
        LogicU16();
        CMPU16();
        CompareTestBitU16();
        JumpU16();
    }

    void AssemblyList::LogicU16() { ANDU16(); ORAU16(); EORU16(); }
    void AssemblyList::ShiftRotateU16() { ASLU16(); LSRU16(); ROLU16(); RORU16(); }
    void AssemblyList::ArithmeticU16() { ADCU16(); SBCU16(); INCU16(); DECU16(); }
    void AssemblyList::StoreU16() { STAU16(); STXYU16(); }
    void AssemblyList::LoadU16() { LDAU16(); LDXU16(); LDYU16(); }

    void AssemblyList::JumpU16()
    {
        Assembly[0x4C] = [this]() { Input16Byte(Assembly_6502::JMP_4C); };
        Assembly[0x6C] = [this]() { Input16Byte(Assembly_6502::JMP_6C); };
        Assembly[0x20] = [this]() { Input16Byte(Assembly_6502::JSR_20); };
    }

    void AssemblyList::CompareTestBitU16()
    {
        Assembly[0xEC] = [this]() { Input16Byte(Assembly_6502::CPX_EC); };
        Assembly[0xCC] = [this]() { Input16Byte(Assembly_6502::CPY_CC); };
        Assembly[0x2C] = [this]() { Input16Byte(Assembly_6502::BIT_2C); };
    }

    void AssemblyList::CMPU16()
    {
        Assembly[0xCD] = [this]() { Input16Byte(Assembly_6502::CMP_CD); };
        Assembly[0xDD] = [this]() { Input16Byte(Assembly_6502::CMP_DD); };
        Assembly[0xD9] = [this]() { Input16Byte(Assembly_6502::CMP_D9); };
    }

    void AssemblyList::EORU16()
    {
        Assembly[0x4D] = [this]() { Input16Byte(Assembly_6502::EOR_4D); };
        Assembly[0x5D] = [this]() { Input16Byte(Assembly_6502::EOR_5D); };
        Assembly[0x59] = [this]() { Input16Byte(Assembly_6502::EOR_59); };
    }

    void AssemblyList::ORAU16()
    {
        Assembly[0x0D] = [this]() { Input16Byte(Assembly_6502::ORA_0D); };
        Assembly[0x1D] = [this]() { Input16Byte(Assembly_6502::ORA_1D); };
        Assembly[0x19] = [this]() { Input16Byte(Assembly_6502::ORA_19); };
    }

    void AssemblyList::ANDU16()
    {
        Assembly[0x2D] = [this]() { Input16Byte(Assembly_6502::AND_2D); };
        Assembly[0x3D] = [this]() { Input16Byte(Assembly_6502::AND_3D); };
        Assembly[0x39] = [this]() { Input16Byte(Assembly_6502::AND_39); };
    }

    void AssemblyList::RORU16()
    {
        Assembly[0x6E] = [this]() { Input16Byte(Assembly_6502::ROR_6E); };
        Assembly[0x7E] = [this]() { Input16Byte(Assembly_6502::ROR_7E); };
    }

    void AssemblyList::ROLU16()
    {
        Assembly[0x2E] = [this]() { Input16Byte(Assembly_6502::ROL_2E); };
        Assembly[0x3E] = [this]() { Input16Byte(Assembly_6502::ROL_3E); };
    }

    void AssemblyList::LSRU16()
    {
        Assembly[0x4E] = [this]() { Input16Byte(Assembly_6502::LSR_4E); };
        Assembly[0x5E] = [this]() { Input16Byte(Assembly_6502::LSR_5E); };
    }

    void AssemblyList::ASLU16()
    {
        Assembly[0x0E] = [this]() { Input16Byte(Assembly_6502::ASL_0E); };
        Assembly[0x1E] = [this]() { Input16Byte(Assembly_6502::ASL_1E); };
    }

    void AssemblyList::DECU16()
    {
        Assembly[0xCE] = [this]() { Input16Byte(Assembly_6502::DEC_CE); };
        Assembly[0xDE] = [this]() { Input16Byte(Assembly_6502::DEC_DE); };
    }

    void AssemblyList::INCU16()
    {
        Assembly[0xEE] = [this]() { Input16Byte(Assembly_6502::INC_EE); };
        Assembly[0xFE] = [this]() { Input16Byte(Assembly_6502::INC_FE); };
    }

    void AssemblyList::SBCU16()
    {
        Assembly[0xED] = [this]() { Input16Byte(Assembly_6502::SBC_ED); };
        Assembly[0xFD] = [this]() { Input16Byte(Assembly_6502::SBC_FD); };
        Assembly[0xF9] = [this]() { Input16Byte(Assembly_6502::SBC_F9); };
    }

    void AssemblyList::ADCU16()
    {
        Assembly[0x6D] = [this]() { Input16Byte(Assembly_6502::ADC_6D); };
        Assembly[0x7D] = [this]() { Input16Byte(Assembly_6502::ADC_7D); };
        Assembly[0x79] = [this]() { Input16Byte(Assembly_6502::ADC_79); };
    }

    void AssemblyList::STXYU16()
    {
        Assembly[0x8E] = [this]() { Input16Byte(Assembly_6502::STX_8E); };
        Assembly[0x8C] = [this]() { Input16Byte(Assembly_6502::STY_8C); };
    }

    void AssemblyList::STAU16()
    {
        Assembly[0x8D] = [this]() { Input16Byte(Assembly_6502::STA_8D); };
        Assembly[0x9D] = [this]() { Input16Byte(Assembly_6502::STA_9D); };
        Assembly[0x99] = [this]() { Input16Byte(Assembly_6502::STA_99); };
    }

    void AssemblyList::LDYU16()
    {
        Assembly[0xAC] = [this]() { Input16Byte(Assembly_6502::LDY_AC); };
        Assembly[0xBC] = [this]() { Input16Byte(Assembly_6502::LDY_BC); };
    }

    void AssemblyList::LDXU16()
    {
        Assembly[0xAE] = [this]() { Input16Byte(Assembly_6502::LDX_AE); };
        Assembly[0xBE] = [this]() { Input16Byte(Assembly_6502::LDX_BE); };
    }

    void AssemblyList::LDAU16()
    {
        Assembly[0xAD] = [this]() { Input16Byte(Assembly_6502::LDA_AD); };
        Assembly[0xBD] = [this]() { Input16Byte(Assembly_6502::LDA_BD); };
        Assembly[0xB9] = [this]() { Input16Byte(Assembly_6502::LDA_B9); };
    }

    // --- No input: implied/accumulator-addressed instructions. ---
    void AssemblyList::NoImputToList()
    {
        Transfer();
        Miscellaneous();
        Stack();
        Clear();
        Set();
        Return();
        IncrementDecrement();
        Logical();
    }

    void AssemblyList::Logical()
    {
        Assembly[0x0A] = [this]() { NoInput(Assembly_6502::ASL_0A); };
        Assembly[0x4A] = [this]() { NoInput(Assembly_6502::LSR_4A); };
        Assembly[0x2A] = [this]() { NoInput(Assembly_6502::ROL_2A); };
        Assembly[0x6A] = [this]() { NoInput(Assembly_6502::ROR_6A); };
    }

    void AssemblyList::IncrementDecrement()
    {
        Assembly[0xCA] = [this]() { NoInput(Assembly_6502::DEX_CA); };
        Assembly[0x88] = [this]() { NoInput(Assembly_6502::DEY_88); };
        Assembly[0xE8] = [this]() { NoInput(Assembly_6502::INX_E8); };
        Assembly[0xC8] = [this]() { NoInput(Assembly_6502::INY_C8); };
    }

    void AssemblyList::Return()
    {
        Assembly[0x40] = [this]() { NoInput(Assembly_6502::RTI_40); };
        Assembly[0x60] = [this]() { NoInput(Assembly_6502::RTS_60); };
    }

    void AssemblyList::Set()
    {
        Assembly[0x38] = [this]() { NoInput(Assembly_6502::SEC_38); };
        Assembly[0x78] = [this]() { NoInput(Assembly_6502::SEI_78); };
        Assembly[0xF8] = [this]() { NoInput(Assembly_6502::SED_F8); };
    }

    void AssemblyList::Clear()
    {
        Assembly[0x18] = [this]() { NoInput(Assembly_6502::CLC_18); };
        Assembly[0x58] = [this]() { NoInput(Assembly_6502::CLI_58); };
        Assembly[0xD8] = [this]() { NoInput(Assembly_6502::CLD_D8); };
        Assembly[0xB8] = [this]() { NoInput(Assembly_6502::CLV_B8); };
    }

    void AssemblyList::Stack()
    {
        Assembly[0x28] = [this]() { NoInput(Assembly_6502::PLP_28); };
        Assembly[0x08] = [this]() { NoInput(Assembly_6502::PHP_08); };
        Assembly[0x48] = [this]() { NoInput(Assembly_6502::PHA_48); };
        Assembly[0x68] = [this]() { NoInput(Assembly_6502::PLA_68); };
    }

    void AssemblyList::Miscellaneous()
    {
        Assembly[0x00] = [this]() { NoInput(Assembly_6502::BRK_00); };
        Assembly[0xEA] = [this]() { NoInput(Assembly_6502::NOP_EA); };
        UnofficialNOPs();
        UnofficialOpcodes();
    }

    // NEW: not a port of anything - see UnofficialNOPs()'s NOTE above for
    // why these exist. Unlike the NOPs, these unofficial opcodes actually do
    // something (combine two official operations, or use undocumented
    // internal CPU bus behavior), so Galaga's boot code getting past $04/$0B
    // correctly depends on them actually computing the right result, not
    // just consuming the right number of bytes. Implements the "stable"
    // unofficial opcodes (http://wiki.nesdev.com/w/index.php/
    // Programming_with_unofficial_opcodes) - deliberately excludes the
    // handful of well-known *unstable* ones (LAX #imm/$AB, XAA/$8B, the
    // SHA/SHX/SHY/TAS/AHX family) whose real-hardware behavior varies by
    // console/temperature and that no real software deliberately relies on.
    void AssemblyList::UnofficialOpcodes()
    {
        // LAX: LDA+TAX combined.
        Assembly[0xA3] = [this]() { Input8Byte([](uint8_t zp) { Math::LAX(NES_Memory::Memory[Parameter::zpx1(zp)]->Value()); }); };
        Assembly[0xA7] = [this]() { Input8Byte([](uint8_t zp) { Math::LAX(NES_Memory::Memory[zp]->Value()); }); };
        Assembly[0xAF] = [this]() { Input16Byte([](uint16_t a) { Math::LAX(NES_Memory::Memory[a]->Value()); }); };
        Assembly[0xB3] = [this]() { Input8Byte([](uint8_t zp) { Math::LAX(NES_Memory::Memory[Parameter::zpy1(zp)]->Value()); }); };
        Assembly[0xB7] = [this]() { Input8Byte([](uint8_t zp) { Math::LAX(NES_Memory::Memory[Parameter::zpy2(zp)]->Value()); }); };
        Assembly[0xBF] = [this]() { Input16Byte([](uint16_t a) { Math::LAX(NES_Memory::Memory[Parameter::ay(a)]->Value()); }); };

        // SAX: stores A & X.
        Assembly[0x83] = [this]() { Input8Byte([](uint8_t zp) { Math::SAX(Parameter::zpx1(zp)); }); };
        Assembly[0x87] = [this]() { Input8Byte([](uint8_t zp) { Math::SAX(zp); }); };
        Assembly[0x8F] = [this]() { Input16Byte([](uint16_t a) { Math::SAX(a); }); };
        Assembly[0x97] = [this]() { Input8Byte([](uint8_t zp) { Math::SAX(Parameter::zpy2(zp)); }); };

        // DCP/ISC/SLO/RLA/SRE/RRA: read-modify-write combo ops, all sharing
        // the same 7-addressing-mode layout (d,x)/d/a/(d),y/d,x/a,y/a,x.
        Assembly[0xC3] = [this]() { Input8Byte([](uint8_t zp) { Math::DCP(Parameter::zpx1(zp)); }); };
        Assembly[0xC7] = [this]() { Input8Byte([](uint8_t zp) { Math::DCP(zp); }); };
        Assembly[0xCF] = [this]() { Input16Byte([](uint16_t a) { Math::DCP(a); }); };
        Assembly[0xD3] = [this]() { Input8Byte([](uint8_t zp) { Math::DCP(Parameter::zpy1(zp)); }); };
        Assembly[0xD7] = [this]() { Input8Byte([](uint8_t zp) { Math::DCP(Parameter::zpx2(zp)); }); };
        Assembly[0xDB] = [this]() { Input16Byte([](uint16_t a) { Math::DCP(Parameter::ay(a)); }); };
        Assembly[0xDF] = [this]() { Input16Byte([](uint16_t a) { Math::DCP(Parameter::ax(a)); }); };

        Assembly[0xE3] = [this]() { Input8Byte([](uint8_t zp) { Math::ISC(Parameter::zpx1(zp)); }); };
        Assembly[0xE7] = [this]() { Input8Byte([](uint8_t zp) { Math::ISC(zp); }); };
        Assembly[0xEF] = [this]() { Input16Byte([](uint16_t a) { Math::ISC(a); }); };
        Assembly[0xF3] = [this]() { Input8Byte([](uint8_t zp) { Math::ISC(Parameter::zpy1(zp)); }); };
        Assembly[0xF7] = [this]() { Input8Byte([](uint8_t zp) { Math::ISC(Parameter::zpx2(zp)); }); };
        Assembly[0xFB] = [this]() { Input16Byte([](uint16_t a) { Math::ISC(Parameter::ay(a)); }); };
        // See CreateAdressMemory()'s NOTE: opcode $FF now has a real slot
        // (the table was fixed to 256 entries), so this can be registered
        // like every other addressing-mode variant above.
        Assembly[0xFF] = [this]() { Input16Byte([](uint16_t a) { Math::ISC(Parameter::ax(a)); }); };

        Assembly[0x03] = [this]() { Input8Byte([](uint8_t zp) { Math::SLO(Parameter::zpx1(zp)); }); };
        Assembly[0x07] = [this]() { Input8Byte([](uint8_t zp) { Math::SLO(zp); }); };
        Assembly[0x0F] = [this]() { Input16Byte([](uint16_t a) { Math::SLO(a); }); };
        Assembly[0x13] = [this]() { Input8Byte([](uint8_t zp) { Math::SLO(Parameter::zpy1(zp)); }); };
        Assembly[0x17] = [this]() { Input8Byte([](uint8_t zp) { Math::SLO(Parameter::zpx2(zp)); }); };
        Assembly[0x1B] = [this]() { Input16Byte([](uint16_t a) { Math::SLO(Parameter::ay(a)); }); };
        Assembly[0x1F] = [this]() { Input16Byte([](uint16_t a) { Math::SLO(Parameter::ax(a)); }); };

        Assembly[0x23] = [this]() { Input8Byte([](uint8_t zp) { Math::RLA(Parameter::zpx1(zp)); }); };
        Assembly[0x27] = [this]() { Input8Byte([](uint8_t zp) { Math::RLA(zp); }); };
        Assembly[0x2F] = [this]() { Input16Byte([](uint16_t a) { Math::RLA(a); }); };
        Assembly[0x33] = [this]() { Input8Byte([](uint8_t zp) { Math::RLA(Parameter::zpy1(zp)); }); };
        Assembly[0x37] = [this]() { Input8Byte([](uint8_t zp) { Math::RLA(Parameter::zpx2(zp)); }); };
        Assembly[0x3B] = [this]() { Input16Byte([](uint16_t a) { Math::RLA(Parameter::ay(a)); }); };
        Assembly[0x3F] = [this]() { Input16Byte([](uint16_t a) { Math::RLA(Parameter::ax(a)); }); };

        Assembly[0x43] = [this]() { Input8Byte([](uint8_t zp) { Math::SRE(Parameter::zpx1(zp)); }); };
        Assembly[0x47] = [this]() { Input8Byte([](uint8_t zp) { Math::SRE(zp); }); };
        Assembly[0x4F] = [this]() { Input16Byte([](uint16_t a) { Math::SRE(a); }); };
        Assembly[0x53] = [this]() { Input8Byte([](uint8_t zp) { Math::SRE(Parameter::zpy1(zp)); }); };
        Assembly[0x57] = [this]() { Input8Byte([](uint8_t zp) { Math::SRE(Parameter::zpx2(zp)); }); };
        Assembly[0x5B] = [this]() { Input16Byte([](uint16_t a) { Math::SRE(Parameter::ay(a)); }); };
        Assembly[0x5F] = [this]() { Input16Byte([](uint16_t a) { Math::SRE(Parameter::ax(a)); }); };

        Assembly[0x63] = [this]() { Input8Byte([](uint8_t zp) { Math::RRA(Parameter::zpx1(zp)); }); };
        Assembly[0x67] = [this]() { Input8Byte([](uint8_t zp) { Math::RRA(zp); }); };
        Assembly[0x6F] = [this]() { Input16Byte([](uint16_t a) { Math::RRA(a); }); };
        Assembly[0x73] = [this]() { Input8Byte([](uint8_t zp) { Math::RRA(Parameter::zpy1(zp)); }); };
        Assembly[0x77] = [this]() { Input8Byte([](uint8_t zp) { Math::RRA(Parameter::zpx2(zp)); }); };
        Assembly[0x7B] = [this]() { Input16Byte([](uint16_t a) { Math::RRA(Parameter::ay(a)); }); };
        Assembly[0x7F] = [this]() { Input16Byte([](uint16_t a) { Math::RRA(Parameter::ax(a)); }); };

        // Immediate-mode unofficial opcodes.
        Assembly[0x0B] = [this]() { Input8Byte([](uint8_t v) { Math::ANC(v); }); };
        Assembly[0x2B] = [this]() { Input8Byte([](uint8_t v) { Math::ANC(v); }); };
        Assembly[0x4B] = [this]() { Input8Byte([](uint8_t v) { Math::ALR(v); }); };
        Assembly[0x6B] = [this]() { Input8Byte([](uint8_t v) { Math::ARR(v); }); };
        Assembly[0xCB] = [this]() { Input8Byte([](uint8_t v) { Math::SBX(v); }); };
        // $EB is a documented duplicate encoding of official SBC #imm ($E9).
        Assembly[0xEB] = [this]() { Input8Byte(Assembly_6502::SBC_E9); };
    }

    // NEW: not a port of anything - neither this port nor the C# original
    // implemented any of the 6502's well-known "unofficial"/"illegal"
    // opcodes at all (every one of them fell through to the NoAssemby
    // catch-all - see CreateAdressMemory() above), which was harmless as
    // long as a ROM's actual executed code path never happened to hit one.
    // Once the official-opcode bugs found via nestest.nes (see the FIXED
    // notes throughout this file and Math.cpp/Status.cpp/Assembly_6502.cpp)
    // were corrected, Galaga's boot/reset code turned out to genuinely
    // execute opcode $04 as part of its warm-up wait - a documented,
    // widespread pattern (Nintendo's own official reset boilerplate uses
    // unofficial NOPs for exact cycle padding), previously never reached
    // because the CPU was taking a different, incorrect path through the
    // same code. Without a handler, that opcode threw NoAssemby every single
    // time PC landed there - caught and silently ignored by NES_CPU::Step()
    // (see its NOTE), but critically *never advancing PC*, so execution was
    // stuck retrying the same instruction forever: a hang that looked worse
    // than the earlier bugs specifically because the CPU is now correct
    // enough to reach it. This implements the complete standard set of
    // unofficial NOPs (http://wiki.nesdev.com/w/index.php/CPU_unofficial_opcodes)
    // - all are genuine no-ops on real hardware, they just consume 1 or 2
    // operand bytes (still fetched from memory, for correct PC advancement
    // and because the fetch itself can have side effects via memory-mapped
    // I/O, though none of the ones below happen to hit any) depending on
    // addressing mode.
    void AssemblyList::UnofficialNOPs()
    {
        auto nop0 = [this]() { NoInput([]() {}); };
        auto nop1 = [this]() { Input8Byte([](uint8_t) {}); };
        auto nop2 = [this]() { Input16Byte([](uint16_t) {}); };

        // Implied (1 byte total, matching official NOP $EA).
        for (uint8_t op : { 0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA })
            Assembly[op] = nop0;
        // Immediate / zero page / zero page,X (2 bytes total).
        for (uint8_t op : { 0x80, 0x82, 0x89, 0xC2, 0xE2, 0x04, 0x44, 0x64,
                             0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4 })
            Assembly[op] = nop1;
        // Absolute / absolute,X (3 bytes total).
        for (uint8_t op : { 0x0C, 0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC })
            Assembly[op] = nop2;
    }

    void AssemblyList::Transfer()
    {
        Assembly[0x8A] = [this]() { NoInput(Assembly_6502::TXA_8A); };
        Assembly[0x98] = [this]() { NoInput(Assembly_6502::TYA_98); };
        Assembly[0x9A] = [this]() { NoInput(Assembly_6502::TXS_9A); };
        Assembly[0xA8] = [this]() { NoInput(Assembly_6502::TAY_A8); };
        Assembly[0xAA] = [this]() { NoInput(Assembly_6502::TAX_AA); };
        Assembly[0xBA] = [this]() { NoInput(Assembly_6502::TSX_BA); };
    }
}
