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

namespace NES
{
    /// PAL/NTSC/no-throttle CPU speed selector. Port of the C# `Mod` enum.
    enum class Mod
    {
        NTSC,
        PAL,
        none
    };

    /// @brief The 6502 fetch-decode-execute loop. Port of CPU/CPU/NES_CPU.cs.
    /// http://wiki.nesdev.com/w/index.php/CPU
    class NES_CPU
    {
    public:
        /// Instantiates the opcode table (mirrors the C# constructor creating an Assembly_6502).
        NES_CPU();

        static double cpuspeed;

        /// PAL/NTSC/no-throttle selector - see SleepTime()/Sleep()'s FIXED
        /// notes. Moved from private to public (the C# original's
        /// CPUSpeedForm debug tool, never ported, presumably had its own
        /// access to this - see NES_Console::INIT(), which now sets this to
        /// Mod::NTSC directly since that debug UI doesn't exist here).
        static Mod mod;

        // NEW, no C# equivalent - user-requested speed control ("+"/"-"/"0"
        // keys, see NES/main.cpp), plus the direct fix for a real
        // performance question raised live: with the real-time throttle
        // removed entirely (Mod::none), this ROM measured ~470k instr/sec
        // on the test machine - a real, if modest, speedup over the
        // ~340k instr/sec Mod::NTSC produces (confirming the throttle
        // *is* doing real work, not a no-op masked by rendering cost - see
        // Sleep()'s own comment for why both can be true at once) - nowhere
        // near "1000x faster" naive host-vs-6502 clock-speed math would
        // suggest, because this port's own per-instruction/per-scanline
        // rendering work is real, non-trivial computation happening on the
        // same thread, not an artificial delay. Rather than replace
        // Mod::NTSC's real-hardware-accurate pacing outright, this scales
        // Sleep()'s target delay directly - 1.0 (default) reproduces
        // Mod::NTSC exactly, 2.0 halves the delay (~2x speed), 0.25 is
        // quarter speed, and a large value (see kMaxSpeedMultiplier) makes
        // the target delay small enough that Sleep()'s busy-wait exits
        // almost immediately - i.e. effectively uncapped - without needing
        // a separate code path from the normal-speed case.
        static std::atomic<double> speedMultiplier;
        static constexpr double kMinSpeedMultiplier = 0.125; // 1/8x
        static constexpr double kMaxSpeedMultiplier = 64.0;  // effectively uncapped in practice

        // NEW, no C# equivalent - real, measured frames/second (distinct
        // from `cpuspeed`, which is a pacing-loop-internal ns/cycle figure,
        // not an actual frame rate). Updated a few times a second from a
        // rolling real-time window in Run() - see its own comment. Exposed
        // for the CPU-speed debug window (NES/main.cpp).
        static std::atomic<double> measuredFPS;

        // NEW, no C# equivalent - a real, monotonic count of completed
        // 262-scanline NES frames (incremented in Run(), right alongside
        // the NES_Console::RenderFrame() call it's paired with - see that
        // call site's own comment). FIXED (real bug, found while
        // investigating a user-reported Bram Stoker's Dracula "flickers
        // between frames, sometimes normal sometimes text" symptom):
        // NES/main.cpp's UI-thread loop (NES_PLAYBACK_INPUT replay,
        // NES_AUTO_QUIT_FRAME, and the original recording feature itself)
        // used to number frames via its own free-running loop-iteration
        // counter (`uiFrame`, incremented once per `cv::waitKeyEx(16)`
        // poll), completely decoupled from how many *real* NES frames the
        // CPU thread had actually completed at that moment - the UI loop
        // and the CPU thread's own real-time pacing (Sleep(), see Run()'s
        // own comment) are two independently-paced loops with no
        // frame-lock between them. Under normal conditions the two stay
        // roughly in step, but any real-world timing hitch (OS scheduling
        // jitter, background CPU load, a debug window repaint) desyncs
        // them - found live: replaying the exact same recorded input file
        // from a cold process start, twice, landed on visibly different
        // game states at the same nominal "frame 3768" depending on
        // concurrent system load, even though the input file itself never
        // changed. A recorded button press tagged "frame N" would then get
        // replayed against the CPU thread's Nth *UI-loop-tick*, not its
        // Nth *real emulated frame* - silently shifting every input's real
        // timing by however far the two loops had drifted apart at
        // recording or replay time. Exposing this real per-frame counter
        // lets NES/main.cpp key playback/quit-frame numbering off actual
        // emulated frames instead, immune to UI-thread scheduling jitter.
        static std::atomic<long long> completedFrames;

        /// Runs the fetch-decode-execute loop until Interrupt::POWER goes
        /// false. NEW behavior, no C# equivalent: also advances a real
        /// per-scanline/per-dot PPU clock after every Step() with that
        /// instruction's own real executed cycle count
        /// (NES_PPU::AdvanceDots()), and calls NES_Console::RenderFrame()
        /// once that clock completes a 262-scanline frame - see
        /// NES_PPU::AdvanceDots()'s own comment for why this replaced the
        /// earlier flat per-frame cycle accumulator (which itself had
        /// replaced the original UI-thread/wall-clock-driven trigger).
        static void Run();

        /// Fetches and executes exactly one instruction, then checks
        /// pending interrupts - the body Run() loops forever. Exposed so a
        /// test harness (see tests/nestest) can single-step the CPU without
        /// the wall-clock throttling Run()/Sleep() apply, and compare
        /// register state against a known-good reference log after each
        /// instruction. Returns the executed opcode's real 6502 cycle count
        /// (see NES_CPU.cpp's kCycleTable) - Run()/Sleep() uses this to
        /// throttle by the right amount per instruction instead of a flat
        /// one-cycle assumption (see Sleep()'s own FIXED note); a caller
        /// that just single-steps (like tests/nestest) can ignore it.
        static int Step();

        /// One real NTSC/PAL frame's worth of CPU cycles - see the .cpp
        /// definition's own comment. No longer used by Run() directly (see
        /// NES_PPU::AdvanceDots(), which now drives frame timing off a real
        /// per-scanline clock) - kept as a standalone, still-tested
        /// documented hardware fact (see tests/cpu/cpu_check.cpp's
        /// regression test against these exact constants).
        static double FrameCycles();

    private:
        static int SleepTime();
    };
}
