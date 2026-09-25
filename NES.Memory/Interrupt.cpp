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
#include "EnvFlag.h"
#include "Interrupt.h"
#include "NES_PPU.h"
#include "NES_CPU.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"
#include "Stack.h"
#include <cstdlib>
#include <iostream>

namespace NES
{
    std::atomic<bool> Interrupt::nmi = false;
    std::atomic<bool> Interrupt::irq = false;
    std::atomic<bool> Interrupt::brk = false;
    std::atomic<bool> Interrupt::nmiSuppressed = false;
    std::atomic<bool> Interrupt::POWER = true;
    std::atomic<bool> Interrupt::RESET = false;
    int Interrupt::irqSevenClock = 7;
    int Interrupt::brkSevenClock = 7;
    int Interrupt::nmiSevenClock = 7;
    bool Interrupt::nmiInProgress = false;
    bool Interrupt::irqDispatchInProgress = false;
    uint8_t Interrupt::nmiEntryStackPointer = 0;

    int Interrupt::dispatchCycles = 0;
    int Interrupt::nmiDelay = 0;

    int Interrupt::TakeDispatchCycles()
    {
        int c = dispatchCycles;
        dispatchCycles = 0;
        return c;
    }

    void Interrupt::Check(int cycles)
    {
        if (nmiDelay > 0 && --nmiDelay == 0)
            NMI(true);
        isBRK(cycles);
        isNMI(cycles); // NMI has priority over IRQ
        isIRQ(cycles);
        isRESET();
    }

    // An IRQ is taken at the next instruction boundary when the I flag is
    // clear (level-triggered, so the source keeps the line up until it is
    // acknowledged). The 7-cycle interrupt sequence is charged afterwards via
    // TakeDispatchCycles(). The pushed status is the pre-interrupt one; I is
    // set only after the push, so RTI restores the old I flag.
    // http://wiki.nesdev.com/w/index.php/CPU_interrupts
    void Interrupt::isIRQ(int)
    {
        if (!IRQ() || NES_Register::P.Interrupt())
            return;
        ReplacePC(0xfffe, false, true);
        NES_Register::P.Interrupt(true);
        IRQ(false);
        dispatchCycles += 7;
        if (NES_GETENV("NES_TRACE_MMC3_IRQLATCH"))
            std::cerr << "[irqServiced] scanline=" << NES_PPU::CurrentScanline() << " jumped to 0x" << std::hex << NES_Register::PC << std::dec << std::endl;
    }

    // BRK pushes the return address and the status with B set, then sets I
    // (the push comes first, so RTI restores the old I flag).
    void Interrupt::isBRK(int)
    {
        if (!BRK())
            return;
        ReplacePC(0xfffe, true, true);
        NES_Register::P.Interrupt(true);
        BRK(false);
        dispatchCycles += 7;
    }

    // The NMI line is edge-triggered and ignores the I flag: a pending NMI is
    // taken at the next instruction boundary, even inside another handler
    // (it nests). The 7-cycle sequence is charged via TakeDispatchCycles().
    // http://wiki.nesdev.com/w/index.php/CPU_interrupts
    void Interrupt::isNMI(int)
    {
        if (!NMI() || nmiSuppressed)
            return;
        if (NES_GETENV("NES_TRACE_NMI_CYCLES"))
            std::cerr << "NMI cycles=" << NES_CPU::totalCyclesEver.load() << std::endl;
        ReplacePC(0xfffa, false, true);
        NES_Register::P.Interrupt(true);
        NMI(false);
        dispatchCycles += 7;
    }

    void Interrupt::isRESET()
    {
        if (RESET)
        {
            NES_Register::PC = static_cast<uint16_t>(
                NES_Memory::Memory[0xfffc]->Value() | (NES_Memory::Memory[0xfffd]->Value() << 8));
        }
    }

    // FIXED (found via nestest.nes):
    // real 6502 BRK/IRQ/NMI push PCH, then PCL, then the flags byte (P ends
    // up on top) - http://wiki.nesdev.com/w/index.php/CPU_interrupts,
    // http://wiki.nesdev.com/w/index.php/RTI, both of which RTI must pop in
    // the reverse of that order (P first, then PC - see Assembly_6502.cpp's
    // RTI_40). This code previously pushed P
    // *before* PC instead - internally self-consistent with this
    // port's own (equally backwards) RTI pop order at the time, so a BRK/IRQ/NMI
    // followed by this port's own RTI happened to still round-trip
    // correctly, but nestest.nes builds its RTI test stack by hand in the
    // real push order - which the old RTI pop order got completely wrong.
    // Fixed to push in the real order so both this port's own interrupts
    // and a real/reference stack layout resolve correctly via RTI.
    //
    // FIXED (second bug, found by testing the above against Galaga rather
    // than nestest alone): a real interrupt pushes the *exact* PC - no
    // adjustment. Stack::PcToStack() decrements PC first, which is JSR's
    // convention (JSR pushes `return_address - 1` because RTS always adds 1
    // back - see PcToStack()'s own NOTE) - reusing it here silently applied
    // that same "-1" to every interrupt entry too. Before this file's first
    // fix above, that "-1" was invisible: RTI's *old*, wrong pop order
    // happened to always add a
    // compensating "+1" back (the shared, RTS-style StackToPc()), so the
    // net effect cancelled out. Fixing RTI's pop order to match real
    // hardware (this file's first fix, and Assembly_6502.cpp's RTI_40 -
    // which correctly stopped adding that "+1", since a genuine interrupt
    // push needs none) removed that accidental cancellation, leaving this
    // "-1" with nothing to compensate for it - every interrupt return
    // landed one byte before where it should have, letting the CPU
    // misinterpret the tail of an unrelated instruction as a fresh opcode.
    // Fixed by pushing PC directly here (no PcToStack()), matching real
    // hardware and correctly pairing with RTI's real-hardware pop order.
    void Interrupt::ReplacePC(int address, bool b, bool u)
    {
        Stack::PushToStack(static_cast<uint8_t>(NES_Register::PC >> 8));
        Stack::PushToStack(static_cast<uint8_t>(NES_Register::PC));
        Stack::ProcessorstatusToStack(b, u);
        NES_Register::PC = static_cast<uint16_t>(
            NES_Memory::Memory[address]->Value() | (NES_Memory::Memory[address + 1]->Value() << 8));
    }
}
