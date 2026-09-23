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
#include <atomic>
#include <cstdint>

namespace NES
{
    /// @brief Handles the 6502's IRQ / NMI / BRK / RESET lines.
    /// http://wiki.nesdev.com/w/index.php/CPU_interrupts
    ///
    /// FIXED (a real data race,
    /// found live via ThreadSanitizer, per this project's own "verify
    /// against real evidence" rule: user asked for ASan/TSan builds
    /// specifically because of this project's real CPU-thread/UI-thread
    /// split - see NES/main.cpp's std::thread running NES_Console::Run()
    /// alongside the UI thread's render loop): at the time of this fix,
    /// nmi/irq/brk/POWER/RESET were all written from one thread
    /// (NES_PPU::Display(), then still called directly from the UI thread's
    /// getDisplay(), set NMI(true); main.cpp's UI thread sets POWER=false to
    /// stop) and read from the other (the CPU thread's Interrupt::Check(),
    /// called after every single instruction). Plain `bool` has no defined
    /// behavior for that under the C++ memory model - TSan caught the
    /// concrete case (NES_PPU::Display() writing NMI() at the same time
    /// Interrupt::isNMI() reads it) directly, with a full two-thread stack
    /// trace. In *practice* this rarely visibly corrupted anything (an
    /// aligned single-byte read/write is atomic on the CPU architectures
    /// this project has been tested on, and a torn read of a flag that's
    /// about to be overwritten again next frame anyway is low-consequence)
    /// - but it's undefined behavior regardless of platform luck, and
    /// std::atomic<bool> costs nothing extra here (single-byte flags
    /// checked at most once per instruction, nowhere near a hot path) while
    /// making the exact same cross-thread signaling this project already
    /// relies on well-defined. Every one of NMI()/IRQ()/BRK()/POWER/RESET's
    /// call sites already only ever does a plain read or a plain
    /// whole-value write (see e.g. NES_CPU.cpp's `while (Interrupt::POWER)`
    /// and NES_Console.cpp's `Interrupt::POWER = true`), both of which
    /// std::atomic<bool> supports as a drop-in replacement - no call site
    /// needed to change.
    ///
    /// UPDATE (later in the same overall effort, see
    /// NES_Console::RenderFrame()'s own comment for the full story): the
    /// actual bug this session was chasing (Chip 'n Dale's MMC1
    /// shift-register corruption) turned out to need NES_PPU::Display()
    /// itself moved off the UI thread entirely, onto the CPU thread's own
    /// execution loop (NES_CPU::Run(), gated on real accumulated CPU
    /// cycles). That means nmi/irq/brk are no longer actually written from
    /// a different thread than reads them - every write site (Display()'s
    /// `Interrupt::NMI(true)`, NES_PPU_Register.cpp's PPUCTRL-driven
    /// `Interrupt::NMI(v)`, Mapper_MMC3::OnScanline()'s `Interrupt::IRQ(true)`,
    /// Assembly_6502.cpp's `BRK_00()`) and every read site now run on the
    /// CPU thread only. `nmi`/`irq`/`brk` are kept `std::atomic<bool>`
    /// anyway - it costs nothing, and it's a correctness safety net rather
    /// than dead weight if frame composition (or a future mapper feature)
    /// ever moves back onto another thread. POWER/RESET remain genuinely
    /// cross-thread regardless (the UI thread starts/stops emulation via
    /// them - see NES/main.cpp) and must stay atomic.
    class Interrupt
    {
    public:
        static bool NMI() { return nmi; }
        static void NMI(bool v) { nmi = v; }

        static bool IRQ() { return irq; }
        static void IRQ(bool v) { irq = v; }

        static bool BRK() { return brk; }
        static void BRK(bool v) { brk = v; }

        static std::atomic<bool> POWER;
        static std::atomic<bool> RESET;

