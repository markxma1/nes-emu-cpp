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
#include "NES_Console.h"
#include "NES_Memory.h"
#include "NES_Register.h"
#include "NES_CPU.h"
#include "Interrupt.h"
#include "NES_PPU_Register.h"
#include "NES_PPU_Palette.h"
#include "NES_GamePad.h"
#include "NES_ROM.h"
#include "../NES_PPU/NES_PPU_Folder/NES_PPU.h"
#include "../NES_APU/NES_APU.h"
#include "../NES_APU/NES_APU_Register.h"

namespace NES
{
    std::mutex NES_Console::frameMutex;
    NES_PPU::Picture NES_Console::latestFrame(256, 240);
    std::atomic<bool> NES_Console::nameTableDebugWindowVisible{false};
    NES_PPU::Picture NES_Console::latestNameTableDebugOverlay(64 * 8, 60 * 8);
    std::atomic<bool> NES_Console::oamDebugWindowVisible{false};
    NES_PPU::Picture NES_Console::latestOAMDebugOverlay(256, 280);

    bool NES_Console::DrawRefresh() { return NES_PPU::DrawRefresh; }
    void NES_Console::DrawRefresh(bool v) { NES_PPU::DrawRefresh = v; }

    void NES_Console::INIT()
    {
        NES_Memory memory;
        // NES_Register and NES_CPU have no meaningful constructors in this
        // port (both are all-static classes, matching the C# `new
        // NES_Register()`/`new NES_CPU()` calls whose constructors did
        // nothing observable either) - nothing to instantiate for them.
        NES_PPU ppu;
        NES_GamePad gamepad;
        // New code, no C# equivalent to mirror - see NES_APU.h.
        NES_APU_Register apuRegister;

        // FIXED (was a preserved C# bug, now activated - see NES_CPU.cpp's
        // Sleep()/SleepTime() FIXED notes for the full story): `mod`
        // defaulted to `Mod::none` (no CPU throttling at all, i.e. running
        // as fast as the host machine can execute instructions) because
        // nothing ever selected NTSC/PAL - the C# original's CPUSpeedForm
        // debug tool that would have let a user pick one was never ported.
        // Every ROM this project has ever tested is an NTSC-region dump
        // (Galaga, Monster Truck Rally, Contra, Super Mario Bros., ... all
        // "(U)"/US releases), so NTSC is the correct fixed default absent
        // any actual region-selection UI - matches real NTSC NES hardware's
        // ~1.789773 MHz CPU clock instead of running arbitrarily fast.
        NES_CPU::mod = Mod::NTSC;

        // NEW, no C# equivalent - see NES_PPU::SetScanlineCallback()'s own
        // comment for why this indirection exists (NES_PPU has no
        // compile-time dependency on NES_ROM/Mapper). Registered once,
        // here, rather than resolved fresh on every call, since
        // NES_ROM::CurrentMapper() only ever changes on a ROM
        // load/switch - the lambda re-queries it each time it runs, so a
        // ROM switch after this still reaches the newly loaded mapper.
        NES_PPU::SetScanlineCallback([]() {
            if (Mapper* mapper = NES_ROM::CurrentMapper())
                mapper->OnScanline();
        });
    }

    void NES_Console::Run()
    {
        NES_Register::RessetPointer();
        NES_Register::P.Interrupt(false);
        Interrupt::POWER = true;
        NES_PPU_Register::InitialAtPower();
        NES_APU::Reset();
        NES_CPU::Run();
    }

    void NES_Console::Restart()
    {
        Interrupt::POWER = false;
        NES_Register::RessetPointer();
        NES_Register::P.Interrupt(false);
        Interrupt::POWER = true;
        NES_PPU_Register::InitialOnReset();
        NES_APU::Reset();
        NES_CPU::Run();
    }

    void NES_Console::Stop()
    {
        Interrupt::POWER = false;
    }

    void NES_Console::Resume()
    {
        Interrupt::POWER = true;
        NES_CPU::Run();
    }

    NES_PPU::Picture NES_Console::getPaletteTable()
    {
        return NES_PPU::PaletteTable();
    }

    NES_PPU::Picture NES_Console::getPatternTable(int PN)
    {
        return NES_PPU::PatternTable(PN);
    }

    NES_PPU::Picture NES_Console::getNameTabele(bool display)
    {
        return NES_PPU::NameTabele(display);
    }

