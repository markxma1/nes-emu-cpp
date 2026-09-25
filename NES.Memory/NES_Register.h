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
/// https://en.wikibooks.org/wiki/NES_Programming/Introduction
/// http://wiki.nesdev.com/w/index.php/CPU_status_flag_behavior
/// http://wiki.nesdev.com/w/index.php/PPU_registers
#pragma once
#include <cstdint>
#include <string>

namespace NES
{
    /// @brief 6502 processor status flags, packed into one byte.
    ///
    /// Bit layout (low to high):
    /// C(0) Z(1) I(2) D(3) B(4) U(5) V(6) N(7) - see
    /// http://wiki.nesdev.com/w/index.php/Status_flags
    struct PFlags
    {
        /// The packed status byte.
        uint8_t P = 0;

        /// Carry flag (bit 0): unsigned overflow out of bit 7, or shifted-out bit.
        bool Carry() const { return (P & 0x1) > 0; }
        /// Sets or clears the carry flag.
        void Carry(bool v) { P = static_cast<uint8_t>(P & ~0x1); if (v) P = static_cast<uint8_t>(P | 0x1); }

        /// Zero flag (bit 1): last result was zero.
        bool Zero() const { return (P & 0x2) > 0; }
        /// Sets or clears the zero flag.
        void Zero(bool v) { P = static_cast<uint8_t>(P & ~0x2); if (v) P = static_cast<uint8_t>(P | 0x2); }

        /// Interrupt-disable flag (bit 2): while set, IRQ is ignored.
        bool Interrupt() const { return (P & 0x4) > 0; }
        /// Sets or clears the interrupt-disable flag.
        void Interrupt(bool v) { P = static_cast<uint8_t>(P & ~0x4); if (v) P = static_cast<uint8_t>(P | 0x4); }

        /// Decimal-mode flag (bit 3); stored, but the NES 2A03 has no decimal arithmetic.
        bool Decimal() const { return (P & 0x8) > 0; }
        /// Sets or clears the decimal flag.
        void Decimal(bool v) { P = static_cast<uint8_t>(P & ~0x8); if (v) P = static_cast<uint8_t>(P | 0x8); }

        /// Break flag (bit 4): only meaningful in the copy of P pushed to the stack.
        bool B() const { return (P & 0x10) > 0; }
        /// Sets or clears the break flag.
        void B(bool v) { P = static_cast<uint8_t>(P & ~0x10); if (v) P = static_cast<uint8_t>(P | 0x10); }

        /// Unused flag (bit 5): always 1 when P is pushed.
        bool U() const { return (P & 0x20) > 0; }
        /// Sets or clears the unused flag.
        void U(bool v) { P = static_cast<uint8_t>(P & ~0x20); if (v) P = static_cast<uint8_t>(P | 0x20); }

        /// Overflow flag (bit 6): signed overflow of the last ADC/SBC.
        bool Overflow() const { return (P & 0x40) > 0; }
        /// Sets or clears the overflow flag.
        void Overflow(bool v) { P = static_cast<uint8_t>(P & ~0x40); if (v) P = static_cast<uint8_t>(P | 0x40); }

        /// Negative flag (bit 7): copy of bit 7 of the last result.
        bool Negative() const { return (P & 0x80) > 0; }
        /// Sets or clears the negative flag.
        void Negative(bool v) { P = static_cast<uint8_t>(P & ~0x80); if (v) P = static_cast<uint8_t>(P | 0x80); }

        /// Formats the flags as text for debugging and trace output.
        std::string ToString() const;
    };

    /// @brief The 6502 CPU registers (A, X, Y, P, S, PC).
    ///
    /// Kept as static members rather than an
    /// instance, since the whole emulator only ever has one CPU.
    class NES_Register
    {
    public:
        /// Accumulator: handles all arithmetic and logic.
        static uint8_t A;
        /// Index registers with limited capabilities.
        static uint8_t X;
        /// Second index register (see X).
        static uint8_t Y;

        /// Processor status: flags holding the results of tests and CPU state.
        static PFlags P;
        /// Stack pointer.
        static uint8_t S;
        /// Program counter: address of the current CPU instruction.
        static uint16_t PC;

        /// Loads PC from the reset vector at $FFFC/$FFFD.
        /// http://wiki.nesdev.com/w/index.php/CPU_power_up_state
        static void RessetPointer();
    };
}
