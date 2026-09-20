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
/// @brief Entry point. This is new code, not a port of any single C# file -
/// per the porting brief, only the UI shell is allowed to differ from the
/// C# original. It replaces two C# pieces at once:
///
///  - NES/Program.cs + NES/MainForm.cs: `NES_Console::INIT()`, loading a ROM
///    and starting `NES_Console::Run()` on a background thread (the C#
///    original used a WinForms `BackgroundWorker`; here it's a plain
///    `std::thread`, matching backgroundWorker1_DoWork).
///  - NES/Forms/Monitor.cs: polling `NES_Console::getDisplay()` and drawing
///    it, originally on a WinForms `timer1_Tick`; here it's a plain loop
///    around `cv::imshow`.
///
/// NOTE: the C# original never actually wired keyboard (or any other) input
/// to NES_GamePad::Player1 anywhere in the solution - Player1/Player2/3/4's
/// Button map is only ever default-constructed, never written to outside of
/// NES_GamePad.cs itself (checked: no KeyDown/KeyPress/KeyUp handler exists
/// in any .cs file). So the original, run as-is, has no way to actually
/// play a game interactively. Since this file is pure new UI-shell code
/// (not a port of an existing class), input wiring is added below as a
/// UI-layer addition, not a "fix" to any ported logic - it only calls the
/// pre-existing, already-ported NES_GamePad::Player1 public API the
/// original class always had. See InputSource.h for how that wiring is
/// structured: main.cpp only ever asks "is this button down, across
/// whatever InputSources are active" - it doesn't know or care whether a
/// keyboard or a gamepad answered.
///
/// The three debug windows below (NameTable/PatternTable/CPU speed) are the
/// same UI-shell-only replacement for the C# original's NES/Forms/NameTable.cs,
/// PatternTable.cs and CPUSpeedForm.cs: those were WinForms windows opened
/// from buttons on MainForm and refreshed on a timer, reading the exact same
/// NES_Console::getNameTabele/getPatternTable/getPaletteTable/getCPUSpeed
/// entry points this port already exposes unchanged. Only the window
/// toggling (keys instead of buttons) and the CPU-speed line chart (drawn
/// directly with OpenCV instead of a WinForms Chart control) are new here -
/// the actual debug data comes from already-ported, unmodified core code.
#include <opencv2/opencv.hpp>

