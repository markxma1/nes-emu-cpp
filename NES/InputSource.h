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
/// @brief A source of NES-button press states (keyboard, gamepad, ...).
/// Part of the UI shell, separate from the core CPU/PPU/Memory/Mapper
/// emulation logic. This
/// interface is the seam: NES/main.cpp's game loop only ever asks "is this
/// NES button currently down, across whatever input sources are active" -
/// it never needs to know whether the answer came from a keyboard, a
/// gamepad, or something else entirely. Adding a new input device means
/// writing one new class that implements this interface; it does not mean
/// touching the game loop, and never means touching anything under
/// CPU/PPU/Memory (which don't know input sources exist at all - they only
/// ever see NES_GamePad::Player1's already-resolved button states).
#pragma once
#include <string>

namespace NES
{
    /// The 8 button names this interface (and NES_GamePad::Controller,
    /// which these ultimately feed) uses: "U","D","L","R","A","B","START","SELECT".
    class InputSource
    {
    public:
        virtual ~InputSource() = default;

        /// Refresh this source's internal state. Call once per frame,
        /// before any IsDown() calls for that frame.
        virtual void Poll() = 0;

        /// True if `button` is currently held according to this source.
        virtual bool IsDown(const std::string& button) const = 0;

        /// Like IsDown(), for controller `player` (1 or 2). By default a source only serves player 1; sources
        /// that can tell two players apart (e.g. two gamepads) override this.
        virtual bool IsDownForPlayer(int player, const std::string& button) const { return player == 1 && IsDown(button); }

        /// False if this source couldn't be opened/initialized (e.g. no
        /// gamepad plugged in, or no X11 display available) - callers
        /// should skip Poll()/IsDown() on an unavailable source rather than
        /// querying a dead one every frame.
        virtual bool Available() const = 0;

        /// Re-reads the saved bindings (called when the settings program changed the config files).
        virtual void Reload() {}

        /// Human-readable name for logging/menus ("Keyboard", "Gamepad").
        virtual std::string Name() const = 0;

        // --- Optional remapping support - default no-op; override only in
        // sources that can actually be rebound. ---

        /// Whether this input source's buttons can be rebound; false by default.
        virtual bool SupportsRemap() const { return false; }

        /// Non-blocking: call once per frame while in "waiting for the next
        /// button press" mode. Returns a source-specific label for whatever
        /// physical input was just pressed ("w", "Joystick button 2", ...),
        /// or an empty string if nothing new was pressed since the last
        /// call. The returned label is opaque to callers - it's only ever
        /// round-tripped back into Bind() on the same source.
        virtual std::string PollForCapture() { return {}; }

        /// Binds `button` (one of the 8 names above) to whatever
        /// PollForCapture() last returned.
        virtual void Bind(const std::string& button, const std::string& label) { (void)button; (void)label; }

        /// Persists current bindings (e.g. to a config file next to the
        /// executable) so they survive a restart.
        virtual void SaveBindings() const {}
    };
}
