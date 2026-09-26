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
#include "EnvFlag.h"
#include "GamepadInputSource.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef __linux__
#include <cstdint>
#include <fcntl.h>
#include <linux/joystick.h>
#include <unistd.h>
#endif

namespace NES
{
    GamepadInputSource::GamepadInputSource()
    {
        SetDefaultBindings();
#ifdef __linux__
        for (int i = 0; i < 4; ++i)
        {
            std::string path = "/dev/input/js" + std::to_string(i);
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0)
            {
                Device d;
                d.fd = fd;
                d.path = path;
                devices.push_back(d);
            }
        }
#endif
        twoPads = NES_GETENV("NES_TWO_PADS") && devices.size() >= 2;
        if (devices.empty())
            std::cerr << "No gamepad found at /dev/input/js0-3 - running keyboard-only "
                         "(this is normal if nothing is plugged in)." << std::endl;
        LoadBindings();
    }

    GamepadInputSource::~GamepadInputSource()
    {
#ifdef __linux__
        for (auto& d : devices)
            close(d.fd);
#endif
    }

    std::string GamepadInputSource::Name() const
    {
        if (devices.empty())
            return "Gamepad";
        if (devices.size() == 1)
            return "Gamepad (" + devices[0].path + ")";
        std::string names = "Gamepads (";
        for (size_t i = 0; i < devices.size(); ++i)
            names += (i ? ", " : "") + devices[i].path;
        return names + ")";
    }

    void GamepadInputSource::SetDefaultBindings()
    {
        // http://www.kernel.org/doc/Documentation/input/joystick-api.txt -
        // most gamepads report the D-pad as axis 0 (X) / axis 1 (Y) and the
        // four "face" buttons starting at button 0, matching a typical
        // SNES-style layout closely enough to use as sane defaults; anyone
        // with a different layout can remap via the in-emulator menu.
        bindings = {
            {"L", "Axis 0-"}, {"R", "Axis 0+"}, {"U", "Axis 1-"}, {"D", "Axis 1+"},
            {"A", "Button 1"}, {"B", "Button 0"}, {"START", "Button 9"}, {"SELECT", "Button 8"}
        };
    }

    void GamepadInputSource::Poll()
    {
#ifdef __linux__
        for (auto& d : devices)
        {
            d.prevButtonDown = d.buttonDown;
            d.prevAxisValue = d.axisValue;

            js_event event{};
            while (read(d.fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)))
            {
                uint8_t type = event.type & ~JS_EVENT_INIT; // strip the "this is the initial sync state" flag
                if (type == JS_EVENT_BUTTON && event.number < kMaxButtons)
                    d.buttonDown[event.number] = event.value != 0;
                else if (type == JS_EVENT_AXIS && event.number < kMaxAxes)
                    d.axisValue[event.number] = event.value;
            }
        }
#endif
    }

    bool GamepadInputSource::IsLabelActiveOn(size_t deviceIndex, const std::string& label) const
    {
#ifdef __linux__
        const Device& d = devices[deviceIndex];
        if (label.rfind("Button ", 0) == 0)
        {
            int n = std::atoi(label.c_str() + 7);
            return n >= 0 && n < kMaxButtons && d.buttonDown[static_cast<size_t>(n)];
        }
        if (label.rfind("Axis ", 0) == 0 && !label.empty())
        {
            char sign = label.back();
            int n = std::atoi(label.c_str() + 5);
            if (n < 0 || n >= kMaxAxes)
                return false;
            int v = d.axisValue[static_cast<size_t>(n)];
            return sign == '+' ? v > kAxisThreshold : v < -kAxisThreshold;
        }
#else
        (void)deviceIndex;
        (void)label;
#endif
        return false;
    }

    bool GamepadInputSource::IsLabelActive(const std::string& label) const
    {
        for (size_t i = 0; i < devices.size(); ++i)
            if (IsLabelActiveOn(i, label))
                return true;
        return false;
    }

    bool GamepadInputSource::IsDownForPlayer(int player, const std::string& button) const
    {
        if (!twoPads)
            return player == 1 && IsDown(button);
        size_t index = player == 1 ? 0 : 1;
        if (index >= devices.size())
            return false;
        auto it = player == 2 ? bindings.find("P2." + button) : bindings.end();
        if (it == bindings.end())
            it = bindings.find(button);
        return it != bindings.end() && IsLabelActiveOn(index, it->second);
    }

    bool GamepadInputSource::IsDown(const std::string& button) const
    {
        auto it = bindings.find(button);
        return it != bindings.end() && IsLabelActive(it->second);
    }

    std::string GamepadInputSource::PollForCapture()
    {
#ifdef __linux__
        for (const auto& d : devices)
        {
            for (int i = 0; i < kMaxButtons; ++i)
                if (d.buttonDown[static_cast<size_t>(i)] && !d.prevButtonDown[static_cast<size_t>(i)])
                    return "Button " + std::to_string(i);
            for (int i = 0; i < kMaxAxes; ++i)
            {
                bool wasActive = std::abs(d.prevAxisValue[static_cast<size_t>(i)]) > kAxisThreshold;
                bool isActive = std::abs(d.axisValue[static_cast<size_t>(i)]) > kAxisThreshold;
                if (isActive && !wasActive)
                    return "Axis " + std::to_string(i) + (d.axisValue[static_cast<size_t>(i)] > 0 ? "+" : "-");
            }
        }
#endif
        return {};
    }

    void GamepadInputSource::Bind(const std::string& button, const std::string& label)
    {
        if (label.empty())
            return;
        bindings[button] = label;
    }

    void GamepadInputSource::SaveBindings() const
    {
        std::ofstream f("./gamepad.cfg");
        if (!f)
        {
            std::cerr << "Warning: could not write ./gamepad.cfg - gamepad bindings won't persist." << std::endl;
            return;
        }
        f << "# NES emulator gamepad bindings - button=\"Button N\" or \"Axis N+/-\"\n";
        f << "# Regenerated by the in-emulator remap menu; safe to hand-edit.\n";
        for (const auto& [button, label] : bindings)
            f << button << "=" << label << "\n";
    }

    void GamepadInputSource::LoadBindings()
    {
        std::ifstream f("./gamepad.cfg");
        if (!f)
            return;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string button = line.substr(0, eq);
            std::string label = line.substr(eq + 1);
            if (!label.empty() && label.back() == '\r')
                label.pop_back();
            if (!label.empty())
                bindings[button] = label;
        }
    }
}
