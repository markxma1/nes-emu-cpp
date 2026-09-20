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
#include "NES_CPU.h"
#include "AssemblyList.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "AddressSetup.h"
#include "Interrupt.h"
#include "../OAM/NES_PPU_OAM.h"
#include "NES_PPU_Register.h"
#include "NES_PPU.h"
#include "NES_Console.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace NES
{
    namespace
    {
        AssemblyList Assembly;

        // NEW, no C# equivalent - the C# original's Sleep() (see this
        // file's own FIXED notes on it) throttled every Step() by a flat
        // per-*instruction* delay, silently treating every opcode as if it
        // took exactly one CPU cycle. Real 6502 opcodes take 2-8 cycles
        // depending on the opcode and addressing mode - undercounting every
        // multi-cycle instruction meant this port's CPU still ran multiple
        // times faster than authentic NTSC/PAL speed even after the
        // microsecond/nanosecond unit fix (confirmed live: Chip 'n Dale's
        // music still played "ultra schnell" with that fix alone). This
        // table gives Step() each opcode's *base* cycle count (i.e.
        // excluding the well-known +1 for a taken branch, +1 more if that
        // branch crosses a page, and +1 for some read instructions'
        // absolute,X/Y and (zp),Y addressing modes when the effective
        // address crosses a page - none of that is tracked here, so this
        // remains an approximation, just a far closer one than "1 cycle
        // flat"), so Sleep() can throttle each instruction by its own
        // correct cycle count instead.
        //
        // Cross-referenced from https://www.masswerk.at/6502/6502_instruction_set.html
        // (base cycle counts, both official and the unofficial/"illegal"
        // opcodes this project also implements - see
        // AssemblyList.cpp's UnofficialOpcodes()) against this port's own
        // verified 6502 knowledge, with corrections applied at $0F, $1F,
        // $2F, $3F, $4F, $5F, $6F, $7F, $80 and $8F: that source's table
        // listed 65C02/Rockwell-only opcodes (BBR/BBS/BRA) at those bytes,
        // which don't exist on the NES's NMOS-based 2A03 - those slots hold
        // the real NMOS illegal opcodes there instead (SLO/RLA/SRE/RRA
        // absolute(,X) and SAX absolute, all confirmed against this port's
        // own AssemblyList.cpp), and one more correction at $DA (single-byte
        // unofficial NOP - 2 cycles like its siblings at $1A/$3A/$5A/$7A/$FA,
        // not 3). Bytes with no implemented opcode at all in AssemblyList.cpp
        // (JAM/KIL - $02/$12/$22/$32/$42/$52/$62/$72/$92/$B2/$D2/$F2, which
        // hang real hardware and are never reachable via NoAssemby's
        // exception path anyway) use a harmless placeholder of 2.
        constexpr std::array<uint8_t, 256> kCycleTable = {
            7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,   // 0x00-0x0F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0x10-0x1F
            6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,   // 0x20-0x2F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0x30-0x3F
            6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,   // 0x40-0x4F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0x50-0x5F
            6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,   // 0x60-0x6F
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0x70-0x7F
            2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,   // 0x80-0x8F
            2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,   // 0x90-0x9F
            2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,   // 0xA0-0xAF
            2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,   // 0xB0-0xBF
            2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,   // 0xC0-0xCF
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0xD0-0xDF
            2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,   // 0xE0-0xEF
            2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 0xF0-0xFF
        };

        // TEMPORARY diagnostic aid, not a port of anything - opt-in via the
        // NES_TRACE_PC environment variable, off by default (zero cost when
        // unset). Set NES_TRACE_PC=1 to log every caught exception (was
        // fully silent before - see the catch block below) and a PC sample
        // every ~200000 executed instructions, to help tell apart "CPU is
        // genuinely stuck re-executing the same address forever" (e.g. an
        // unimplemented/illegal opcode whose exception is swallowed without
        // PC ever advancing) from "CPU is still progressing but some other
        // state never satisfies a wait condition".
        bool TracePC()
        {
            static const bool enabled = std::getenv("NES_TRACE_PC") != nullptr;
            return enabled;
        }

        // TEMPORARY, same spirit as TracePC() - opt-in via NES_TRACE_RAM.
        // Diffs zero page ($0000-$00FF) every ~5000 instructions and logs
        // any byte that changed, mirroring an FCEUX Lua RAM-diff trace used
        // to find the exact RAM writes around the "STAGE 1" transition on a
        // reference emulator - lets that signature be searched for here too.
        bool TraceRAM()
        {
            static const bool enabled = std::getenv("NES_TRACE_RAM") != nullptr;
            return enabled;
        }

        // NEW, no C# equivalent - a permanent, reusable instruction-level
        // debugger, opt-in via env vars (same zero-cost-when-unset pattern
        // as every other NES_TRACE_* hook here). Built specifically because
        // the printf-style tracing this project had been using (TracePC()
        // and its one-off temporary extensions, added and removed
        // repeatedly throughout this project's mapper/timing investigations)
        // hit a real limit investigating Chip 'n Dale's custom cooperative
        // task scheduler: knowing *that* a JSR didn't return to its call
        // site isn't enough to know *why* - that needs to actually see the
        // stack contents instruction-by-instruction around the point of
        // divergence, which one-off single-value watches can't give you.
        //
        // Usage: NES_BREAK_AT=0xC2A5 (hex or decimal) arms a breakpoint;
        // once PC reaches that address, this prints full CPU state (PC, A,
        // X, Y, S, P) plus the 8 bytes above the current stack pointer
        // (NES_Memory::Stack[S+1..S+8], i.e. exactly what the next few
        // PLA/PLP/RTS/RTI would pop) for that instruction and the next
        // NES_BREAK_STEPS-1 (default 40) instructions after it, letting a
        // JSR/RTS or PHA/PLA mismatch be seen directly rather than inferred
        // from its side effects. NES_BREAK_WATCH=addr1,addr2,... (hex or
        // decimal, comma-separated) additionally prints those raw memory
        // bytes on every traced step. NES_BREAK_ONCE=1 arms the breakpoint
        // only the first time PC reaches it (default: re-arms every time,
        // useful for a resume point like this one that's visited - or, as
        // in the bug this was built for, *should be* visited - repeatedly).
        long ParseAddrEnv(const char* name)
        {
            const char* v = std::getenv(name);
            if (!v) return -1;
            return std::strtol(v, nullptr, 0); // base 0: accepts "0xC2A5" or plain decimal
        }

        void DebugBreakpoint(uint16_t pcBefore, uint8_t opcode, uint64_t instructionCount)
        {
            static const long breakAt = ParseAddrEnv("NES_BREAK_AT");
            if (breakAt < 0)
                return;
            static const long stepBudget = [] { long s = ParseAddrEnv("NES_BREAK_STEPS"); return s > 0 ? s : 40; }();
            static const bool once = std::getenv("NES_BREAK_ONCE") != nullptr;
            static bool everArmed = false;
            static long stepsLeft = 0;

            if (stepsLeft == 0)
            {
                if (pcBefore != static_cast<uint16_t>(breakAt))
                    return;
                if (once && everArmed)
                    return;
                everArmed = true;
                stepsLeft = stepBudget;
                std::cerr << "[NES_BREAK] hit $" << std::hex << breakAt << std::dec
                           << " (instr #" << instructionCount << ") - tracing " << stepBudget
                           << " instructions" << std::endl;
            }

            std::cerr << "[NES_BREAK] PC=0x" << std::hex << pcBefore << " op=0x" << static_cast<int>(opcode)
                       << " A=0x" << static_cast<int>(NES_Register::A) << " X=0x" << static_cast<int>(NES_Register::X)
                       << " Y=0x" << static_cast<int>(NES_Register::Y) << " S=0x" << static_cast<int>(NES_Register::S)
                       << " P=0x" << static_cast<int>(NES_Register::P.P) << std::dec
                       << " (instr #" << instructionCount << ")";

            std::cerr << " stack[S+1..S+8]=";
            for (int i = 1; i <= 8; ++i)
            {
                size_t idx = static_cast<size_t>((NES_Register::S + i) & 0xFF);
                std::cerr << std::hex << static_cast<int>(NES_Memory::Stack[idx]->value()) << std::dec << " ";
            }

            static const std::string watchList = [] { const char* w = std::getenv("NES_BREAK_WATCH"); return w ? std::string(w) : std::string(); }();
            if (!watchList.empty())
            {
                std::cerr << " watch=";
                size_t pos = 0;
                while (pos < watchList.size())
                {
                    size_t comma = watchList.find(',', pos);
                    std::string tok = watchList.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    long addr = std::strtol(tok.c_str(), nullptr, 0);
                    if (addr >= 0 && addr <= 0xFFFF)
                        std::cerr << "$" << std::hex << addr << "=0x"
                                   << static_cast<int>(NES_Memory::Memory[static_cast<size_t>(addr)]->value())
                                   << std::dec << " ";
                    if (comma == std::string::npos) break;
                    pos = comma + 1;
                }
            }
            std::cerr << std::endl;

            --stepsLeft;
        }
    }

    double NES_CPU::cpuspeed = 0;
    Mod NES_CPU::mod = Mod::none;
    std::atomic<double> NES_CPU::speedMultiplier{1.0};
    std::atomic<double> NES_CPU::measuredFPS{0.0};
    std::atomic<long long> NES_CPU::completedFrames{0};

    NES_CPU::NES_CPU()
    {
        // C# built a throwaway `new Assembly_6502()` here; Assembly_6502 in this
        // port only has static members, so there is nothing to construct - the
        // opcode table itself lives in the file-local `Assembly` instance above.
    }

    int NES_CPU::Step()
    {
        static uint64_t instructionCount = 0;
        static uint8_t zpPrev[256] = {};
        static bool zpInit = false;

        // Real instructions/sec, checked every 20000 instructions (cheap)
        // and printed once per real second - kept as a permanent part of
        // NES_TRACE_PC (same "extends an existing diagnostic" precedent as
        // the PPUCTRL.V= field below): answers "is the CPU merely stuck in
        // a loop, or is something making it run drastically slower than
        // normal" *before* assuming the former - the periodic PC/register
        // sample futher down only fires every 2000000 instructions, which a
        // sufficiently slowed-down ROM may never even reach within a normal
        // test window. Found real value immediately: a mapper register
        // write that ends up re-painting a mapper's full PRG/CHR window
        // (e.g. Mapper_MMC3's WriteBankSelect()/WriteBankData(), which both
        // unconditionally call ApplyPrgBanks()+ApplyChrBanks() even when
        // only one of the two actually changed) is $O(bank size)$ per call,
        // not cheap - a game whose own polling/setup loop writes those
        // registers in a tight cycle (observed on Tiny Toon Adventures,
        // mapper 4/MMC3: a repeating `STA $8000 / STA $8001` pair) can drop
        // CPU throughput by more than an order of magnitude (~590000 to
        // ~30000 instr/sec measured), which on its own can turn "a loop
        // that would finish in a handful of real NES frames" into "still
        // running after 90 real seconds" even without that loop being a
        // true infinite hang.
        if (TracePC() && (instructionCount % 20000 == 0))
        {
            static auto lastReportTime = std::chrono::steady_clock::now();
            static uint64_t lastReportCount = 0;
            auto now = std::chrono::steady_clock::now();
            double elapsedSec = std::chrono::duration<double>(now - lastReportTime).count();
            if (elapsedSec >= 1.0)
            {
                std::cerr << "[NES_TRACE_PC] throughput: " << ((instructionCount - lastReportCount) / elapsedSec)
                           << " instr/sec (instr #" << instructionCount << ") PC=0x" << std::hex << NES_Register::PC
                           << std::dec << " I=" << NES_Register::P.Interrupt() << " IRQpending=" << Interrupt::IRQ()
                           << std::endl;
                lastReportTime = now;
                lastReportCount = instructionCount;
            }
        }

        uint16_t pcBefore = NES_Register::PC;
        uint8_t opcode = NES_Memory::Memory[pcBefore]->Value();

        DebugBreakpoint(pcBefore, opcode, instructionCount);

        // TEMPORARY diagnostic aid - opt-in via NES_TRACE_HUDCLEAR_FOLLOWUP,
        // off by default. See NES_PPU_Register::hudClearFollowupTraceRemaining's
        // own comment - logs the raw (PC, opcode) of every instruction while
        // the counter armed by that hook is still counting down, read at the
        // exact moment each one actually executes (so whichever PRG bank is
        // live right then is what gets shown - no separate-dump bank risk).
        if (NES_PPU_Register::hudClearFollowupTraceRemaining > 0)
        {
            std::cerr << "[hudclear+] PC=0x" << std::hex << pcBefore << " op=0x" << static_cast<int>(opcode)
                       << std::dec << std::endl;
            --NES_PPU_Register::hudClearFollowupTraceRemaining;
        }

        try
        {
            Assembly.assembly()[opcode]();
        }
        catch (const std::exception& e)
        {
            // Matches the C# catch-all around one instruction step: an
            // unmapped opcode (NoAssemby - see AssemblyList.cpp's
            // UnofficialNOPs()/UnofficialOpcodes() for how many of these are
            // now actually implemented) is swallowed and execution just
            // continues from wherever PC ended up. See TracePC()
            // above - if the opcode handler threw before advancing PC,
            // this repeats forever on the same address (a real stall
            // that looks alive only because NMI still interrupts it).
            if (TracePC())
            {
                std::cerr << "[NES_TRACE_PC] exception at PC=0x" << std::hex << pcBefore
                           << std::dec << " opcode=0x" << std::hex << static_cast<int>(opcode)
                           << std::dec << ": " << e.what() << std::endl;
                static uint16_t lastReportedPc = 0xFFFF;
                if (pcBefore != lastReportedPc)
                {
                    lastReportedPc = pcBefore;
                    std::cerr << "[NES_TRACE_PC] last " << AssemblyList::debug.size()
                               << " executed instructions before this:" << std::endl;
                    for (const auto& d : AssemblyList::debug)
                        std::cerr << "[NES_TRACE_PC]   " << d << std::endl;
                }
            }
        }
        Interrupt::Check(kCycleTable[opcode]);
        ++instructionCount;



        // Sample less often but log more: registers (to see if the
        // CPU is computing different values over time or looping
        // with frozen state) and a bit of PPU/OAM state (to tell
        // apart "game logic never advances" from "game logic
        // advances fine but rendering doesn't show it").
        if (TracePC() && (instructionCount % 2000000 == 0))
        {
            std::cerr << "[NES_TRACE_PC] PC=0x" << std::hex << NES_Register::PC
                       << " A=0x" << static_cast<int>(NES_Register::A)
                       << " X=0x" << static_cast<int>(NES_Register::X)
                       << " Y=0x" << static_cast<int>(NES_Register::Y)
                       << " S=0x" << static_cast<int>(NES_Register::S)
                       << " P=0x" << static_cast<int>(NES_Register::P.P)
                       << " PPUMASK.b=" << std::dec << NES_PPU_Register::PPUMASK.b()
                       << " PPUMASK.s=" << NES_PPU_Register::PPUMASK.s()
                       << " PPUCTRL.V=" << NES_PPU_Register::PPUCTRL.V()
                       << " OAMcount=" << NES_PPU_OAM::SpriteTile.size();
            if (!NES_PPU_OAM::SpriteYc.empty())
                std::cerr << " OAM0.Y=0x" << std::hex << static_cast<int>(NES_PPU_OAM::SpriteYc[0]->Value())
                           << " OAM0.X=0x" << static_cast<int>(NES_PPU_OAM::SpriteXc[0]->Value());
            std::cerr << std::dec << " (instr #" << instructionCount << ")" << std::endl;
            // TEMPORARY, same spirit as the block above - dump the last 20
            // executed instructions (populated on every Step(), not just on
            // exception - see AssemblyList::Debug()) to see what loop, if
            // any, the CPU is actually spinning in.
            std::cerr << "[NES_TRACE_PC] last instructions:" << std::endl;
            for (const auto& d : AssemblyList::debug)
                std::cerr << "[NES_TRACE_PC]   " << d << std::endl;
        }

        // Zero-page diff, same idea as the FCEUX Lua ram-diff script
        // used to find the exact writes around the reference
        // emulator's "STAGE 1" transition (a block of $0022-$0037
        // all reset together there) - see if the same signature
        // ever shows up here.
        if (TraceRAM() && (instructionCount % 5000 == 0))
        {
            bool sawTransition = false;
            for (int i = 0; i < 256; i++)
            {
                uint8_t v = NES_Memory::Memory[static_cast<size_t>(i)]->value();
                if (!zpInit || v != zpPrev[i])
                {
                    if (zpInit)
                    {
                        std::cerr << "[NES_TRACE_RAM] instr#" << instructionCount
                                   << " addr=0x" << std::hex << i
                                   << " old=0x" << static_cast<int>(zpPrev[i])
                                   << " new=0x" << static_cast<int>(v) << std::dec << std::endl;
                        if (i == 0x22 && v == 0x24)
                            sawTransition = true;
                    }
                    zpPrev[i] = v;
                }
            }
            zpInit = true;

            static bool afterTransition = false;
            static int dumpsRemaining = 0;
            if (sawTransition)
            {
                afterTransition = true;
                dumpsRemaining = 15;
            }

            // Full OAM dump every ~300000 instructions for a while
            // after the reference emulator's "ship spawns" signature
            // ($22 -> 0x24) is seen here too - is a plausible
            // on-screen sprite actually sitting in OAM, and does its
            // tile index vary sensibly frame to frame, or is this a
            // one-off snapshot mid-composition?
            if (afterTransition && dumpsRemaining > 0 && (instructionCount % 300000 < 5000))
            {
                --dumpsRemaining;
                std::cerr << "[NES_TRACE_OAM] --- instr#" << instructionCount << " ---" << std::endl;
                for (size_t i = 0; i < NES_PPU_OAM::SpriteTile.size(); i++)
                {
                    std::cerr << "[NES_TRACE_OAM] i=" << i
                               << " Y=0x" << std::hex << static_cast<int>(NES_PPU_OAM::SpriteYc[i]->Value())
                               << " X=0x" << static_cast<int>(NES_PPU_OAM::SpriteXc[i]->Value())
                               << " tile=0x" << static_cast<int>(NES_PPU_OAM::SpriteTile[i].adress->Value())
                               << " attr=0x" << static_cast<int>(NES_PPU_OAM::SpriteAttribute[i].adress->Value())
                               << std::dec << std::endl;
                }
            }
        }

        return kCycleTable[opcode];
    }

    // NEW, no C# equivalent - one real NTSC/PAL frame's worth of CPU
    // cycles. http://wiki.nesdev.com/w/index.php/Cycle_reference_chart:
    // NTSC is 341 PPU dots/scanline * 262 scanlines/frame = 89342 PPU
    // dots/frame, at a fixed 3 PPU-dots-per-CPU-cycle ratio -> 89342/3 =
    // 29780.667 CPU cycles/frame; PAL is 341*312 = 106392 dots/frame at a
    // 3.2:1 ratio -> 106392/3.2 = 33247.5 CPU cycles/frame. `Mod::none`
    // (no throttling, used only by test harnesses that call Step() directly
    // and never call Run() at all - see tests/cpu/cpu_check.cpp,
    // tests/nestest/nestest_check.cpp, tests/mappers/mapper_check.cpp) has
    // no meaningful frame length; 0 here documents that rather than
    // dividing by it.
    //
    // UPDATE (later in the same overall effort - see NES_PPU::AdvanceDots()
    // for the full story): Run() no longer uses this to decide *when* to
    // publish a frame - a real per-scanline PPU clock (NES_PPU::AdvanceDots(),
    // driven off each Step()'s own real cycle count) does that now,
    // correctly landing frame/vblank boundaries at the real per-scanline
    // CPU-cycle offsets real hardware would, rather than a flat
    // once-every-~29780-cycles average. This function is kept as a
    // standalone, still-useful, still-tested (see
    // tests/cpu/cpu_check.cpp's TestFrameCyclesMatchesRealHardwareTiming())
    // documented hardware constant - 341 dots/scanline * 262 scanlines is
    // the same fact AdvanceDots() itself hardcodes, just not re-derived
    // from this function directly, to avoid the PPU depending on
    // NES_CPU::mod for a value it doesn't otherwise need (PAL isn't
    // supported end-to-end anywhere else in this port either - see
    // NES_Console::INIT() hardcoding Mod::NTSC).
    double NES_CPU::FrameCycles()
    {
        switch (mod)
        {
            case Mod::NTSC: return 89342.0 / 3.0;
            case Mod::PAL: return 106392.0 / 3.2;
            default: return 0.0;
        }
    }

    // FIXED (new design, not a C# port - see NES_PPU::AdvanceDots()'s own
    // comment for the full story on the bug class this closes, found live
    // via three separate real games this session: Chip 'n Dale's MMC1
    // shift-register corruption, Tiny Toon Adventures' MMC3 status-bar
    // CHR-bank splits landing on the wrong scanline, and Chip 'n Dale's
    // nametable-streaming desync during horizontal scroll): a real emulated
    // frame used to be triggered by a flat ~29780-accumulated-cycle
    // counter (this function's own earlier revision), which fixed the
    // original UI-thread/wall-clock-driven trigger bug but still only ever
    // sampled PPU/mapper state once per *whole frame* - anything a game
    // changed mid-frame via real per-scanline timing (CHR banks, nametable
    // streaming, scroll) had no way to be reflected at the right moment.
    // Fixed by advancing a real per-scanline/per-dot PPU clock
    // (NES_PPU::AdvanceDots()) after every single Step(), using that
    // instruction's own real executed cycle count - the same value this
    // loop already had on hand - instead of only checking it against a
    // flat per-frame total. AdvanceDots() returns true exactly once per
    // completed 262-scanline sweep, which is when a newly composed frame
    // is ready to publish via NES_Console::RenderFrame() - same publish
    // contract as before, just correctly timed on the way there.
    // FIXED (real bug, found via explicit user report: "bei 64x ist speed
    // nur 90% von dem was es sein soll" - at 64x speed the achieved rate
    // was only ~90% of nominal, and even 1x didn't feel authentic): the old
    // design (git history: a Sleep(function) wrapper) called
    // std::chrono::steady_clock::now() in a busy-wait *after every single
    // Step()*, with a fresh target computed from that one instruction's own
    // cycle count. Two compounding problems: (1) at high speedMultiplier
    // values the *target* itself shrinks to a few nanoseconds per
    // instruction (e.g. 559ns/64 ~= 8.7ns) - well under the real overhead of
    // one std::chrono::now() call plus the surrounding function-call/lambda
    // machinery, so the achieved rate was capped by that per-instruction
    // overhead, not by real NES timing; (2) each instruction's delay was
    // computed and applied in total isolation - a single instruction or
    // frame that happened to take longer than its own tiny budget (e.g. the
    // per-scanline renderer, a debug-window rebuild, host OS scheduling
    // jitter) had no way to be "made up" by anything after it, so temporary
    // slowdowns permanently dragged the achieved average speed down instead
    // of being absorbed - exactly the "es nicht gesamtes teil verlangsamt"
    // (a slowdown shouldn't slow down the whole thing) behavior asked for.
    //
    // Fixed with a cumulative pacing design, explicitly decoupled from
    // individual CPU ticks ("unabhängig vom CPU takten" - the per-Step()
    // loop itself never touches the clock or waits at all): a pacing
    // *baseline* (a real time + a total cycle count, both reset only when
    // speedMultiplier changes) is compared against the clock once every
    // kPacingCheckCycles (~a few dozen instructions, not every single one -
    // amortizing the now()-call overhead that broke the 64x case), and only
    // waits if genuinely *ahead* of the cumulative schedule; if behind, it
    // simply continues without waiting, letting the deficit close itself on
    // its own over subsequent (usually cheaper) instructions rather than
    // bursting to catch up all at once - except when the deficit exceeds
    // kMaxCatchUpNs (e.g. after the debug windows stall real time, or a
    // save/load), where rebasing avoids a multi-hundred-millisecond
    // full-tilt burst. speedMultiplier changes mid-run rebase the baseline
    // so the new rate only ever applies to cycles executed from that point
    // on, never retroactively.
    void NES_CPU::Run()
    {
        using Clock = std::chrono::steady_clock;
        constexpr uint64_t kPacingCheckCycles = 200; // ~30-100 instructions between clock reads
        constexpr double kMaxCatchUpNs = 250.0 * 1000.0 * 1000.0; // 250ms - beyond this, rebase instead of bursting

        auto baseTime = Clock::now();
        uint64_t baseCycles = 0;
        uint64_t totalCycles = 0;
        uint64_t cyclesSinceCheck = 0;
        double lastMultiplier = speedMultiplier.load(std::memory_order_relaxed);

        // FPS is tracked completely independently of the pacing baseline
        // above (never rebased by a speed-multiplier change or a
        // catch-up reset) - it always reflects the real, measured frame
        // rate, updated a few times a second from its own rolling window.
        auto fpsWindowStart = Clock::now();
        int framesInFpsWindow = 0;

        while (Interrupt::POWER)
        {
            int cycles = Step();
            if (NES_PPU::AdvanceDots(cycles))
            {
                NES_Console::RenderFrame();
                completedFrames.fetch_add(1, std::memory_order_relaxed);
                framesInFpsWindow++;
                double fpsWindowSec = std::chrono::duration<double>(Clock::now() - fpsWindowStart).count();
                if (fpsWindowSec >= 0.5)
                {
                    measuredFPS.store(framesInFpsWindow / fpsWindowSec, std::memory_order_relaxed);
                    framesInFpsWindow = 0;
                    fpsWindowStart = Clock::now();
                }
            }

            totalCycles += static_cast<uint64_t>(cycles);
            cyclesSinceCheck += static_cast<uint64_t>(cycles);
            if (cyclesSinceCheck < kPacingCheckCycles)
                continue;
            cyclesSinceCheck = 0;

            double multiplier = speedMultiplier.load(std::memory_order_relaxed);
            if (multiplier != lastMultiplier)
            {
                baseTime = Clock::now();
                baseCycles = totalCycles;
                lastMultiplier = multiplier;
                continue;
            }

            uint64_t cyclesSinceBase = totalCycles - baseCycles;
            double targetNs = static_cast<double>(cyclesSinceBase) * SleepTime() / multiplier;
            double elapsedNs = std::chrono::duration<double, std::nano>(Clock::now() - baseTime).count();

            if (elapsedNs < targetNs)
            {
                while (elapsedNs < targetNs)
                    elapsedNs = std::chrono::duration<double, std::nano>(Clock::now() - baseTime).count();
            }
            else if (elapsedNs - targetNs > kMaxCatchUpNs)
            {
                baseTime = Clock::now();
                baseCycles = totalCycles;
                elapsedNs = 0;
                cyclesSinceBase = 0;
            }

            // cpuspeed keeps its established meaning (ns actually spent per
            // cycle, over the batch just measured) for the existing
            // CPU-speed debug chart (NES/main.cpp) - a smaller value means
            // running faster.
            cpuspeed = cyclesSinceBase > 0 ? elapsedNs / static_cast<double>(cyclesSinceBase) : cpuspeed;
        }
    }

    int NES_CPU::SleepTime()
    {
        switch (mod)
        {
            case Mod::NTSC: return 559; // ~1.789773 MHz (~559 ns/cycle)
            case Mod::PAL: return 601;  // ~1.662607 MHz (~601 ns/cycle)
            default: return 0;
        }
    }
}