    // FIXED (real regression, TSan-confirmed - a data race between this
    // function, called from the UI thread every time the debug Name Table
    // window redraws, and the CPU thread's own memory writes): this used
    // to call straight into NES_PPU::NameTabeleDebugOverlay() -> NameTabele()
    // -> DrowOneNameTable()/Tile(), which read NES_PPU_Memory/PatternTableN
    // directly - live PPU state the CPU thread (NES_CPU::Run(), running
    // continuously on its own thread - see NES/main.cpp's cpuThread)
    // concurrently writes via every $2006/$2007 access, completely
    // unsynchronized. TSan caught this immediately once exercised (headless
    // run with the debug window forced open): concurrent read/write pairs
    // on NES_PPU_Memory between NES::AddressSetup::Value() on the CPU
    // thread and NES::NES_PPU::Tile()/DrowOneNameTable() on the UI thread.
    // This is exactly the class of bug RenderFrame()'s own comment already
    // describes fixing for the *main* display buffer (getDisplay() "no
    // longer runs any PPU/mapper logic itself, so there is nothing left for
    // the UI thread to race with") - this debug-only path had quietly
    // regressed the same invariant. Fixed the same way: this function is
    // now a pure consumer, returning the most recently published snapshot
    // under frameMutex; the actual NameTabeleDebugOverlay() computation
    // moved to RenderFrame(), which only ever runs on the CPU thread (see
    // its own comment on nameTableDebugWindowVisible).
    NES_PPU::Picture NES_Console::getNameTabeleDebugOverlay()
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        return latestNameTableDebugOverlay;
    }

    // See getNameTabeleDebugOverlay()'s own comment - same producer
    // (RenderFrame(), CPU thread)/consumer (UI thread) split, same reason.
    NES_PPU::Picture NES_Console::getOAMDebugOverlay()
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        return latestOAMDebugOverlay;
    }

    // NEW, no C# equivalent - see getNameTabeleDebugOverlay()'s own comment
    // for the race this closes. NES/main.cpp calls this once per UI-thread
    // loop iteration with the debug Name Table window's current `visible`
    // state, so RenderFrame() below only pays NameTabeleDebugOverlay()'s
    // real cost (a full 4-quadrant, ~1920-tile rebuild - measured around
    // 2ms) on frames where the window is actually open, not on every single
    // real NES frame regardless (which would otherwise silently cost this
    // port ~2ms of *every* 16.67ms/frame CPU-thread budget - a real,
    // reproducible slowdown, not merely a race - the moment the debug view
    // got fixed to actually rebuild at all).
    void NES_Console::setNameTableDebugWindowVisible(bool visible)
    {
        nameTableDebugWindowVisible.store(visible, std::memory_order_relaxed);
    }

    void NES_Console::setOAMDebugWindowVisible(bool visible)
    {
        oamDebugWindowVisible.store(visible, std::memory_order_relaxed);
    }

    // FIXED (new design, not a C# port - the actual root cause behind this
    // session's Chip and Dale investigation): this used to run directly
    // inside getDisplay(), i.e. on the UI thread, once per NES/main.cpp
    // render-loop iteration - itself throttled only by cv::waitKeyEx(16)
    // (~60Hz *real* time), completely independent of how many CPU cycles the
    // separate CPU thread (NES_Console::Run() -> NES_CPU::Run()) had
    // actually executed since the last frame. Real hardware's PPU is wired
    // to the CPU by a fixed 3:1 clock ratio and raises vblank/NMI exactly
    // once per 89342 PPU dots = 29780.67 CPU cycles
    // (http://wiki.nesdev.com/w/index.php/Cycle_reference_chart) - nothing
    // like that fixed relationship existed here: a slow UI frame (window
    // unfocused, debug windows open, host machine briefly busy, ...) let far
    // more CPU instructions run between NMIs than real hardware ever would,
    // while a fast one could in principle undershoot it. Traced live via
    // Chip 'n Dale (MMC1): its cooperative task-scheduler's yield primitive
    // ($FF04) and its own NMI handler both perform unprotected, multi-write
    // MMC1 shift-register bank-select sequences - on real hardware, NMI's
    // true ~29780-cycle cadence makes interrupting one of those short
    // sequences astronomically rare, but this port's UI-driven NMI arrived
    // roughly every 1000-1300 instructions, constantly interleaving the two
    // write streams and corrupting MMC1's shift register / the scheduler's
    // own task-ID bookkeeping - a permanently black screen with the game's
    // music running at many times real speed (SevenClock/Sleep() no longer
    // being the bottleneck once those were separately fixed this session).
    //
    // Fixed by moving frame composition (this function's old body) into
    // RenderFrame(), invoked only from the CPU thread's own execution loop
    // (NES_CPU::Run(), gated on a real accumulated-CPU-cycle counter - see
    // its own comment) - matching real hardware's fixed CPU-cycle-driven
    // vblank cadence instead of wall-clock UI polling. getDisplay() (still
    // called from the UI thread) now only ever *reads* the most recently
    // published frame, under frameMutex; it no longer runs any PPU/mapper
    // logic itself, so there is nothing left for the UI thread to race with.
    //
    // This also incidentally closes the other data race TSan found this
    // session and this project's own Interrupt.h explicitly deferred as "a
    // much bigger, deliberately out-of-scope redesign" (NES_PPU_Register's
    // PPUSTATUS compound state, written by NES_PPU::Display() and read by
    // the CPU thread's own `LDA $2002`): Display() (and Mapper::OnScanline(),
    // which can raise Interrupt::IRQ() - see Mapper_MMC3::OnScanline()) now
    // only ever runs on the CPU thread, same as the read side, so that
    // "much bigger redesign" turned out to already be this one.
    //
    // NOTE: the `display` parameter is accepted but unused, matching the C#
    // original (NES.Console/NES_Console.cs getDisplay) - it's never actually
    // passed through to NES_PPU::Display(), which takes no arguments.
    NES_PPU::Picture NES_Console::getDisplay(bool /*display*/)
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        return latestFrame;
    }

    // UPDATE (later in the same overall effort - see NES_PPU::AdvanceDots()/
    // SetScanlineCallback()): Mapper::OnScanline() (default a no-op; only
    // Mapper_MMC3 overrides it - renamed from OnFrame(), which this
    // function used to call directly, once, right here) is now invoked
    // once per real visible scanline instead, via a callback INIT()
    // registers below - NES_PPU still has no compile-time dependency on
    // NES_ROM/Mapper, just no longer bridged from this exact call site.
    void NES_Console::RenderFrame()
    {
        NES_PPU::Picture frame = NES_PPU::Display();

        // See getNameTabeleDebugOverlay()/setNameTableDebugWindowVisible()'s
        // own comments: only pay for the debug Name Table rebuild when the
        // window is actually open, and always do it here (CPU thread) -
        // never from the UI thread, which is what caused the data race this
        // fixes. Computed *before* taking frameMutex below, same as
        // NES_PPU::Display() just above - keep the lock scope limited to
        // the actual publish, not the (comparatively expensive) rendering
        // work.
        bool wantsNameTableDebug = nameTableDebugWindowVisible.load(std::memory_order_relaxed);
        NES_PPU::Picture nameTableDebug(64 * 8, 60 * 8);
        if (wantsNameTableDebug)
            nameTableDebug = NES_PPU::NameTabeleDebugOverlay();

        // See setOAMDebugWindowVisible()/getOAMDebugOverlay()'s own
        // comments - same "only pay for it while the window is open, always
        // compute on the CPU thread" reasoning as the Name Table debug view
        // just above.
        bool wantsOAMDebug = oamDebugWindowVisible.load(std::memory_order_relaxed);
        NES_PPU::Picture oamDebug(256, 280);
        if (wantsOAMDebug)
            oamDebug = NES_PPU::OAMDebugOverlay();

        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame = frame;
        if (wantsNameTableDebug)
            latestNameTableDebugOverlay = nameTableDebug;
        if (wantsOAMDebug)
            latestOAMDebugOverlay = oamDebug;
    }

    NES_PPU::Color NES_Console::getUniversalBackgroundColor()
    {
        return NES_PPU_Palette::UniversalBackgroundColor();
    }

    void NES_Console::LoadRom(const std::string& path)
    {
        NES_ROM::LoadRom(path);
    }

    double NES_Console::getCPUSpeed()
    {
        return NES_CPU::cpuspeed;
    }

    double NES_Console::getMeasuredFPS()
    {
        return NES_CPU::measuredFPS.load(std::memory_order_relaxed);
    }

    double NES_Console::getSpeedMultiplier()
    {
        return NES_CPU::speedMultiplier.load(std::memory_order_relaxed);
    }

    void NES_Console::setSpeedMultiplier(double multiplier)
    {
        if (multiplier < NES_CPU::kMinSpeedMultiplier) multiplier = NES_CPU::kMinSpeedMultiplier;
        if (multiplier > NES_CPU::kMaxSpeedMultiplier) multiplier = NES_CPU::kMaxSpeedMultiplier;
        NES_CPU::speedMultiplier.store(multiplier, std::memory_order_relaxed);
    }

    uint8_t NES_Console::getMemoryByte(uint16_t address)
    {
        return NES_Memory::Memory[address]->value();
    }
}