        // NEW, no real-hardware equivalent - a
        // deliberate, documented deviation from strict hardware fidelity,
        // added specifically to close a residual recurrence of the exact
        // "Chip 'n Dale MMC1 shift-register corruption" bug class
        // NES_Console::RenderFrame()'s own comment already documents fixing
        // once this session (via cycle-accurate NMI cadence). Real hardware
        // has no interlock here at all - it just relies on a multi-write
        // MMC1 shift-register burst (Mapper_MMC1::WriteRegister(), called
        // from a game's own bank-switch code) being astronomically unlikely
        // to overlap the true ~29780-cycle-cadence NMI pulse. This port's
        // NMI is now driven by real accumulated CPU cycles too (see
        // NES_PPU::AdvanceDots()), which makes a collision similarly rare -
        // but not impossible, since NES_CPU::Step() stays instruction-atomic
        // (a documented, deliberate scope limit: no per-cycle/mid-instruction
        // NMI delivery - see the redesign plan's "Explicit non-goals"). A
        // live save-state capture from Chip 'n Dale reproduced exactly this
        // residual case: an NMI landed between two writes of $FF04's own
        // 5-write MMC1 burst, corrupting Mapper_MMC1's shared shiftRegister/
        // writeCount state and leaving the game's cooperative task scheduler
        // permanently stuck (a $9A "skip PPU update" flag the corrupted
        // write path never cleared again). Mapper_MMC1::WriteRegister() sets
        // this for the duration of each 5-write burst (see its own comment);
        // isNMI() simply leaves a pending NMI request pending (matching real
        // hardware's edge-triggered semantics - see isNMI()'s own comment)
        // instead of delivering it while a burst is in flight, letting the
        // burst finish uninterrupted before the very next Check() delivers
        // it, typically only a handful of instructions later - an
        // imperceptible timing nudge, not a correctness gap in the other
        // direction (NMI is never dropped, only briefly deferred).
        static void SuppressNMI(bool v) { nmiSuppressed = v; }

        /// $FFFA-$FFFB  Address of Non-Maskable Interrupt (NMI) handler routine
        /// $FFFC-$FFFD  Address of power-on reset handler routine
        /// $FFFE-$FFFF  Address of Break (BRK instruction) handler routine
        ///
        /// FIXED (real bug, found live via Tiny Toon Adventures' missing
        /// status-bar HUD - see NES_PPU::AdvanceDots()'s own comment for the
        /// investigation this grew out of): this used to take no cycle
        /// count, calling isIRQ()/isBRK()/isNMI() once per Step() with no
        /// way for their shared SevenClock countdown to know how many real
        /// CPU cycles the instruction it just observed actually took - see
        /// isIRQ()'s own FIXED note for the full story and the exact
        /// real-game symptom this caused. Now takes the executed
        /// instruction's real cycle count (NES_CPU.cpp passes
        /// kCycleTable[opcode], already known at the Check() call site) so
        /// dispatch latency can be measured in real cycles instead of whole
        /// instructions.
        static void Check(int cycles);

        static void Stop() { POWER = false; }

    private:
        static std::atomic<bool> nmi;
        static std::atomic<bool> irq;
        static std::atomic<bool> brk;
        static std::atomic<bool> nmiSuppressed;

        // FIXED (real bug, found alongside the cycle-accuracy fix above):
        // a single shared SevenClock counted down by all three of
        // isIRQ()/isBRK()/isNMI() whenever more than one was concurrently
        // dispatching - a real, previously-documented-but-unaddressed
        // crosstalk hazard (each one's decrement stole from the others'
        // countdown). Switching the decrement from "1 per call" to "real
        // cycles per call" made this materially worse (a single slow
        // instruction could push multiple concurrent countdowns past zero
        // at once), so fixed properly now: each interrupt type gets its own
        // independent countdown, matching real hardware's three genuinely
        // separate dispatch sequences.
        static int irqSevenClock;
        static int brkSevenClock;
        static int nmiSevenClock;

        // NEW - see isNMI()'s FIXED note for the race
        // this closes. Real hardware's NMI line is edge-triggered and can't
        // pulse again until the current handler's RTI has actually run;
        // this tracks that in the only way available without a real
        // per-cycle PPU/CPU clock - by watching the stack unwind back past
        // where NMI entry pushed its own PC+P frame.
        static bool nmiInProgress;
        static uint8_t nmiEntryStackPointer;

        // NEW - see isIRQ()'s own FIXED note (the real
        // bug this closes: IRQ dispatch could get stuck for thousands of
        // instructions). Same role as nmiInProgress above, for the same
        // reason: a dedicated "am I mid-dispatch" flag, kept separate from
        // NES_Register::P's real 6502 Interrupt-disable (I) flag, since
        // that flag gets *set* partway through dispatch (matching real
        // hardware) and so can't safely double as its own re-entry guard.
        static bool irqDispatchInProgress;

        static void isIRQ(int cycles);
        static void isBRK(int cycles);
        static void isNMI(int cycles);
        static void isRESET();
        static void ReplacePC(int address, bool b, bool u);
    };
}
