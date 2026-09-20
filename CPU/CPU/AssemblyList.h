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
#include <functional>
#include <string>
#include <vector>

namespace NES
{
    /// @brief The 6502 opcode dispatch table: maps each opcode byte to a
    /// wrapper that reads its operand bytes (if any) from memory, advances PC
    /// past them, and calls the matching Assembly_6502 instruction.
    /// Port of CPU/CPU/AssemblyList.cs. See https://en.wikibooks.org/wiki/6502_Assembly
    /// and http://nesdev.com/6502.txt for the opcode table this mirrors.
    class AssemblyList
    {
    public:
        using Func = std::function<void()>;

        static std::vector<std::string> debug;

        AssemblyList();

        const std::vector<Func>& assembly() const { return Assembly; }
        std::vector<Func>& assembly() { return Assembly; }

    private:
        static std::vector<Func> Assembly;

        static void CreateAdressMemory();
        static void Debug(uint16_t PC, int b);

        using Func8 = std::function<void(uint8_t)>;
        using Funcs8 = std::function<void(int8_t)>;
        using Func16 = std::function<void(uint16_t)>;

        void NoInput(const Func& func);
        void Input8Byte(const Func8& func);
        void Inputs8Byte(const Funcs8& func);
        void Input16Byte(const Func16& func);

        void BranchToList();

        void U8BitToList();
        void CompareTestBit();
        void Logic();
        void ShiftRotateU8();
        void ArithmeticU8();
        void StoreU8();
        void LoadU8();
        void BITU8();
        void CPYU8();
        void CPXU8();
        void CMPU8();
        void EORU8();
        void ORAU8();
        void ANDU8();
        void RORU8();
        void ROLU8();
        void LSRU8();
        void ASLU8();
        void DECU8();
        void INCU8();
        void SBCU8();
        void ADCU8();
        void STYU8();
        void STXU8();
        void STAU8();
        void LDYU8();
        void LDXU8();
        void LDAU8();

        void U16ByteToList();
        void LogicU16();
        void ShiftRotateU16();
        void ArithmeticU16();
        void StoreU16();
        void LoadU16();
        void JumpU16();
        void CompareTestBitU16();
        void CMPU16();
        void EORU16();
        void ORAU16();
        void ANDU16();
        void RORU16();
        void ROLU16();
        void LSRU16();
        void ASLU16();
        void DECU16();
        void INCU16();
        void SBCU16();
        void ADCU16();
        void STXYU16();
        void STAU16();
        void LDYU16();
        void LDXU16();
        void LDAU16();

        void NoImputToList();
        void Logical();
        void IncrementDecrement();
        void Return();
        void Set();
        void Clear();
        void Stack();
        void Miscellaneous();
        void Transfer();
        /// New, not a port - see AssemblyList.cpp's NOTE on Miscellaneous().
        void UnofficialNOPs();
        /// New, not a port - see AssemblyList.cpp's NOTE above its definition.
        void UnofficialOpcodes();
    };
}
