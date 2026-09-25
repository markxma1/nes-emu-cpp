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
/// @brief Gamepad InputSource, using the Linux kernel joystick API
/// (/dev/input/jsN - see http://www.kernel.org/doc/Documentation/input/joystick-api.txt)
/// rather than a library like SDL, since this device already exists on any
/// Linux box with a controller plugged in - no extra dependency needed for
/// "is this button down". New UI-shell code, not a port of anything - runs
/// alongside KeyboardInputSource (see InputSource.h), never replacing it.
#pragma once
#include "InputSource.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace NES
{
    /// FIXED: originally opened only the *first* of /dev/input/js0-3 that
    /// succeeded, so with two controllers plugged in at once (e.g. an Xbox
    /// pad via its USB wireless receiver, which shows up as soon as the
    /// receiver is plugged in even with no actual pad connected to it yet,
    /// *and* a Bluetooth DualShock 4) the one the player actually wanted to
    /// use could easily be the one that got ignored, with no way to tell
    /// which one "won". Opens *all* of them now and merges button state the
    /// same way main.cpp already merges keyboard + gamepad - any connected
    /// pad pressing a bound button counts, so it doesn't matter which port/
    /// receiver order things happened to enumerate in.
    class GamepadInputSource : public InputSource
    {
    public:
        /// Opens every /dev/input/js0 .. js3 that succeeds (not just the
        /// first). No gamepad plugged in is not an error - Available() just
        /// reports false and the emulator runs keyboard-only.
        GamepadInputSource();
        ~GamepadInputSource() override;

        void Poll() override;
        bool IsDown(const std::string& button) const override;
        bool Available() const override { return !devices.empty(); }
        std::string Name() const override;

        /// Gamepads support button remapping, so this returns true.
        bool SupportsRemap() const override { return true; }
        std::string PollForCapture() override;
        void Bind(const std::string& button, const std::string& label) override;
        void SaveBindings() const override;

    private:
        void LoadBindings();
        void SetDefaultBindings();
        bool IsLabelActive(const std::string& label) const;

        static constexpr int kMaxButtons = 32;
        static constexpr int kMaxAxes = 16;
        /// Analog stick / D-pad axis magnitude past which an axis counts as
        /// "held in that direction" - roughly half of the signed 16-bit
        /// range most joystick drivers report axis values in.
        static constexpr int kAxisThreshold = 16000;

        /// One open /dev/input/jsN device and its own button/axis state -
        /// kept separate per device so one controller's stale/centered axes
        /// can't mask another's real input when merged in IsLabelActive().
        struct Device
        {
            int fd = -1;
            std::string path;
            std::array<bool, kMaxButtons> buttonDown{};
            std::array<bool, kMaxButtons> prevButtonDown{};
            std::array<int, kMaxAxes> axisValue{};
            std::array<int, kMaxAxes> prevAxisValue{};
        };
        std::vector<Device> devices;

        /// button name -> label, e.g. "Button 0" or "Axis 1+"/"Axis 1-" -
        /// shared across all connected pads (they're assumed similar enough
        /// - e.g. two same-brand pads - that one binding set makes sense;
        /// wildly different controllers may need re-remapping per session).
        std::unordered_map<std::string, std::string> bindings;
    };
}
