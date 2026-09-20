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
#include "Color.h"
#include "Picture.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace NES
{
    /// @brief Top-level wiring: constructs the CPU/PPU/Memory/Controller and
    /// exposes the handful of entry points the UI needs. Port of
    /// NES.Console/NES_Console.cs.
    ///
    /// SaveGame/LoadGame (Extras/SaveLoadMemory.cs) are not ported yet - out
    /// of scope for this first "core" pass, same bucket as the debug tool
    /// windows (Monitor/NameTable/PatternTable/CPUSpeed forms).
    class NES_Console
    {
    public:
        static bool DrawRefresh();
        static void DrawRefresh(bool v);

        static void INIT();
        static void Run();
        static void Restart();
        static void Stop();

        // NEW, no C# equivalent - user-requested save/load-state feature
        // (see NES_SaveState's own comment). Resumes the CPU instruction
        // loop exactly as-is, unlike Run()/Restart() (both of which
        // reinitialize registers/PPU/APU state first - a real power-on or
        // reset line pulse, neither of which a save/load should ever
        // trigger). NES/main.cpp's Q/E save/load key handling stops the
        // CPU thread, calls NES_SaveState::Save()/Load(), then starts a
        // new thread on this instead of Run()/Restart() - a Save() doesn't
        // change any state to begin with, and a Load() has already put
        // every register/memory cell exactly where it needs to be, so
        // re-running Run()'s own reset sequence here would just
        // immediately overwrite it.
        static void Resume();

        // NEW, no C# equivalent - see RenderFrame()'s own comment for the
        // full story (this is the fix for the Chip and Dale MMC1
        // shift-register corruption bug, and the deferred-as-out-of-scope
        // PPUSTATUS cross-thread race): composes exactly one real emulated
        // frame (NES_PPU::Display() - mapper->OnScanline() now happens
        // per-scanline instead, via NES_PPU::SetScanlineCallback(), see
        // INIT()) and publishes it for getDisplay() to hand out. Called
        // from the CPU thread (NES_CPU::Run()) once per completed
        // 262-scanline sweep (NES_PPU::AdvanceDots()) - never from the UI
        // thread.
        static void RenderFrame();

        static NES_PPU::Picture getPaletteTable();
        static NES_PPU::Picture getPatternTable(int PN);
        static NES_PPU::Picture getNameTabele(bool display = true);
        static NES_PPU::Picture getNameTabeleDebugOverlay();
        static NES_PPU::Picture getDisplay(bool display = true);
        static NES_PPU::Color getUniversalBackgroundColor();

        // NEW, no C# equivalent - same producer (CPU thread, RenderFrame())/
        // consumer (UI thread) hand-off as getNameTabeleDebugOverlay(), for
        // the same reason (see NES_PPU::OAMDebugOverlay()'s own comment for
        // what this shows and why): a plain read of live OAM state from the
        // UI thread would race the CPU thread's concurrent OAM DMA/writes,
        // the exact TSan-confirmed bug class getNameTabeleDebugOverlay()
        // already fixed once for the Name Table window.
        static NES_PPU::Picture getOAMDebugOverlay();

        // NEW, no C# equivalent - see RenderFrame()'s own comment on
        // nameTableDebugWindowVisible/latestNameTableDebugOverlay for the
        // full story (a real, TSan-confirmed data race: the debug Name
        // Table window used to call straight into NES_PPU::NameTabele(),
        // reading live NES_PPU_Memory from the UI thread while the CPU
        // thread concurrently wrote it). NES/main.cpp calls this once per
        // UI-thread loop iteration, passing the debug window's current
        // `visible` state, so the CPU thread knows whether it's worth
        // paying to rebuild the (fairly expensive - ~2ms) Name Table
        // snapshot this frame.
        static void setNameTableDebugWindowVisible(bool visible);

        // NEW, no C# equivalent - same purpose as
        // setNameTableDebugWindowVisible() above, for the OAM Viewer debug
        // window (see NES_PPU::OAMDebugOverlay()'s own comment).
        static void setOAMDebugWindowVisible(bool visible);

        static void LoadRom(const std::string& path);

        static double getCPUSpeed();

        // NEW, no C# equivalent - real, measured frames/second, see
        // NES_CPU::measuredFPS's own comment.
        static double getMeasuredFPS();

        // NEW, no C# equivalent - user-requested speed control, see
        // NES_CPU::speedMultiplier's own comment for the full story.
        // setSpeedMultiplier() clamps into
        // [NES_CPU::kMinSpeedMultiplier, NES_CPU::kMaxSpeedMultiplier] so a
        // runaway key-repeat can't drive it to zero/negative (which would
        // make Sleep() divide by zero or run backwards) or to an
        // absurdly large value.
        static double getSpeedMultiplier();
        static void setSpeedMultiplier(double multiplier);

        /// Debug-only: the raw byte currently stored at a CPU address, for
        /// the memory-viewer debug window (NES/main.cpp) - uses
        /// AddressSetup::value() (unhooked), not Value(), so merely looking
        /// at memory can never itself trigger a real side effect (e.g.
        /// clearing PPUSTATUS's vblank flag by "reading" $2002, or shifting
        /// the controller's serial data by "reading" $4016 - see
        /// NES_PPU_Register.cpp/NES_GamePad.cpp for those hooks).
        static uint8_t getMemoryByte(uint16_t address);

    private:
        // NEW, no C# equivalent - the only piece of state the CPU thread
        // (RenderFrame(), called from NES_CPU::Run()) and the UI thread
        // (getDisplay(), called from NES/main.cpp's render loop) still need
        // to hand off between each other after this session's cycle-accurate
        // rendering fix (see RenderFrame()'s own comment): the most recently
        // composed frame. A plain `Picture` copy under a `std::mutex` is the
        // simplest correct hand-off for "producer writes occasionally,
        // consumer reads the latest value" - deliberately not a lock-free
        // double-buffer or similar, since Picture copies here are cheap
        // (256x240) and happen at most ~60/sec, nowhere near a contended hot
        // path.
        static std::mutex frameMutex;
        static NES_PPU::Picture latestFrame;

        // NEW, no C# equivalent - same producer/consumer hand-off pattern
        // as latestFrame/frameMutex above, added for the debug Name Table
        // window specifically (see RenderFrame()'s own comment). `atomic`
        // rather than needing frameMutex to read/write, since it's just a
        // single bool checked once per CPU-thread frame and set once per
        // UI-thread loop iteration - no larger critical section needed.
        static std::atomic<bool> nameTableDebugWindowVisible;
        static NES_PPU::Picture latestNameTableDebugOverlay;

        // NEW, no C# equivalent - same hand-off pattern as
        // nameTableDebugWindowVisible/latestNameTableDebugOverlay above,
        // for the OAM Viewer debug window.
        static std::atomic<bool> oamDebugWindowVisible;
        static NES_PPU::Picture latestOAMDebugOverlay;
    };
}
