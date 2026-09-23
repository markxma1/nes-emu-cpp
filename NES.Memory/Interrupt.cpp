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

    void Interrupt::Check(int cycles)
    {
        isIRQ(cycles);
        isBRK(cycles);
        isNMI(cycles);
        isRESET();
    }

    // FIXED (real bug, found live via Tiny Toon Adventures' garbled sprite/
    // background - a user-flagged screenshot showing a chaotic pink/blue
    // splotch mixed into otherwise-correct tiles - traced through MMC3's
    // scanline-IRQ-driven CHR-bank split, confirmed with a targeted trace:
    // IRQ() sitting pending, un-dispatched, for anywhere from hundreds to
    // over nine thousand consecutive isIRQ() calls at a stretch): this used
    // to gate re-entry on `!NES_Register::P.Interrupt()` - the CPU's real
    // 6502 status-register I (interrupt-disable) flag - but also
    // *unconditionally set that same flag true* the moment the block was
    // first entered, before the SevenClock countdown below had a chance to
    // reach zero and actually dispatch. Every call after the first found
    // the flag already true and skipped the whole block, so SevenClock
    // never got to finish counting down and ReplacePC()/IRQ(false) never
    // ran - the interrupt was silently never delivered, stuck until
    // something *external* to this function incidentally cleared P's I
    // flag (typically the next NMI's own dispatch-then-RTI cycle, since
    // that's the only other thing in this codebase that touches it) -
    // explaining both the wildly variable stuck durations in the trace and
    // why this wasn't a *total*, permanently-hung freeze (NMI still fires
    // ~60 times/sec regardless, eventually unsticking it by accident).
    // Once genuinely that late, the CHR-bank-select register write a
    // game's IRQ handler makes in response lands many scanlines after the
    // split it was meant to produce, mismatching CHR data against
    // completely unrelated on-screen rows - exactly the reported symptom.
    //
    // Fixed the same way isNMI() (just above) already correctly handles
    // the identical problem: a dedicated `irqDispatchInProgress` flag -
    // *not* P's real I flag - tracks "is a dispatch already counting
    // down", so SevenClock can keep decrementing on every subsequent call
    // regardless of I's value. P.Interrupt() is now only ever consulted
    // once, to decide whether a *newly*-pending IRQ is currently masked by
    // the game's own SEI - matching real hardware, where checking I is a
    // precondition for *starting* dispatch, not a stand-in for "dispatch
    // already in progress".
    // FIXED (real bug, found live via Tiny Toon Adventures' missing status-
    // bar HUD, together with NES_PPU::AdvanceDots()'s dot-260 mapper-clock
    // fix - see its own comment for the full investigation): dispatch used
    // to count down by 1 per Check() call, i.e. one whole CPU *instruction*
    // per step, regardless of that instruction's real cycle length (2-7
    // cycles) - real hardware dispatches in a fixed 7 *cycles*, often well
    // under one of this port's "SevenClock ticks". Confirmed live via a
    // save-state trace + real ROM disassembly: Tiny Toon's NMI handler does
    // a deliberate CLI to let this exact status-bar IRQ interrupt it, but
    // only safely *before* reaching its own two-write $2006 pair nine
    // instructions later - this port's instruction-counted dispatch delay
    // landed dispatch almost exactly inside that pair nearly every time,
    // desyncing the shared PPUADDR write-toggle and corrupting both the
    // split handler's and the NMI handler's own writes. Fixed by decrementing
    // by the real cycle count of the instruction Check() just observed
    // (passed in from NES_CPU.cpp, which already knows it via
    // kCycleTable[opcode]) instead of a flat 1.
    void Interrupt::isIRQ(int cycles)
    {
        if (irqDispatchInProgress)
        {
            irqSevenClock -= cycles;
            if (irqSevenClock <= 0)
            {
                // FIXED (second, closely-related bug found while writing
                // this fix's own regression test - a fresh IRQ requested
                // after a previous one had already completed also failed
                // to dispatch): ReplacePC()'s internal stack push must
                // capture P as it was *before* this dispatch - matching
                // real 6502 hardware, which pushes the pre-interrupt status
                // and only sets I *afterward*, so a later RTI correctly
                // restores I back to whatever it was (typically false,
                // interrupts enabled). Setting P.Interrupt(true) before
                // calling ReplacePC() here (as this line used to, matching
                // the same premature-set mistake the FIXED note above
                // already explains for the *first* dispatch) would push a
                // status byte with I already set, so RTI would restore I as
                // *true* instead of false - leaving interrupts permanently
                // masked after every single IRQ, only one call to
                // isIRQ()'s own outer `!P.Interrupt()` check away from
                // reproducing the exact "stuck until something else clears
                // it" bug this whole fix exists to close, just moved one
                // dispatch later.
                ReplacePC(0xfffe, false, true);
                NES_Register::P.Interrupt(true);
                IRQ(false);
                if (std::getenv("NES_TRACE_MMC3_IRQLATCH"))
                    std::cerr << "[irqServiced] scanline=" << NES_PPU::CurrentScanline() << " jumped to 0x" << std::hex << NES_Register::PC << std::dec
                              << " $39=0x" << std::hex << static_cast<int>(NES_Memory::Memory[0x39]->value())
                              << " $3b=0x" << static_cast<int>(NES_Memory::Memory[0x3b]->value())
                              << " $3c=0x" << static_cast<int>(NES_Memory::Memory[0x3c]->value())
                              << " $38=0x" << static_cast<int>(NES_Memory::Memory[0x38]->value()) << std::dec << std::endl;
                irqSevenClock = 7;
                irqDispatchInProgress = false;
            }
            return;
        }
        if (IRQ() && !NES_Register::P.Interrupt())
            irqDispatchInProgress = true;
    }

    void Interrupt::isBRK(int cycles)
    {
        if (BRK())
        {
            NES_Register::P.Interrupt(true);
            brkSevenClock -= cycles;
            if (brkSevenClock <= 0)
            {
                ReplacePC(0xfffe, true, true);
                BRK(false);
                brkSevenClock = 7;
            }
        }
    }

    // FIXED (a race this port's own wall-clock-
    // driven NMI delivery introduces, found while investigating why Tiny
    // Toon Adventures' CPU throughput measured throughout stuck loops at
    // ~30000-275000 instr/sec that never advanced no matter how much real
    // time was given): this port's
    // NES_PPU::Display() (see its own comment on this) sets the
    // "please NMI" flag once per rendered frame - unlike real hardware,
    // where the PPU's vblank pulse and NMI line are wired directly together
    // and genuinely cannot pulse again until the *next* real vblank, ~16.6ms
    // later. If a game's own NMI handler does CLI part-way through (legal,
    // deliberate 6502 practice - re-enables nested/higher-priority
    // interrupts before the handler has actually returned via RTI) *and*
    // that handler is slow enough in this port (confirmed via NES_TRACE_NMI:
    // Display() gets called ~40 times/sec, and the flag was still found
    // pending, un-serviced, from a *previous* call a couple of times a
    // second on this exact ROM), a second NMI pulse can arrive and yank the
    // CPU straight back to the handler's very start before it ever reaches
    // RTI - discarding all the not-yet-committed work from the interrupted
    // attempt and starting over, forever, since the replacement attempt is
    // just as likely to be interrupted again the same way. This is
    // indistinguishable from "stuck in a tiny loop" when sampled (see
    // CPU/CPU/NES_CPU.cpp's TracePC()), which is exactly what this looked
    // like before being traced back here.
    //
    // Fixed by tracking whether the CPU is still inside an NMI handler that
    // hasn't reached RTI yet (nmiInProgress, set on entry below and cleared
    // once the stack unwinds back past the 3 bytes ReplacePC() just pushed -
    // the only signal available without a real per-cycle CPU/PPU clock) and
    // refusing to re-enter while that's true, matching real hardware's
    // edge-triggered NMI line: a pulse that arrives before the previous
    // handler returns is simply not actionable yet, not queued or
    // stacked - `nmi` stays true and isNMI() just tries again on the next
    // instruction, so nothing is lost, only delayed until it's safe.
    void Interrupt::isNMI(int cycles)
    {
        if (nmiInProgress && NES_Register::S == static_cast<uint8_t>(nmiEntryStackPointer + 3))
            nmiInProgress = false;

        if (NMI())
        {
            if (nmiSuppressed)
            {
                if (std::getenv("NES_TRACE_NMI_BLOCK"))
                    std::cerr << "[NMI blocked] nmiInProgress=" << nmiInProgress
                              << " nmiSuppressed=" << nmiSuppressed.load() << std::endl;
                return;
            }

            nmiSevenClock -= cycles;
            if (nmiSevenClock <= 0)
            {
                if (std::getenv("NES_TRACE_NMI_CYCLES"))
                    std::cerr << "NMI cycles=" << NES_CPU::totalCyclesEver.load() << std::endl;
                // The pushed P must be the pre-interrupt status; I is set
                // only afterwards, so RTI restores the old I flag.
                ReplacePC(0xfffa, false, true);
                NES_Register::P.Interrupt(true);
                nmiInProgress = true;
                nmiEntryStackPointer = NES_Register::S;
                NMI(false);
                nmiSevenClock = 7;
            }
        }
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
