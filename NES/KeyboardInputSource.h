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
/// @brief Keyboard InputSource, backed by direct Linux evdev reading
/// (/dev/input/eventN) rather than the display server. See the class body
/// for why this replaced an earlier X11-XQueryKeymap-based version. New
/// UI-shell code, not a port of anything.
#pragma once
#include "InputSource.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace NES
{
    /// FIXED (second attempt - see git history/session notes for the first,
    /// X11-based one this replaced): the previous version polled
    /// XQueryKeymap once per frame to solve simultaneous-key-press
    /// detection, which worked in principle but turned out to report *no*
    /// key ever being down at all on a Wayland/Hyprland desktop (this
    /// project's actual target environment). That's not a logic bug in this
    /// port - it's Wayland's security model deliberately preventing
    /// exactly this kind of "give me the whole system's live keyboard
    /// state" query, even through XWayland's X11 compatibility layer, so no
    /// application can silently read another application's keystrokes.
    /// There is no portable way to get real X11/Wayland-routed keyboard
    /// *state* (as opposed to discrete, single-key-at-a-time *events*, which
    /// is all cv::waitKey ever gave us - see the original simultaneous-press
    /// bug this was all trying to fix) without either implementing a full
    /// Wayland client with real keyboard focus (a much larger undertaking,
    /// and OpenCV's own window's Qt internals aren't exposed for this) or
    /// dropping to a lower level than the display server entirely.
    ///
    /// This reads raw key events directly from the kernel's evdev input
    /// devices (/dev/input/eventN - http://kernel.org/doc/html/latest/input/input.html)
    /// instead, which works identically under X11 and Wayland since it
    /// bypasses the display server completely. The trade-off: evdev nodes
    /// are root:input, mode 660 by default - the user has to be a member of
    /// the `input` group (`sudo usermod -aG input $USER`, then log out and
    /// back in) for this to work at all; Available() reports false and
    /// prints exactly that instruction if no readable keyboard device is
    /// found. The other trade-off, same as the X11 version had: evdev
    /// reports raw, system-wide keyboard state with no window-focus
    /// scoping whatsoever (arguably even more global than XQueryKeymap
    /// would have been) - acceptable for a personal/learning project, not
    /// something to reuse as-is somewhere that matters more.
    class KeyboardInputSource : public InputSource
    {
    public:
        KeyboardInputSource();
        ~KeyboardInputSource() override;

        void Poll() override;
        bool IsDown(const std::string& button) const override;
        /// Player 2 uses its own bindings, stored as `P2.<button>` (none by default; set them in tools/nes_settings.py).
        bool IsDownForPlayer(int player, const std::string& button) const override;
        void Reload() override { SetDefaultBindings(); LoadBindings(); }
        bool Available() const override { return !deviceFds.empty(); }
        std::string Name() const override { return "Keyboard"; }

        /// The keyboard supports key remapping, so this returns true.
        bool SupportsRemap() const override { return true; }
        std::string PollForCapture() override;
        void Bind(const std::string& button, const std::string& label) override;
        void SaveBindings() const override;

    private:
        void OpenDevices();
        void LoadBindings();
        void SetDefaultBindings();
        bool IsCodeDown(int code) const;
        /// Looks up a key's human-readable label ("KEY_W") by its evdev
        /// code, or "KEY_<n>" for anything not in the name table below.
        static std::string CodeToLabel(int code);
        /// Reverse of CodeToLabel() - returns -1 if `label` isn't recognized.
        static int LabelToCode(const std::string& label);

        std::vector<int> deviceFds;
        static constexpr int kKeyMax = 0x300; // linux/input-event-codes.h KEY_MAX + 1
        std::array<bool, kKeyMax> keyDown{};
        std::array<bool, kKeyMax> prevKeyDown{};

        /// button name -> evdev key label (e.g. "U" -> "KEY_W").
        std::unordered_map<std::string, std::string> bindings;

        /// Directions additionally always respond to arrow keys, regardless
        /// of what U/D/L/R are rebound to - losing arrow-key support as a
        /// side effect of rebinding WASD would be a surprising trade a user
        /// probably didn't intend.
        static const std::unordered_map<std::string, std::string> arrowAliases;
    };
}