#include "NES_Console.h"
#include "NES_SaveState.h"
#include "NES_GamePad.h"
#include "Interrupt.h"
#include "../INES/INES.h"
#include "NES_PPU_Register.h"
#include "NES_PPU_OAM.h"
#include "../NES_PPU/NES_PPU_Folder/NES_PPU.h"
#include "../NES_PPU/Memory/NES_PPU_Memory.h"
#include "NES_Register.h"
#include "NES_Memory.h"
#include "Color.h"
#include "Picture.h"
#include "InputSource.h"
#include "KeyboardInputSource.h"
#include "GamepadInputSource.h"
#include "NES_Audio.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace
{
    /// Integer upscale factor for the 256x240 NES framebuffer - purely a
    /// display convenience (UI layer), has no ported C# equivalent.
    constexpr int kScale = 3;

    void SetButton(NES::NES_GamePad::Controller& pad, const std::string& name, bool down)
    {
        for (auto& b : pad.Button)
        {
            if (b.first == name)
            {
                b.second = down;
                return;
            }
        }
    }

    // NEW, no C# equivalent - see the input record/playback feature below
    // (recordedEvents/NES_PLAYBACK_INPUT). Mirrors SetButton()'s own lookup.
    bool GetButton(const NES::NES_GamePad::Controller& pad, const std::string& name)
    {
        for (const auto& b : pad.Button)
            if (b.first == name)
                return b.second;
        return false;
    }

    /// The 8 NES buttons, in the fixed order the remap menu walks through
    /// them (matches Controller's real-hardware read order - see
    /// NES_GamePad.h - purely for a sensible prompt sequence, not a
    /// functional requirement).
    const std::vector<std::string> kButtonNames = { "U", "D", "L", "R", "A", "B", "START", "SELECT" };

    /// Every InputSource that's actually usable this run. Populated once in
    /// main() below; the game loop just asks each one "is X down" and ORs
    /// the answers together - see InputSource.h.
    std::vector<std::unique_ptr<NES::InputSource>> inputSources;

    void PollInputSources()
    {
        for (auto& src : inputSources)
            if (src->Available())
                src->Poll();
    }

    // NEW, no C# equivalent (WinForms key events are inherently
    // window-scoped, so this never came up in the original) - found live:
    // KeyboardInputSource reads raw /dev/input/eventN nodes directly (see
    // its own constructor comment on why - it needs to work under Wayland
    // compositors, which don't let a normal application see key events for
    // windows it doesn't own at all, unlike X11), which has no concept of
    // "window focus" whatsoever - every physical keypress reached
    // IsDown() and got applied to Player1 no matter which application, if
    // any, the user was actually typing into at the time. Confirmed live:
    // typing normally into another window (this very terminal) pressed NES
    // gameplay buttons and toggled this app's own debug-window hotkeys.
    //
    // hyprctl (Hyprland's own control socket CLI, already present on this
    // user's system - see this project's README on the target desktop) is
    // the simplest reliable source of "which window is actually focused
    // right now" available here; matched by PID (this process's own,
    // std::getpid()) rather than window title, since two ROMs happening to
    // produce the same window title would otherwise collide. Deliberately
    // Hyprland-specific rather than a generic X11/portable check: this is a
    // personal project run only on this user's own Hyprland desktop, not
    // software meant for arbitrary window managers - see this project's own
    // README/CLAUDE.md on scope. Throttled to once every kFocusCheckEvery
    // UI ticks (a few times/sec is more than enough to catch a focus change
    // before it matters, and popen()ing a whole new process every single
    // ~16ms tick would be wasteful) with the last result cached in between.
    // Falls back to "assume focused" (this port's old, unconditional
    // behavior) whenever hyprctl is missing or a query fails for any reason
    // - e.g. a different compositor - rather than silently breaking input
    // there instead of just not fixing this specific bug on this system.
    bool WindowHasFocus()
    {
        constexpr int kFocusCheckEvery = 6; // ~10x/sec at the UI's ~60Hz tick rate
        static int ticksSinceCheck = kFocusCheckEvery; // force a check on the first call
        static bool cachedFocused = true;
        static bool hyprctlChecked = false;
        static bool hyprctlAvailable = false;

        if (!hyprctlChecked)
        {
            hyprctlAvailable = (std::system("command -v hyprctl >/dev/null 2>&1") == 0);
            hyprctlChecked = true;
        }
        if (!hyprctlAvailable)
            return true;

        if (++ticksSinceCheck < kFocusCheckEvery)
            return cachedFocused;
        ticksSinceCheck = 0;

        FILE* pipe = popen("hyprctl activewindow -j 2>/dev/null", "r");
        if (!pipe)
            return cachedFocused = true;

        std::string output;
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe))
            output += buf;
        pclose(pipe);

        const std::string pidNeedle = "\"pid\": " + std::to_string(static_cast<long>(getpid()));
        cachedFocused = output.find(pidNeedle) != std::string::npos;
        return cachedFocused;
    }

    bool AnySourceDown(const std::string& name)
    {
        for (auto& src : inputSources)
            if (src->Available() && src->IsDown(name))
                return true;
        return false;
    }

    void ApplyInputSourcesToPlayer1()
    {
        for (const auto& name : kButtonNames)
            SetButton(NES::NES_GamePad::Player1, name, AnySourceDown(name));
    }

    /// TEMPORARY diagnostic aid, not a port of anything - opt-in via
    /// NES_TRACE_INPUT=1, off by default. Prints each InputSource's
    /// Available()/IsDown() state for all 8 buttons about once a second, to
    /// see exactly where a "buttons don't respond" report breaks down:
    /// no source available at all, a source available but never reporting
    /// anything down, or values that look right here but Player1 still
    /// doesn't move (which would point elsewhere, e.g. NES_GamePad itself).
    void TraceInputSourcesIfRequested(long uiFrame)
    {
        if (!std::getenv("NES_TRACE_INPUT") || uiFrame % 60 != 0)
            return;
        for (auto& src : inputSources)
        {
            std::cerr << "[NES_TRACE_INPUT] " << src->Name()
                       << " Available=" << src->Available();
            if (src->Available())
                for (const auto& name : kButtonNames)
                    std::cerr << " " << name << "=" << src->IsDown(name);
            std::cerr << std::endl;
        }
    }

    /// New: a menu (triggered by M for keyboard, G for gamepad - see the
    /// controls printout in main()) that walks through the 8 buttons above,
    /// asking the player to press whatever they want bound to each one, and
    /// saves the result via the active InputSource's own SaveBindings().
    /// Drawn as a text overlay on the main game window (cv::putText onto the
    /// same canvas the game would otherwise show) rather than a separate
    /// window - simplest thing that works given this project has no other
    /// text-rendering system.
    struct RemapState
    {
        NES::InputSource* source = nullptr;
        size_t buttonIndex = 0;

        bool Active() const { return source != nullptr; }

        void Begin(NES::InputSource* s)
        {
            source = s;
            buttonIndex = 0;
        }

        void Cancel() { source = nullptr; }

        /// Called once per frame while active. Returns true once remapping
        /// has finished (all buttons bound) or been cancelled.
        void Tick()
        {
            if (!Active())
                return;
            std::string captured = source->PollForCapture();
            if (captured.empty())
                return;
            source->Bind(kButtonNames[buttonIndex], captured);
            source->SaveBindings();
            ++buttonIndex;
            if (buttonIndex >= kButtonNames.size())
                source = nullptr; // done
        }

        void DrawOverlay(cv::Mat& canvas) const
        {
            cv::rectangle(canvas, { 0, 0 }, { canvas.cols, canvas.rows }, cv::Scalar(20, 20, 20), cv::FILLED);
            std::string title = "Remapping " + source->Name() + " - press a button for:";
            cv::putText(canvas, title, { 20, 40 }, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            std::string current = kButtonNames[buttonIndex];
            cv::putText(canvas, current, { 20, 100 }, cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(0, 220, 0), 2, cv::LINE_AA);
            cv::putText(canvas, "(Esc to cancel)", { 20, canvas.rows - 20 }, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(160, 160, 160), 1, cv::LINE_AA);
        }
    };

    RemapState remapState;

    /// Lists *.nes files sitting next to the executable and in a "roms"
    /// subfolder of it (both relative to cwd, which ChdirToExecutableDir
    /// below already points at the executable's own directory). Not
    /// recursive - keeps this a simple flat picker rather than a full file
    /// manager. New code, no C# equivalent (see RomSelector below).
    /// Extra directories to search, one per line, read from "./romdirs.cfg"
    /// next to the executable if that file exists - e.g. an existing
    /// RetroPie/EmulationStation ROM collection living elsewhere on disk,
    /// which nobody would want to copy into this project's own folder.
    /// Blank lines and lines starting with '#' are ignored. Plain text, safe
    /// to hand-edit - same convention as keyboard.cfg/gamepad.cfg.
    std::vector<std::string> LoadExtraRomDirs()
    {
        std::vector<std::string> dirs;
        std::ifstream f("./romdirs.cfg");
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            dirs.push_back(line);
        }
        return dirs;
    }

    std::vector<std::string> ScanForRoms()
    {
        namespace fs = std::filesystem;
        std::vector<std::string> result;
        auto scanDir = [&](const fs::path& dir)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return;
            // Recursive: a real ROM collection (e.g. an existing
            // RetroPie/EmulationStation "roms/nes" folder) commonly has its
            // own subfolders (by genre, collection, etc.) - a flat listing
            // would silently miss everything not directly in `dir`.
            for (const auto& entry : fs::recursive_directory_iterator(
                     dir, fs::directory_options::skip_permission_denied, ec))
            {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".nes")
                    result.push_back(entry.path().string());
            }
        };
        scanDir(".");
        scanDir("./roms");
        for (const auto& dir : LoadExtraRomDirs())
            scanDir(dir);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    /// New: an in-window ROM browser (key L) listing whatever *.nes files
    /// ScanForRoms() finds, navigated with the D-Pad and A/Start to load,
    /// B/Esc to cancel - reusing the same InputSource abstraction gameplay
    /// already uses (see AnySourceDown above), so it works from a gamepad
    /// too, not just the keyboard. Not a port of anything: the C# original
    /// only had a WinForms File > Open dialog (MainForm.cs), tied to
    /// Windows/WinForms and not meaningfully portable as "the same class".
    /// This struct only decides *which* path was picked; main() is the one
    /// that actually stops/reloads/restarts the emulator (see SwitchRom()),
    /// since only it has access to the CPU thread.
    struct RomSelector
    {
        bool active = false;
        std::vector<std::string> entries;
        int selectedIndex = 0;
        std::string pendingLoad; // non-empty for exactly one Tick() once confirmed

        bool prevUp = false, prevDown = false, prevA = false, prevB = false;

        bool Active() const { return active; }

        void Open()
        {
            entries = ScanForRoms();
            selectedIndex = 0;
            active = true;
            // Seed "previous" state from whatever's down *right now*, not a
            // hardcoded false - a still-held key/button from whatever
            // gesture opened this menu (or a gamepad reporting a stale/
            // "ghost" press - see GamepadInputSource.h's note on wireless
            // receivers with no pad attached) would otherwise read as a
            // brand-new press on the very first Tick() and instantly close
            // the menu again before a single frame of it is ever shown.
            prevUp = AnySourceDown("U");
            prevDown = AnySourceDown("D");
            prevA = AnySourceDown("A") || AnySourceDown("START");
            prevB = AnySourceDown("B");
        }

        void Tick()
        {
            pendingLoad.clear();
            if (!active)
                return;

            bool up = AnySourceDown("U");
            bool down = AnySourceDown("D");
            bool a = AnySourceDown("A") || AnySourceDown("START");
            bool b = AnySourceDown("B");

            if (up && !prevUp && !entries.empty())
                selectedIndex = (selectedIndex + static_cast<int>(entries.size()) - 1)
                               % static_cast<int>(entries.size());
            if (down && !prevDown && !entries.empty())
                selectedIndex = (selectedIndex + 1) % static_cast<int>(entries.size());
            if (a && !prevA && !entries.empty())
            {
                pendingLoad = entries[static_cast<size_t>(selectedIndex)];
                active = false;
            }
            if (b && !prevB)
                active = false;

            prevUp = up;
            prevDown = down;
            prevA = a;
            prevB = b;
        }

        void DrawOverlay(cv::Mat& canvas) const
        {
            cv::rectangle(canvas, { 0, 0 }, { canvas.cols, canvas.rows }, cv::Scalar(20, 20, 20), cv::FILLED);
            cv::putText(canvas, "Select a ROM  (Up/Down, A/Start = load, B/Esc = cancel)",
                        { 20, 30 }, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            if (entries.empty())
            {
                cv::putText(canvas, "No .nes files found next to the executable or in ./roms",
                            { 20, 70 }, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(160, 160, 160), 1, cv::LINE_AA);
                return;
            }
            constexpr int kLineHeight = 24;
            constexpr int kMaxVisible = 14;
            int first = std::max(0, selectedIndex - kMaxVisible / 2);
            first = std::min(first, std::max(0, static_cast<int>(entries.size()) - kMaxVisible));
            int y = 70;
            for (int i = first; i < static_cast<int>(entries.size()) && i < first + kMaxVisible; ++i)
            {
                bool sel = i == selectedIndex;
                cv::Scalar color = sel ? cv::Scalar(0, 220, 0) : cv::Scalar(200, 200, 200);
                // Filename only, not the full path - a real ROM collection
                // (see LoadExtraRomDirs()) can have deeply nested paths that
                // would run off the edge of the overlay.
                std::string name = std::filesystem::path(entries[static_cast<size_t>(i)]).filename().string();
                std::string line = (sel ? "> " : "  ") + name;
                cv::putText(canvas, line, { 20, y }, cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
                y += kLineHeight;
            }
        }
    };

    RomSelector romSelector;
}

namespace
{
    /// One toggleable debug window: owns its own cv window (created lazily
    /// on first show, torn down on hide/exit) and its "is it open" state -
    /// mirrors the C# original's separate NameTable/PatternTable/CPUSpeedForm
    /// windows, minus the WinForms plumbing.
    struct DebugWindow
    {
        std::string name;
        bool visible = false;

        explicit DebugWindow(std::string windowName) : name(std::move(windowName)) {}

        void toggle()
        {
            visible = !visible;
            if (visible)
                cv::namedWindow(name, cv::WINDOW_AUTOSIZE);
            else
                cv::destroyWindow(name);
        }

        /// The user can also close a debug window via its own [x] button;
        /// detect that so `visible` stays in sync (same check the main
        /// window's loop already does for itself).
        void syncClosedByUser()
        {
            if (visible && cv::getWindowProperty(name, cv::WND_PROP_VISIBLE) < 1)
                visible = false;
        }
    };

    DebugWindow nameTableWindow("NES - Name Table");
    DebugWindow patternTableWindow("NES - Pattern Table");
    DebugWindow cpuSpeedWindow("NES - CPU Speed");
    DebugWindow memoryWindow("NES - Memory Viewer");
    // NEW, no C# equivalent - see NES_PPU::OAMDebugOverlay()'s own comment
    // for what this shows. 'U' rather than an 'S'/'B' sprite-related
    // mnemonic because both collide with existing gameplay keys (S = Down
    // in the WASD D-pad scheme; B doesn't collide but reads as "press the
    // NES B button", which this isn't).
    DebugWindow oamWindow("NES - OAM Viewer");
    /// Palette number fed to NES_Console::getPatternTable(PN) - cycled with
    /// 'O' while the pattern-table window is open. Matches PatternTable.cs's
    /// button1_click, minus its 0..10 wraparound quirk (that range only ever
    /// made sense against the C# form's own state, not this key binding).
    int patternTablePalette = 0;

    /// NEW, no C# equivalent - user-requested save/load-state feature (see
    /// NES_SaveState's own comment). Selected with digit keys '1'-'9'
    /// (see HandleDebugKey()); 'Q'/'E' act on whichever slot this
    /// currently is. Starts at 1 rather than 0 so an accidental Q/E before
    /// ever touching a digit key still does something sensible (slot 1)
    /// rather than a slot number nobody chose on purpose.
    int saveStateSlot = 1;

    /// NEW, no C# equivalent - the currently-loaded ROM's path, kept in
    /// sync by main()/switchRom() below (there was previously no need to
    /// remember this past the initial NES_Console::LoadRom() call). Used
    /// only to name save-state files after the ROM they belong to (see
    /// SaveStatePath() below) - not read by anything gameplay-related.
    std::string currentRomPath;

    /// NEW, no C# equivalent - see NES_SaveState's own comment. One file
    /// per (ROM, slot) pair, next to the executable (matching this port's
    /// existing "look next to the executable" convention - see
    /// ChdirToExecutableDir()) so slots from different games never collide
    /// even if their internal filenames happen to match.
    std::string SaveStatePath(int slot)
    {
        std::string base = currentRomPath;
        size_t lastSlash = base.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            base = base.substr(lastSlash + 1);
        return "./" + base + ".slot" + std::to_string(slot) + ".sav";
    }

    // NEW, no C# equivalent - user-requested input record/playback feature,
    // added specifically to make headless bug investigation reliable: this
    // session's own synthetic auto-input (NES_AUTO_RIGHT_FRAME etc.) kept
    // producing subtly wrong in-game results compared to a real play
    // session (e.g. an object's own action-state byte staying at its idle
    // value despite a simulated jump button being held) - close enough to
    // *look* plausible, but not a faithful stand-in for a real recorded
    // sequence of presses at their real frame numbers. Recording only
    // button-state *changes* (not every tick) keeps the file small and
    // human-readable; playback re-applies "currently held" every frame
    // between changes, the same way the existing auto-input flags already
    // do.
    //
    // File format: one line per change, "<frame> <button> <0|1>", e.g.
    // "90 A 1" (A pressed at frame 90) then "120 A 0" (released at frame
    // 120). Frame numbers are the same `uiFrame` NES_AUTO_QUIT_FRAME/etc.
    // already use, so a recording and NES_AUTO_LOAD_SAVE/NES_AUTO_QUIT_FRAME
    // combine directly - "load this save, then play back this recording,
    // then dump a frame at frame N" is one command line.
    bool recordingInput = false;
    long recordingStartFrame = 0;
    std::vector<std::tuple<long, std::string, bool>> recordedEvents;
    std::unordered_map<std::string, bool> recordedPrevState;

    void StartRecording(long uiFrame)
    {
        recordingInput = true;
        recordingStartFrame = uiFrame;
        recordedEvents.clear();
        recordedPrevState.clear();
        for (const auto& b : NES::NES_GamePad::Player1.Button)
            recordedPrevState[b.first] = b.second;
        std::cout << "Recording input... press Y again to stop and save." << std::endl;
    }

    void StopRecording(const std::string& romPath)
    {
        recordingInput = false;
        std::string base = romPath;
        size_t lastSlash = base.find_last_of("/\\");
        if (lastSlash != std::string::npos)
            base = base.substr(lastSlash + 1);
        std::string path = "./" + base + ".inputs.txt";
        std::ofstream f(path);
        if (!f)
        {
            std::cerr << "Recording: FAILED to write " << path << std::endl;
            return;
        }
        for (const auto& [frame, button, down] : recordedEvents)
            f << frame << " " << button << " " << (down ? 1 : 0) << "\n";
        std::cout << "Recording stopped: " << recordedEvents.size() << " events, "
                  << (recordedEvents.empty() ? 0 : std::get<0>(recordedEvents.back()) - recordingStartFrame)
                  << " frames -> " << path << std::endl;
    }

    /// Called once per UI-thread loop iteration, after this frame's real
    /// input (or auto-input overrides) have been applied to Player1 - logs
    /// any button that changed since the previous frame.
    void RecordInputTick(long uiFrame)
    {
        if (!recordingInput)
            return;
        for (const auto& b : NES::NES_GamePad::Player1.Button)
        {
            bool& prev = recordedPrevState[b.first];
            if (b.second != prev)
            {
                recordedEvents.emplace_back(uiFrame, b.first, b.second);
                prev = b.second;
            }
        }
    }

    /// Parses a recording file (see StartRecording()'s own comment on the
    /// format) into frame -> [(button, down)] changes, for
    /// NES_PLAYBACK_INPUT to replay.
    std::map<long, std::vector<std::pair<std::string, bool>>> LoadPlaybackFile(const std::string& path)
    {
        std::map<long, std::vector<std::pair<std::string, bool>>> events;
        std::ifstream f(path);
        if (!f)
        {
            std::cerr << "Failed to open playback file: " << path << std::endl;
            return events;
        }
        long frame;
        std::string button;
        int down;
        while (f >> frame >> button >> down)
            events[frame].emplace_back(button, down != 0);
        std::cout << "Loaded playback: " << path << " (" << events.size() << " frame(s) with changes)"
                  << std::endl;
        return events;
    }

    /// Rolling history of NES_Console::getCPUSpeed() samples (nanoseconds
    /// per emulated CPU step - see CPU/CPU/NES_CPU.cpp's Sleep(), whose
    /// FIXED note explains the unit) for the CPU-speed window's line chart,
    /// replacing CPUSpeedForm.cs's WinForms Chart control.
    std::deque<double> cpuSpeedHistory;
    constexpr size_t kCpuSpeedHistoryLen = 300;

    void DrawCpuSpeedChart()
    {
        double speed = NES::NES_Console::getCPUSpeed();
        cpuSpeedHistory.push_back(speed);
        while (cpuSpeedHistory.size() > kCpuSpeedHistoryLen)
            cpuSpeedHistory.pop_front();

        constexpr int W = 600, H = 240, margin = 30;
        cv::Mat canvas(H, W, CV_8UC3, cv::Scalar(20, 20, 20));

        double maxSpeed = 1.0;
        for (double v : cpuSpeedHistory)
            maxSpeed = std::max(maxSpeed, v);

        cv::line(canvas, { margin, H - margin }, { W - 10, H - margin }, cv::Scalar(90, 90, 90), 1);
        cv::line(canvas, { margin, 10 }, { margin, H - margin }, cv::Scalar(90, 90, 90), 1);

        if (cpuSpeedHistory.size() > 1)
        {
            int plotW = W - margin - 10;
            int plotH = H - margin - 10;
            cv::Point prev;
            for (size_t i = 0; i < cpuSpeedHistory.size(); i++)
            {
                int x = margin + static_cast<int>(static_cast<double>(i) / static_cast<double>(kCpuSpeedHistoryLen - 1) * plotW);
                int y = (H - margin) - static_cast<int>((cpuSpeedHistory[i] / maxSpeed) * plotH);
                cv::Point p{ x, y };
                if (i > 0)
                    cv::line(canvas, prev, p, cv::Scalar(0, 220, 0), 1, cv::LINE_AA);
                prev = p;
            }
        }

        std::ostringstream label;
        label.precision(3);
        label << std::fixed << speed << " ns/step  " << std::setprecision(1) << NES::NES_Console::getMeasuredFPS()
              << " FPS  (" << NES::NES_Console::getSpeedMultiplier() << "x, +/-/0)";
        cv::putText(canvas, label.str(), { margin, 24 }, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

        cv::imshow(cpuSpeedWindow.name, canvas);
    }

    /// New tool, no C# equivalent (Extras/SaveLoadMemory.cs never had a live
    /// viewer, only save/load) - a live hex dump of NES_Memory::Memory[],
    /// same toggleable-window pattern as NameTable/PatternTable/CPU Speed
    /// above. Reads via NES_Console::getMemoryByte() (AddressSetup::value(),
    /// unhooked), so merely looking at a page of memory never itself
    /// triggers a real hardware side effect.
    uint16_t memoryViewBase = 0x0000;
    constexpr int kMemoryViewCols = 16;
    constexpr int kMemoryViewRows = 16;

    void ScrollMemoryView(int deltaPages)
    {
        int newBase = static_cast<int>(memoryViewBase) + deltaPages * kMemoryViewCols * kMemoryViewRows;
        if (newBase < 0)
            newBase = 0;
        if (newBase > 0xFFFF)
            newBase = 0xFFFF;
        memoryViewBase = static_cast<uint16_t>(newBase);
    }

    void DrawMemoryViewer()
    {
        constexpr int cellW = 24, cellH = 16;
        constexpr int leftMargin = 52, topMargin = 34;
        constexpr int W = leftMargin + kMemoryViewCols * cellW + 8;
        constexpr int H = topMargin + kMemoryViewRows * cellH + 10;
        cv::Mat canvas(H, W, CV_8UC3, cv::Scalar(20, 20, 20));

        std::ostringstream title;
        title << "$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << memoryViewBase
              << "  ([ / ] = page,  { / } = 4K jump)";
        cv::putText(canvas, title.str(), { 6, 14 }, cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(200, 200, 200), 1, cv::LINE_AA);

        for (int c = 0; c < kMemoryViewCols; c++)
        {
            std::ostringstream h;
            h << std::hex << std::uppercase << c;
            cv::putText(canvas, h.str(), { leftMargin + c * cellW + 6, topMargin - 6 },
                        cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(150, 150, 90), 1, cv::LINE_AA);
        }

        for (int r = 0; r < kMemoryViewRows; r++)
        {
            int rowAddr = static_cast<int>(memoryViewBase) + r * kMemoryViewCols;
            std::ostringstream addrLabel;
            addrLabel << "$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << (rowAddr & 0xFFFF);
            cv::putText(canvas, addrLabel.str(), { 4, topMargin + r * cellH + 11 },
                        cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(180, 180, 180), 1, cv::LINE_AA);

            for (int c = 0; c < kMemoryViewCols; c++)
            {
                uint16_t addr = static_cast<uint16_t>((rowAddr + c) & 0xFFFF);
                uint8_t v = NES::NES_Console::getMemoryByte(addr);
                std::ostringstream cell;
                cell << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(v);
                // Zero bytes dimmed so non-zero/"interesting" regions stand
                // out at a glance, same idea as most hex-editor UIs.
                cv::Scalar color = v == 0 ? cv::Scalar(80, 80, 80) : cv::Scalar(0, 220, 0);
                cv::putText(canvas, cell.str(), { leftMargin + c * cellW, topMargin + r * cellH + 11 },
                            cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1, cv::LINE_AA);
            }
        }
        cv::imshow(memoryWindow.name, canvas);
    }

    void UpdateDebugWindows()
    {
        nameTableWindow.syncClosedByUser();
        patternTableWindow.syncClosedByUser();
        cpuSpeedWindow.syncClosedByUser();
        memoryWindow.syncClosedByUser();
        oamWindow.syncClosedByUser();

        // See NES_Console::setNameTableDebugWindowVisible()'s own comment -
        // tells the CPU thread whether it's worth rebuilding the debug Name
        // Table snapshot this frame; called every UI-thread iteration
        // (cheap - a single atomic store) so a window closed via its own
        // [x] button (caught by syncClosedByUser() above) is reflected just
        // as promptly as one closed via the 'N' key.
        NES::NES_Console::setNameTableDebugWindowVisible(nameTableWindow.visible);

        if (nameTableWindow.visible)
        {
            NES_PPU::Picture nameTable = NES::NES_Console::getNameTabeleDebugOverlay();
            cv::imshow(nameTableWindow.name, nameTable.Image());
        }
        if (patternTableWindow.visible)
        {
            NES_PPU::Picture patterns = NES::NES_Console::getPatternTable(patternTablePalette);
            NES_PPU::Picture palette = NES::NES_Console::getPaletteTable();
            cv::Mat patImg = patterns.Image();
            cv::Mat palImg = palette.Image();
            cv::Mat combined(patImg.rows + palImg.rows, std::max(patImg.cols, palImg.cols), CV_8UC3, cv::Scalar(0, 0, 0));
            patImg.copyTo(combined(cv::Rect(0, 0, patImg.cols, patImg.rows)));
            palImg.copyTo(combined(cv::Rect(0, patImg.rows, palImg.cols, palImg.rows)));
            cv::imshow(patternTableWindow.name, combined);
        }
        if (cpuSpeedWindow.visible)
            DrawCpuSpeedChart();
        if (memoryWindow.visible)
            DrawMemoryViewer();

        // See NES_Console::setOAMDebugWindowVisible()'s own comment - same
        // "tell the CPU thread whether it's worth the rebuild" reasoning as
        // the Name Table window above.
        NES::NES_Console::setOAMDebugWindowVisible(oamWindow.visible);
        if (oamWindow.visible)
        {
            NES_PPU::Picture oam = NES::NES_Console::getOAMDebugOverlay();
            cv::imshow(oamWindow.name, oam.Image());
        }
    }

    /// Debug-window toggle keys, one-shot actions rather than held button
    /// states - handled straight off the OpenCV key queue (see the main
    /// loop), separately from the InputSource-driven gameplay buttons.
    void HandleDebugKey(int key)
    {
        switch (key)
        {
        case 'n': case 'N': nameTableWindow.toggle(); break;
        case 'p': case 'P': patternTableWindow.toggle(); break;
        case 'o': case 'O': patternTablePalette = (patternTablePalette + 1) % 4; break;
        case 'c': case 'C': cpuSpeedWindow.toggle(); break;
        case 'v': case 'V': memoryWindow.toggle(); break;
        case 'u': case 'U': oamWindow.toggle(); break;
        case '[': ScrollMemoryView(-1); break;
        case ']': ScrollMemoryView(1); break;
        case '{': ScrollMemoryView(-16); break;
        case '}': ScrollMemoryView(16); break;
        case 'm': case 'M':
            if (!remapState.Active())
                for (auto& src : inputSources)
                    if (src->Available() && src->Name() == "Keyboard")
                        remapState.Begin(src.get());
            break;
        case 'g': case 'G':
            if (!remapState.Active())
                for (auto& src : inputSources)
                    if (src->Available() && src->SupportsRemap() && src->Name() != "Keyboard")
                        remapState.Begin(src.get());
            break;
        case 'l': case 'L':
            if (!remapState.Active() && !romSelector.Active())
                romSelector.Open();
            break;
        // NEW, no C# equivalent - user-requested speed control (see
        // NES_CPU::speedMultiplier's own comment for the full story: PC
        // hardware is far faster than a real 6502, but this port's own
        // per-scanline rendering work is real computation, not an
        // artificial delay, so "just remove the throttle" only goes so
        // far - this exposes NES_CPU::mod's target speed as something the
        // user can directly scale instead). '+'/'-' double/halve it,
        // clamped in NES_Console::setSpeedMultiplier(); '0' resets to
        // real-hardware NTSC speed (1.0x).
        case '+': case '=':
            NES::NES_Console::setSpeedMultiplier(NES::NES_Console::getSpeedMultiplier() * 2.0);
            break;
        case '-': case '_':
            NES::NES_Console::setSpeedMultiplier(NES::NES_Console::getSpeedMultiplier() / 2.0);
            break;
        case '0':
            NES::NES_Console::setSpeedMultiplier(1.0);
            break;
        // NEW, no C# equivalent - user-requested save/load-state feature
        // (see NES_SaveState's own comment). Digits '1'-'9' just pick
        // *which* slot Q/E act on next - matching common emulator UX
        // (e.g. RetroArch's own numbered-slot-select convention) - '0'
        // stays speed-reset (see above) rather than a 10th slot, to avoid
        // any ambiguity between the two features. The actual Q/E save/load
        // actions are handled in main()'s own key loop, not here - see its
        // own comment on why (needs to stop the CPU thread first).
        case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            saveStateSlot = key - '0';
            std::cout << "Save-state slot: " << saveStateSlot << std::endl;
            break;
        default: break;
        }
    }
}

namespace
{
    /// Every default relative path in this port (NES_PPU_Palette's
    /// "./Palletes/2C03and2C05.bmp", this file's "./Galaga.nes") is relative
    /// to the current working directory - exactly like the C# original's own
    /// relative paths were, which only ever resolved because Windows starts
    /// a double-clicked .exe with its own folder as the working directory.
    /// A terminal or file-manager "run" launch doesn't give that guarantee
    /// on Linux, so this chdir's into the executable's own directory first,
    /// restoring the behavior the original relied on regardless of how/from
    /// where nes-emu is actually launched. New UI-shell code, not a port.
    void ChdirToExecutableDir(const char* argv0)
    {
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            return; // not on Linux/procfs unavailable - leave cwd as-is
        buf[len] = '\0';
        std::string exePath(buf);
        auto slash = exePath.find_last_of('/');
        if (slash == std::string::npos)
            return;
        std::string exeDir = exePath.substr(0, slash);
        if (chdir(exeDir.c_str()) != 0)
        {
            std::cerr << "Warning: could not chdir to " << exeDir
                       << " - relative paths (ROM, palette bitmap) may fail." << std::endl;
        }
        (void)argv0;
    }
}

int main(int argc, char** argv)
{
    // FIXED (real performance regression, found live via a direct report:
    // "CPU only at 16%, but everything feels slow" on a 32-core machine):
    // OpenCV defaults to spreading its own parallel_for_-based operations
    // (cv::resize()/cv::imshow(), called every single UI-thread loop
    // iteration - see the main loop below) across a worker-thread pool
    // sized to std::thread::hardware_concurrency() (here, 32) the moment
    // the first such call runs. Confirmed via `ps -T`: this process was
    // spawning ~30 extra OpenCV worker threads (plus ~8 Qt "Thread
    // (pooled)" threads from its Qt-backed highgui), none of which do any
    // actual NES emulation - all of it is CPU thread's single-threaded
    // NES_CPU::Run() loop. For genuinely large images this parallelism
    // pays for itself; for this project's tiny 256x240 bitmaps, scaled up
    // once per UI frame, the *synchronization* cost of waking/coordinating
    // dozens of worker threads 60 times/sec is pure overhead - real work
    // per call is far too small to amortize it. That overhead spread thin
    // across many cores is exactly what shows up as "low aggregate CPU%,
    // but still feels slow": no single core is saturated with useful work,
    // but scheduling/wake latency from the thread pool still delays every
    // frame. Fixed by disabling OpenCV's internal threading entirely -
    // this program's own explicit threading (the CPU thread vs UI thread
    // split - see NES_Console::RenderFrame()'s comment) is the only
    // parallelism it actually needs.
    cv::setNumThreads(0);

    ChdirToExecutableDir(argv[0]);

    const std::string romPath = argc > 1 ? argv[1] : "./Galaga.nes";

    try
    {
        NES::NES_Console::INIT();
        NES::NES_Console::LoadRom(romPath);
        currentRomPath = romPath;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Startup failed (ROM \"" << romPath << "\"): " << e.what() << std::endl;
        return 1;
    }

    // TEMPORARY diagnostic aid - opt-in via NES_TRACE_WRITE (a comma-
    // separated list of hex zero-page addresses), off by default. Hooks
    // each address's AfterSet directly, logging the new value and the
    // *current* NES_Register::PC each time - a more direct way to find
    // which code actually writes somewhere than guessing candidate call
    // sites from static disassembly (which kept missing - breakpoints at
    // several statically-identified candidate addresses never fired against
    // either a synthetic-input or a clean attract-mode-demo repro of the
    // same bug). NOTE: PC at hook-fire time is wherever execution has
    // reached by the time this AfterSet callback runs (typically already
    // past the STA that caused it) - close enough to identify the
    // responsible code region, not necessarily the STA's own exact address.
    if (const char* traceWriteEnv = std::getenv("NES_TRACE_WRITE"))
    {
        std::string spec(traceWriteEnv);
        size_t pos = 0;
        while (pos < spec.size())
        {
            size_t comma = spec.find(',', pos);
            std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            unsigned addr = std::stoul(tok, nullptr, 16);
            NES::NES_Memory::Memory[addr]->AfterSet([addr](uint8_t v) {
                std::cerr << "[WRITE] $" << std::hex << addr << " = 0x" << static_cast<int>(v)
                          << " (near PC=0x" << NES::NES_Register::PC << ")" << std::dec
                          << " scanline=" << NES::NES_PPU::CurrentScanline() << std::endl;
            });
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }

    // TEMPORARY diagnostic aid, not a port of anything - opt-in via
    // NES_AUTO_LOAD_SAVE (a save-state file path), off by default. Same
    // spirit as the NES_AUTO_*/NES_DUMP_* env vars below: lets a bug be
    // investigated headlessly starting from a save the user captured live
    // (Q key) at/around the exact moment a visual bug appeared, instead of
    // needing to replay precise input timing under Xvfb. Loaded before the
    // CPU thread starts, so it starts on Resume() (not Run()) - same
    // reasoning as the Q/E key handler further down: Run() would
    // immediately overwrite the just-loaded state with a full power-on
    // reset.
    // TEMPORARY diagnostic aid - prints mirroring/scroll/physical-bank fill
    // state, reused both right after NES_AUTO_LOAD_SAVE and (if
    // NES_TRACE_PPU_STATE is set) on the auto-quit tick, to see whether a
    // save taken mid-transition catches up on its own after more frames run.
    auto printPpuState = [](const char* label) {
        const char* mirrorNames[] = { "vertical", "horisontal", "four_screen", "single_screen_a", "single_screen_b" };
        std::cout << label
                  << " mirroring=" << mirrorNames[static_cast<int>(NES::INES::arrangement)]
                  << " PPUCTRL.N=" << static_cast<int>(NES::NES_PPU_Register::PPUCTRL.N())
                  << " xScroll=" << NES::NES_PPU::xScroll
                  << " yScroll=" << NES::NES_PPU::yScroll
                  << " ScrollXoY=" << NES::NES_PPU::ScrollXoY
                  << std::endl;
        for (size_t bank = 0; bank < 4; bank++)
        {
            auto& nt = NES::NES_PPU_Memory::NameTablePhysicalBanks()[bank];
            auto& at = NES::NES_PPU_Memory::AttributeTablePhysicalBanks()[bank];
            int ntNonZero = 0, atNonZero = 0;
            for (auto& a : nt) if (a->value() != 0) ntNonZero++;
            for (auto& a : at) if (a->value() != 0) atNonZero++;
            std::cout << "  physicalBank[" << bank << "] nameTableNonZero=" << ntNonZero << "/" << nt.size()
                      << " attrNonZero=" << atNonZero << "/" << at.size() << std::endl;
        }
    };

    bool autoLoadedSave = false;
    if (const char* autoLoadSaveEnv = std::getenv("NES_AUTO_LOAD_SAVE"))
    {
        autoLoadedSave = NES::NES_SaveState::Load(autoLoadSaveEnv);
        std::cout << (autoLoadedSave ? "Auto-loaded save state: " : "Failed to auto-load save state: ")
                  << autoLoadSaveEnv << std::endl;
        if (autoLoadedSave)
        {
            printPpuState("[load]");
            // TEMPORARY diagnostic aid - opt-in via NES_SIMULATE_SCROLL_WRAP,
            // off by default. Directly issues the exact $2005-then-$2000
            // write sequence found live in Chip 'n Dale's own NMI handler
            // (disassembled at the exact save-state boundary this bug was
            // root-caused from), through the real public write path, to
            // visually confirm NES_PPU::RecomputeXScroll()'s fix without
            // needing to get past whatever is blocking headless input from
            // reaching this game's own main loop.
            if (std::getenv("NES_SIMULATE_SCROLL_WRAP"))
            {
                NES::NES_PPU::ScrollXoY = true;
                NES::NES_PPU_Register::PPUSCROLL->Value(0x00);
                NES::NES_PPU_Register::PPUSCROLL->Value(0x00);
                NES::NES_PPU_Register::PPUCTRL.adress->Value(0x91);
                printPpuState("[after simulated wrap]");
            }
            // TEMPORARY diagnostic aid - dumps a raw CPU-address-space byte
            // range to a file for offline disassembly, format
            // "startHex:endHex:path" (e.g. "8000:8400:/tmp/dump.bin"). Reads
            // via getMemoryByte() (live, correctly bank-translated, no side
            // effects), so this always reflects whichever PRG bank is
            // currently switched in - unlike reading the raw .nes file,
            // which would need the current MMC1 bank state applied by hand.
            if (const char* rangeEnv = std::getenv("NES_DUMP_MEM_RANGE"))
            {
                std::string spec(rangeEnv);
                size_t c1 = spec.find(':');
                size_t c2 = spec.find(':', c1 + 1);
                if (c1 != std::string::npos && c2 != std::string::npos)
                {
                    unsigned startAddr = std::stoul(spec.substr(0, c1), nullptr, 16);
                    unsigned endAddr = std::stoul(spec.substr(c1 + 1, c2 - c1 - 1), nullptr, 16);
                    std::string outPath = spec.substr(c2 + 1);
                    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                    for (unsigned addr = startAddr; addr <= endAddr; addr++)
                    {
                        uint8_t b = NES::NES_Console::getMemoryByte(static_cast<uint16_t>(addr));
                        out.write(reinterpret_cast<const char*>(&b), 1);
                    }
                    std::cout << "  dumped 0x" << std::hex << startAddr << "-0x" << endAddr << std::dec
                              << " to " << outPath << std::endl;
                }
            }
            // TEMPORARY diagnostic aid - forces a raw byte into CPU address
            // space right after a save-state load, format "addrHex:valueHex"
            // (e.g. "9a:00"). Uses value() (unhooked), matching this file's
            // existing memory-viewer read path's own reasoning for using the
            // unhooked getter - a raw poke shouldn't trigger a register side
            // effect unless the target genuinely is a hooked register.
            if (const char* pokeEnv = std::getenv("NES_POKE"))
            {
                std::string spec(pokeEnv);
                size_t c1 = spec.find(':');
                if (c1 != std::string::npos)
                {
                    unsigned addr = std::stoul(spec.substr(0, c1), nullptr, 16);
                    unsigned val = std::stoul(spec.substr(c1 + 1), nullptr, 16);
                    NES::NES_Memory::Memory[addr]->value(static_cast<uint8_t>(val));
                    std::cout << "  poked 0x" << std::hex << addr << " = 0x" << val << std::dec << std::endl;
                }
            }
        }
    }
    std::thread cpuThread([autoLoadedSave]() {
        if (autoLoadedSave)
            NES::NES_Console::Resume();
        else
            NES::NES_Console::Run();
    });

    // TEMPORARY diagnostic aid - opt-in via NES_SPEED_MULTIPLIER, off by
    // default. Sets the initial speed multiplier (same clamp/mechanism as
    // the interactive +/-/0 keys - see setSpeedMultiplier()'s own comment),
    // so a headless test that needs to sit through a long real-time wait
    // (e.g. a title-screen attract-mode demo) doesn't also have to wait at
    // real NTSC speed to get there.
    if (const char* speedEnv = std::getenv("NES_SPEED_MULTIPLIER"))
        NES::NES_Console::setSpeedMultiplier(std::atof(speedEnv));

    // Stops the running CPU thread, loads a different ROM, and starts a
    // fresh thread on NES_Console::Restart() (not Run() - Restart() is the
    // one that re-homes PC at the reset vector and re-inits the PPU/APU
    // registers, matching what a real console's RESET line does). New code,
    // backing RomSelector above - the C# original never supported switching
    // ROMs without relaunching the whole program.
    auto switchRom = [&cpuThread](const std::string& path)
    {
        NES::NES_Console::Stop();
        if (cpuThread.joinable())
            cpuThread.join();
        try
        {
            NES::NES_Console::LoadRom(path);
            currentRomPath = path;
            std::cout << "Loaded " << path << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to load " << path << ": " << e.what() << std::endl;
        }
        cpuThread = std::thread([]() { NES::NES_Console::Restart(); });
    };

    const std::string windowName = "NES";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    NES::NES_Audio::Start(); // non-fatal if it fails - see NES_Audio.h

    inputSources.push_back(std::make_unique<NES::KeyboardInputSource>());
    inputSources.push_back(std::make_unique<NES::GamepadInputSource>());

    std::cout << "Controls: WASD or Arrow keys = D-Pad, K = A, J = B, Enter = START, Space = SELECT, Esc = quit"
              << std::endl;
    std::cout << "Press M to remap the keyboard";
    for (auto& src : inputSources)
        if (src->Available() && src->Name() != "Keyboard")
            std::cout << ", G to remap the " << src->Name();
    std::cout << "." << std::endl;
    std::cout << "Debug windows: N = Name Table, P = Pattern Table (O cycles its palette), "
                 "C = CPU Speed, V = Memory Viewer ([/] to scroll a page, {/} to jump 4K), "
                 "U = OAM Viewer (all 64 sprites at their real OAM position, including ones "
                 "parked below the red line at the bottom of the screen)"
              << std::endl;
    std::cout << "Press L to pick a different ROM (looked for next to the executable and in ./roms)."
              << std::endl;
    std::cout << "Speed: + doubles, - halves, 0 resets to real NTSC (1x)." << std::endl;
    std::cout << "Save states: 1-9 picks a slot (default 1), Q saves, E loads." << std::endl;
    std::cout << "Press Y to start/stop recording input (saved as <rom>.inputs.txt)." << std::endl;

    // TEMPORARY diagnostic aid, not a port of anything - opt-in via env
    // vars, both unset (no-op) by default. Mirrors the FCEUX Lua script
    // approach used to investigate the "STAGE 1 never progresses" bug:
    // lets this be driven headlessly (no real keyboard/window focus needed)
    // for automated comparison against a reference emulator.
    const char* autoStartEnv = std::getenv("NES_AUTO_START_FRAME");
    const long autoStartFrame = autoStartEnv ? std::atol(autoStartEnv) : -1;
    // NEW, same spirit as autoStartFrame - a menu that needs Start pressed
    // more than once (e.g. a "press start" splash, then a player-count
    // menu) can't be reached by a single 30-frame press. NES_AUTO_START_COUNT
    // repeats the press every NES_AUTO_START_INTERVAL frames (default 90,
    // i.e. ~1.5 real seconds at ~60Hz) starting at autoStartFrame.
    const char* autoStartCountEnv = std::getenv("NES_AUTO_START_COUNT");
    const long autoStartCount = autoStartCountEnv ? std::atol(autoStartCountEnv) : 1;
    const char* autoStartIntervalEnv = std::getenv("NES_AUTO_START_INTERVAL");
    const long autoStartInterval = autoStartIntervalEnv ? std::atol(autoStartIntervalEnv) : 90;
    // NEW - holds RIGHT from this frame onward (until quit), for reaching
    // an in-level scrolling glitch headlessly instead of needing a real
    // keyboard/gamepad in front of the window.
    const char* autoRightEnv = std::getenv("NES_AUTO_RIGHT_FRAME");
    const long autoRightFrame = autoRightEnv ? std::atol(autoRightEnv) : -1;
    // Same spirit as NES_AUTO_RIGHT_FRAME - holds UP from this frame onward,
    // for reaching a vertical-scrolling glitch (e.g. climbing a pole)
    // headlessly.
    const char* autoUpEnv = std::getenv("NES_AUTO_UP_FRAME");
    const long autoUpFrame = autoUpEnv ? std::atol(autoUpEnv) : -1;
    // Same spirit as NES_AUTO_UP_FRAME - holds DOWN instead, for reaching a
    // vertical-scrolling glitch that only shows up scrolling the other way.
    const char* autoDownEnv = std::getenv("NES_AUTO_DOWN_FRAME");
    const long autoDownFrame = autoDownEnv ? std::atol(autoDownEnv) : -1;
    // Same spirit as NES_AUTO_RIGHT_FRAME/NES_AUTO_UP_FRAME - holds SELECT
    // from this frame onward, for reaching an interaction that specifically
    // needs SELECT (not START) headlessly.
    const char* autoSelectEnv = std::getenv("NES_AUTO_SELECT_FRAME");
    const long autoSelectFrame = autoSelectEnv ? std::atol(autoSelectEnv) : -1;
    // Same spirit as NES_AUTO_START_COUNT/INTERVAL - presses A (jump) for 30
    // frames, repeating every NES_AUTO_JUMP_INTERVAL frames (default 60, ~1
    // real second), NES_AUTO_JUMP_COUNT times starting at NES_AUTO_JUMP_FRAME.
    // Added specifically to reproduce "climb a pipe, jump 1-2x, background
    // goes black" headlessly from a save state taken just before the bug.
    const char* autoJumpEnv = std::getenv("NES_AUTO_JUMP_FRAME");
    const long autoJumpFrame = autoJumpEnv ? std::atol(autoJumpEnv) : -1;
    const char* autoJumpCountEnv = std::getenv("NES_AUTO_JUMP_COUNT");
    const long autoJumpCount = autoJumpCountEnv ? std::atol(autoJumpCountEnv) : 1;
    const char* autoJumpIntervalEnv = std::getenv("NES_AUTO_JUMP_INTERVAL");
    const long autoJumpInterval = autoJumpIntervalEnv ? std::atol(autoJumpIntervalEnv) : 60;
    const char* autoQuitEnv = std::getenv("NES_AUTO_QUIT_FRAME");
    const long autoQuitFrame = autoQuitEnv ? std::atol(autoQuitEnv) : -1;
    // Same spirit as NES_AUTO_START_FRAME/NES_DUMP_FRAME above - opt-in,
    // off by default. Opens the Name Table debug window headlessly, e.g.
    // under Xvfb+TSan, for regression-testing the cross-thread race
    // NES_Console::getNameTabeleDebugOverlay()'s own comment describes
    // (real bug: a UI-thread NameTabele() call raced the CPU thread's live
    // NES_PPU_Memory writes) - this is how it was originally found.
    if (std::getenv("NES_AUTO_OPEN_NAMETABLE"))
        nameTableWindow.toggle();

    // Same spirit as NES_AUTO_OPEN_NAMETABLE above - opens the OAM Viewer
    // debug window headlessly so NES_DUMP_OAM (below) gets real content
    // instead of a black placeholder (RenderFrame() only bothers rebuilding
    // it while oamWindow.visible is true - see NES_Console::
    // setOAMDebugWindowVisible()'s own comment).
    if (std::getenv("NES_AUTO_OPEN_OAM"))
        oamWindow.toggle();

    // Same spirit as NES_AUTO_LOAD_SAVE above - opt-in via NES_PLAYBACK_INPUT
    // (a recording file path, see StartRecording()'s own comment on the
    // format). Loaded once, here; applied every UI-thread loop iteration
    // below via SetButton() calls in the same unconditional-of-focus block
    // the other NES_AUTO_* overrides already use.
    std::map<long, std::vector<std::pair<std::string, bool>>> playbackEvents;
    std::set<std::string> playbackHeld;
    if (const char* playbackEnv = std::getenv("NES_PLAYBACK_INPUT"))
        playbackEvents = LoadPlaybackFile(playbackEnv);

    long uiFrame = 0;

    bool running = true;
    while (running)
    {
        ++uiFrame;
        {
            auto it = playbackEvents.find(uiFrame);
            if (it != playbackEvents.end())
                for (const auto& [button, down] : it->second)
                {
                    if (down)
                        playbackHeld.insert(button);
                    else
                        playbackHeld.erase(button);
                }
        }
        if (std::getenv("NES_TRACE_PC") && uiFrame % 30 == 0)
            std::cout << "  frame " << uiFrame << " PC=0x" << std::hex << NES::NES_Register::PC << std::dec
                      << " xScroll=" << NES::NES_PPU::xScroll << " yScroll=" << NES::NES_PPU::yScroll << std::endl;
        // TEMPORARY diagnostic aid - opt-in via NES_TRACE_FPS, off by
        // default. Prints the real measured FPS/speed-multiplier so the
        // CPU pacing redesign (NES_CPU::Run()) can be verified headlessly
        // against a real wall-clock duration instead of eyeballing the
        // CPU-speed debug window.
        if (std::getenv("NES_TRACE_FPS") && uiFrame % 60 == 0)
            std::cout << "  [fps] frame=" << uiFrame << " measuredFPS=" << NES::NES_Console::getMeasuredFPS()
                      << " target=" << (60.0988 * NES::NES_Console::getSpeedMultiplier())
                      << " multiplier=" << NES::NES_Console::getSpeedMultiplier() << std::endl;
        bool autoStartActive = false;
        for (long i = 0; autoStartFrame >= 0 && i < autoStartCount; ++i)
        {
            long pressAt = autoStartFrame + i * autoStartInterval;
            if (uiFrame >= pressAt && uiFrame < pressAt + 30)
            {
                autoStartActive = true;
                break;
            }
        }
        bool autoRightActive = autoRightFrame >= 0 && uiFrame >= autoRightFrame;
        bool autoUpActive = autoUpFrame >= 0 && uiFrame >= autoUpFrame;
        bool autoDownActive = autoDownFrame >= 0 && uiFrame >= autoDownFrame;
        bool autoSelectActive = autoSelectFrame >= 0 && uiFrame >= autoSelectFrame;
        bool autoJumpActive = false;
        for (long i = 0; autoJumpFrame >= 0 && i < autoJumpCount; ++i)
        {
            long pressAt = autoJumpFrame + i * autoJumpInterval;
            if (uiFrame >= pressAt && uiFrame < pressAt + 30)
            {
                autoJumpActive = true;
                break;
            }
        }
        if (autoQuitFrame >= 0 && uiFrame >= autoQuitFrame)
            running = false;

        // TEMPORARY diagnostic aid - pokes zero-page $9A to 0 every single
        // UI frame (not just once), to test whether the game's own logic is
        // re-setting it back to nonzero every frame (in which case a
        // one-shot poke would look like it "did nothing"). Not
        // thread-safe (writes NES_Memory from the UI thread while the CPU
        // thread runs) - throwaway diagnostic only, never enabled by
        // default.
        if (std::getenv("NES_FORCE_9A_ZERO"))
            NES::NES_Memory::Memory[0x9A]->value(0);

        NES_PPU::Picture frame = NES::NES_Console::getDisplay();
        cv::Mat img = frame.Image();

        // TEMPORARY diagnostic aid, not a port of anything - opt-in via
        // NES_DUMP_FRAME (a file path), off by default. Dumps exactly one
        // PNG of the current frame on the auto-quit tick, for headless
        // black-screen-bug verification (see NES_AUTO_START_FRAME/
        // NES_AUTO_QUIT_FRAME above).
        if (autoQuitFrame >= 0 && uiFrame == autoQuitFrame)
        {
            if (std::getenv("NES_TRACE_PPU_STATE"))
                printPpuState("[quit]");

            if (std::getenv("NES_TRACE_NMI_COUNTER"))
                std::cout << "  [quit] $92 (NMI vblank counter) = 0x" << std::hex
                          << static_cast<int>(NES::NES_Console::getMemoryByte(0x92)) << std::dec << std::endl;

            // TEMPORARY diagnostic aid - dumps a raw CPU-address-space byte
            // range at quit time (same "startHex:endHex:path" format as the
            // NES_AUTO_LOAD_SAVE-only NES_DUMP_MEM_RANGE above), so a
            // clean-boot/no-save-state run (e.g. the attract-mode demo) can
            // still be inspected - that other one only fires right after a
            // save-state load.
            if (const char* rangeEnv = std::getenv("NES_DUMP_MEM_RANGE_AT_QUIT"))
            {
                std::string spec(rangeEnv);
                size_t c1 = spec.find(':');
                size_t c2 = spec.find(':', c1 + 1);
                if (c1 != std::string::npos && c2 != std::string::npos)
                {
                    unsigned startAddr = std::stoul(spec.substr(0, c1), nullptr, 16);
                    unsigned endAddr = std::stoul(spec.substr(c1 + 1, c2 - c1 - 1), nullptr, 16);
                    std::string outPath = spec.substr(c2 + 1);
                    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                    for (unsigned addr = startAddr; addr <= endAddr; addr++)
                    {
                        uint8_t b = NES::NES_Console::getMemoryByte(static_cast<uint16_t>(addr));
                        out.write(reinterpret_cast<const char*>(&b), 1);
                    }
                    std::cout << "  dumped 0x" << std::hex << startAddr << "-0x" << endAddr << std::dec
                              << " to " << outPath << std::endl;
                }
            }

            // TEMPORARY diagnostic aid - dumps all 4 physical nametable+
            // attribute banks (NES_PPU_Memory::NameTablePhysicalBanks()/
            // AttributeTablePhysicalBanks() - real PPU address space, not
            // the CPU-address-space $2000-$2FFF mirror of PPU *registers*
            // NES_DUMP_MEM_RANGE_AT_QUIT would read there instead) to a
            // single file, 1024 bytes per bank (960 nametable + 64
            // attribute), so bank content can be compared directly instead
            // of inferred from a screenshot.
            if (const char* ntPath = std::getenv("NES_DUMP_PHYSICAL_BANKS"))
            {
                std::ofstream out(ntPath, std::ios::binary | std::ios::trunc);
                for (auto& bank : NES::NES_PPU_Memory::NameTablePhysicalBanks())
                    for (auto& cell : bank)
                    {
                        uint8_t b = cell->value();
                        out.write(reinterpret_cast<const char*>(&b), 1);
                    }
                for (auto& bank : NES::NES_PPU_Memory::AttributeTablePhysicalBanks())
                    for (auto& cell : bank)
                    {
                        uint8_t b = cell->value();
                        out.write(reinterpret_cast<const char*>(&b), 1);
                    }
                std::cout << "  dumped 4 physical nametable+attribute banks to " << ntPath << std::endl;
            }

            // TEMPORARY diagnostic aid - opt-in via NES_TRACE_OAM, off by
            // default. Prints every OAM sprite currently on-screen (Y<240),
            // to correlate a visually-glitched sprite's screen position
            // with its raw tile index/bank/palette - for spotting whether
            // one specific sprite is reading garbage OAM/CHR state without
            // needing to guess from a screenshot alone.
            if (std::getenv("NES_TRACE_OAM"))
            {
                for (size_t i = 0; i < NES::NES_PPU_OAM::SpriteTile.size(); i++)
                {
                    int y = NES::NES_PPU_OAM::SpriteYc[i]->Value();
                    if (y >= 240) continue;
                    int x = NES::NES_PPU_OAM::SpriteXc[i]->Value();
                    auto& tile = NES::NES_PPU_OAM::SpriteTile[i];
                    auto& attr = NES::NES_PPU_OAM::SpriteAttribute[i];
                    std::cout << "  [oam] #" << i << " X=" << x << " Y=" << y
                              << " tileNum=" << static_cast<int>(tile.Number())
                              << " bank=" << tile.Bank()
                              << " pal=" << static_cast<int>(attr.Palette())
                              << " flipH=" << attr.FlipH() << " flipV=" << attr.FlipV() << std::endl;
                }
            }

            // TEMPORARY diagnostic aid - opt-in via NES_TEST_WRITE_ISOLATION,
            // off by default. Directly tests whether writes through two
            // *different* logical CPU addresses ($2000 vs $2400) actually
            // land in two genuinely separate physical AddressSetup cells,
            // or whether something still aliases them to the same one
            // regardless of the bankForSlot fix - found live via real
            // gameplay still showing identical ("AA/AA") content in all 4
            // Name Table debug quadrants after that fix.
            if (std::getenv("NES_TEST_WRITE_ISOLATION"))
            {
                NES::NES_Memory::Memory[0x2002]->Value(); // reset $2006 write toggle
                NES::NES_Memory::Memory[0x2006]->Value(0x20);
                NES::NES_Memory::Memory[0x2006]->Value(0x00);
                NES::NES_Memory::Memory[0x2007]->Value(0xAA); // write to $2000 (slot 0)

                NES::NES_Memory::Memory[0x2006]->Value(0x24);
                NES::NES_Memory::Memory[0x2006]->Value(0x00);
                NES::NES_Memory::Memory[0x2007]->Value(0xBB); // write to $2400 (slot 1)

                uint8_t slot0 = NES::NES_PPU_Memory::NameTableN[0][0]->value();
                uint8_t slot1 = NES::NES_PPU_Memory::NameTableN[1][0]->value();
                uint8_t slot2 = NES::NES_PPU_Memory::NameTableN[2][0]->value();
                uint8_t slot3 = NES::NES_PPU_Memory::NameTableN[3][0]->value();
                std::cout << "  write isolation test: wrote 0xAA to $2000, 0xBB to $2400" << std::endl;
                std::cout << "    slot0($2000)=0x" << std::hex << (int)slot0 << " slot1($2400)=0x" << (int)slot1
                          << " slot2($2800)=0x" << (int)slot2 << " slot3($2C00)=0x" << (int)slot3 << std::dec
                          << std::endl;
            }

            // TEMPORARY diagnostic aid - opt-in via NES_TEST_OVERLAY_QUADRANTS,
            // off by default. Reads raw pixel bytes back out of the Name
            // Table debug overlay bitmap directly (not a screenshot) and
            // sums each quadrant into a checksum, so quadrant equality can
            // be verified numerically instead of by eye.
            if (std::getenv("NES_TEST_OVERLAY_QUADRANTS"))
            {
                NES_PPU::Picture overlay = NES::NES_Console::getNameTabeleDebugOverlay();
                int w = overlay.Width(), h = overlay.Height();
                long sum[4] = {0, 0, 0, 0};
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                    {
                        int q = (x < w / 2 ? 0 : 1) + (y < h / 2 ? 0 : 2);
                        NES_PPU::Color c = overlay.GetPixel(x, y);
                        sum[q] += c.R + c.G * 257 + c.B * 65537;
                    }
                std::cout << "  overlay quadrant checksums: TL=" << sum[0] << " TR=" << sum[1]
                          << " BL=" << sum[2] << " BR=" << sum[3] << std::endl;
                std::cout << "    TL==TR:" << (sum[0] == sum[1]) << " TL==BL:" << (sum[0] == sum[2])
                          << " TR==BR:" << (sum[1] == sum[3]) << " BL==BR:" << (sum[2] == sum[3]) << std::endl;
            }

            const char* dumpPath = std::getenv("NES_DUMP_FRAME");
            if (dumpPath)
                cv::imwrite(dumpPath, img);

            // Same spirit as NES_DUMP_FRAME above (kept permanently for the
            // same reason) - dumps the pattern-table debug view (both 4KB
            // CHR banks) so a missing/wrong tile can be checked directly
            // against the raw CHR data, independent of which nametable
            // entry currently references it.
            const char* dumpPtPath = std::getenv("NES_DUMP_PATTERNTABLE");
            if (dumpPtPath)
            {
                NES_PPU::Picture patterns = NES::NES_Console::getPatternTable(0);
                cv::imwrite(dumpPtPath, patterns.Image());
            }

            // Same spirit as NES_DUMP_FRAME/NES_DUMP_PATTERNTABLE above -
            // dumps the Name Table debug overlay so the user-reported "Name
            // Table window is black" regression can be checked headlessly.
            const char* dumpNtPath = std::getenv("NES_DUMP_NAMETABLE");
            if (dumpNtPath)
            {
                NES_PPU::Picture nameTable = NES::NES_Console::getNameTabeleDebugOverlay();
                cv::imwrite(dumpNtPath, nameTable.Image());
            }

            // Same spirit as NES_DUMP_NAMETABLE above - dumps the OAM Viewer
            // debug overlay (see NES_PPU::OAMDebugOverlay()'s own comment).
            // Needs NES_AUTO_OPEN_OAM set too, or this is just a black image.
            const char* dumpOamPath = std::getenv("NES_DUMP_OAM");
            if (dumpOamPath)
            {
                NES_PPU::Picture oam = NES::NES_Console::getOAMDebugOverlay();
                cv::imwrite(dumpOamPath, oam.Image());
            }

            // TEMPORARY diagnostic aid - opt-in via NES_DUMP_HUD_ROWS, off
            // by default. Prints the raw tile IDs of the bottom 2 rows
            // (rows 28-29, where Tiny Toon Adventures' status bar should
            // live) of all 4 physical nametables directly from
            // NES_PPU_Memory, bypassing the (separately, possibly still
            // broken) debug overlay - to see whether the game ever writes
            // real HUD tile data there at all.
            if (std::getenv("NES_DUMP_HUD_ROWS"))
            {
                std::cerr << "[hudrows] $38=0x" << std::hex
                          << static_cast<int>(NES::NES_Memory::Memory[0x38]->value()) << std::dec << std::endl;
                for (int nt = 0; nt < 4; nt++)
                {
                    std::cerr << "[hudrows] nametable " << nt << ":" << std::endl;
                    for (int row = 0; row <= 29; row++)
                    {
                        std::cerr << "  row " << row << ": ";
                        for (int col = 0; col < 32; col++)
                        {
                            int k = row * 32 + col;
                            std::cerr << std::hex
                                      << static_cast<int>(
                                             NES::NES_PPU_Memory::NameTableN[static_cast<size_t>(nt)]
                                                                             [static_cast<size_t>(k)]
                                                                                 ->Value())
                                      << std::dec << " ";
                        }
                        std::cerr << std::endl;
                    }
                }
            }
        }

        cv::Mat scaled;
        cv::resize(img, scaled, cv::Size(img.cols * kScale, img.rows * kScale), 0, 0, cv::INTER_NEAREST);

        PollInputSources();

        if (remapState.Active())
        {
            remapState.Tick();
            if (remapState.Active()) // still going (Tick() clears source when done)
                remapState.DrawOverlay(scaled);
        }
        else if (romSelector.Active())
        {
            romSelector.Tick();
            if (romSelector.Active())
                romSelector.DrawOverlay(scaled);
            else if (!romSelector.pendingLoad.empty())
                switchRom(romSelector.pendingLoad);
        }
        cv::imshow(windowName, scaled);

        UpdateDebugWindows();

        // Drains the whole pending key-event queue each tick (not just one
        // key) so debug-window toggles / remap-menu triggers / Esc never
        // wait an extra ~16ms tick behind each other. Gameplay buttons no
        // longer go through this queue at all - see InputSource.h and
        // ApplyInputSourcesToPlayer1() above for why direct state polling
        // replaced it for those.
        // NEW - see WindowHasFocus()'s own comment for the bug this closes:
        // every key/button action below is gated on this window actually
        // being the focused one, so typing into some other application no
        // longer reaches this emulator at all (gameplay buttons *or* the
        // N/P/O/C/V/L/M/Esc debug hotkeys).
        bool focused = WindowHasFocus();

        int rawKey = cv::waitKeyEx(16);
        bool sawEsc = false;
        int keysProcessedThisTick = 0;
        constexpr int kMaxKeysPerTick = 16; // safety cap, not a real limit
        while (rawKey >= 0 && keysProcessedThisTick < kMaxKeysPerTick)
        {
            // FIXED - was a preserved bug in this file's own debug/save-state
            // key dispatch (not the emulator core, and not the real D-pad
            // input path - see KeyboardInputSource.cpp's own evdev-based
            // reading, entirely separate from this cv::waitKeyEx() call):
            // arrow keys and other non-ASCII/special keys come back from
            // OpenCV as extended codes outside the ASCII range (e.g. the
            // GTK/Qt backend's Left arrow is the X11 keysym 0xFF51, not a
            // plain byte) - unconditionally masking with `& 0xFF` silently
            // truncated any such code down into ASCII range, so Left arrow
            // (0xFF51 & 0xFF = 0x51 = 'Q') was indistinguishable from a real
            // 'Q' keypress here and fired an unwanted save every time,
            // simultaneously with the real D-pad movement that same
            // keypress correctly triggered via the separate evdev path -
            // found live via real gameplay ("wenn ich den Pfeiltaste Left
            // drücke, macht es einen Save"). Fixed by only treating rawKey
            // as one of this loop's ASCII debug/save commands when it's
            // already in that range to begin with - true ASCII keypresses
            // (letters, digits, Esc) come back from OpenCV unmodified and
            // still work exactly as before; any larger/extended code (every
            // arrow key, F-keys, etc., regardless of which OpenCV backend
            // is in use) now simply falls through with no debug-key match,
            // instead of colliding with an unrelated ASCII command.
            int key = (rawKey >= 0 && rawKey < 256) ? rawKey : -1;
            if (focused)
            {
                if (key == 27 /* Esc */)
                {
                    if (remapState.Active())
                        remapState.Cancel();
                    else if (romSelector.Active())
                        romSelector.active = false;
                    else
                        sawEsc = true;
                }
                else if (!remapState.Active() && !romSelector.Active())
                {
                    HandleDebugKey(key);
                    // NEW, no C# equivalent - user-requested save/load-state
                    // feature (see NES_SaveState's own comment). Handled
                    // here rather than inside HandleDebugKey() specifically
                    // because NES_SaveState::Save()/Load() touch live
                    // NES_Memory/NES_PPU_Memory/... state directly, the
                    // same state the CPU thread (cpuThread, running
                    // NES_CPU::Run()) concurrently reads/writes every
                    // single instruction - exactly the class of
                    // cross-thread race NES_Console::getNameTabeleDebugOverlay()'s
                    // own comment already describes fixing elsewhere this
                    // session. Reuses switchRom()'s own established,
                    // already-correct pattern for safely touching that
                    // state from the UI thread: stop the CPU thread, join
                    // it (so no instruction can be mid-execution), do the
                    // save/load, then start a fresh thread again. A brief
                    // stutter is an acceptable, expected cost for a
                    // debugging/testing feature - nothing here runs on any
                    // real-time-sensitive path.
                    if (key == 'q' || key == 'Q' || key == 'e' || key == 'E')
                    {
                        NES::NES_Console::Stop();
                        if (cpuThread.joinable())
                            cpuThread.join();
                        std::string path = SaveStatePath(saveStateSlot);
                        bool ok = (key == 'q' || key == 'Q') ? NES::NES_SaveState::Save(path)
                                                              : NES::NES_SaveState::Load(path);
                        std::cout << ((key == 'q' || key == 'Q') ? "Save" : "Load") << " slot " << saveStateSlot
                                  << (ok ? ": OK (" : ": FAILED (") << path << ")" << std::endl;
                        cpuThread = std::thread([]() { NES::NES_Console::Resume(); });
                    }
                    else if (key == 'y' || key == 'Y')
                    {
                        if (recordingInput)
                            StopRecording(currentRomPath);
                        else
                            StartRecording(uiFrame);
                    }
                }
            }
            ++keysProcessedThisTick;
            rawKey = cv::pollKey();
        }
        if (sawEsc)
            running = false;

        if (!remapState.Active() && !romSelector.Active())
        {
            if (focused)
            {
                ApplyInputSourcesToPlayer1();
            }
            else
            {
                // Release everything rather than merely skip polling, so a
                // button already held down when focus is lost doesn't stay
                // "stuck" pressed in the emulator until focus comes back.
                for (const std::string& button : kButtonNames)
                    SetButton(NES::NES_GamePad::Player1, button, false);
            }
            // NEW - deliberately *not* gated on `focused` (unlike the real
            // keyboard/gamepad input above): these are explicit, opt-in
            // test-automation overrides (see the NOTE above autoStartFrame),
            // not physical input that could leak from another window - the
            // whole reason for the focus gate above - so they must still
            // work under Xvfb/headless test runs, which never have real
            // window-manager focus at all.
            if (autoStartActive)
                SetButton(NES::NES_GamePad::Player1, "START", true);
            // FIXED - was a preserved bug in this file's own headless test
            // harness (not the emulator core): NES_GamePad::Controller's
            // real button vocabulary (NES_GamePad.h) uses single-letter
            // direction names ("U"/"D"/"L"/"R" - matching real hardware's
            // read order), not "UP"/"RIGHT" - SetButton()'s exact string
            // match silently never matched either of these calls, so both
            // NES_AUTO_RIGHT_FRAME and NES_AUTO_UP_FRAME have been no-ops
            // this whole session: every "hold RIGHT/UP for N frames, does
            // the game respond" headless test that used them was actually
            // testing "does the game move with no directional input held" -
            // found live via a direct xScroll/yScroll trace showing zero
            // movement across multiple different save states and multi-
            // hundred-frame budgets, despite the CPU visibly executing
            // varied code (not stuck) in most of those runs.
            if (autoRightActive)
            {
                SetButton(NES::NES_GamePad::Player1, "R", true);
                if (std::getenv("NES_TRACE_AUTORIGHT") && uiFrame % 30 == 0)
                    std::cout << "  [autoright] frame=" << uiFrame
                              << " R=" << GetButton(NES::NES_GamePad::Player1, "R") << std::endl;
            }
            if (autoUpActive)
                SetButton(NES::NES_GamePad::Player1, "U", true);
            if (autoDownActive)
                SetButton(NES::NES_GamePad::Player1, "D", true);
            if (autoJumpActive)
                SetButton(NES::NES_GamePad::Player1, "A", true);
            if (autoSelectActive)
                SetButton(NES::NES_GamePad::Player1, "SELECT", true);
            for (const std::string& button : playbackHeld)
                SetButton(NES::NES_GamePad::Player1, button, true);
        }
        RecordInputTick(uiFrame);
        TraceInputSourcesIfRequested(uiFrame);

        if (cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) < 1)
        {
            running = false;
        }
    }

    NES::NES_Console::Stop();
    cpuThread.join();
    NES::NES_Audio::Stop();

    return 0;
}
