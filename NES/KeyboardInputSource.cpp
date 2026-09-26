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
#include "KeyboardInputSource.h"

#include <cstdio>
#include <fstream>
#include <iostream>

#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace NES
{
    const std::unordered_map<std::string, std::string> KeyboardInputSource::arrowAliases = {
        {"U", "KEY_UP"}, {"D", "KEY_DOWN"}, {"L", "KEY_LEFT"}, {"R", "KEY_RIGHT"}
    };

    namespace
    {
#ifdef __linux__
        // Only the keys this emulator ever binds to something, by name - not
        // an attempt at a complete evdev keycode table. CodeToLabel() falls
        // back to "KEY_<n>" for anything not listed here, so remapping to an
        // unlisted key still works, just with a less friendly config label.
        const std::vector<std::pair<int, const char*>>& KeyTable()
        {
            static const std::vector<std::pair<int, const char*>> table = {
                {KEY_A, "KEY_A"}, {KEY_B, "KEY_B"}, {KEY_C, "KEY_C"}, {KEY_D, "KEY_D"},
                {KEY_E, "KEY_E"}, {KEY_F, "KEY_F"}, {KEY_G, "KEY_G"}, {KEY_H, "KEY_H"},
                {KEY_I, "KEY_I"}, {KEY_J, "KEY_J"}, {KEY_K, "KEY_K"}, {KEY_L, "KEY_L"},
                {KEY_M, "KEY_M"}, {KEY_N, "KEY_N"}, {KEY_O, "KEY_O"}, {KEY_P, "KEY_P"},
                {KEY_Q, "KEY_Q"}, {KEY_R, "KEY_R"}, {KEY_S, "KEY_S"}, {KEY_T, "KEY_T"},
                {KEY_U, "KEY_U"}, {KEY_V, "KEY_V"}, {KEY_W, "KEY_W"}, {KEY_X, "KEY_X"},
                {KEY_Y, "KEY_Y"}, {KEY_Z, "KEY_Z"},
                {KEY_0, "KEY_0"}, {KEY_1, "KEY_1"}, {KEY_2, "KEY_2"}, {KEY_3, "KEY_3"},
                {KEY_4, "KEY_4"}, {KEY_5, "KEY_5"}, {KEY_6, "KEY_6"}, {KEY_7, "KEY_7"},
                {KEY_8, "KEY_8"}, {KEY_9, "KEY_9"},
                {KEY_UP, "KEY_UP"}, {KEY_DOWN, "KEY_DOWN"}, {KEY_LEFT, "KEY_LEFT"}, {KEY_RIGHT, "KEY_RIGHT"},
                {KEY_ENTER, "KEY_ENTER"}, {KEY_KPENTER, "KEY_KPENTER"}, {KEY_SPACE, "KEY_SPACE"},
                {KEY_TAB, "KEY_TAB"}, {KEY_ESC, "KEY_ESC"},
                {KEY_LEFTSHIFT, "KEY_LEFTSHIFT"}, {KEY_RIGHTSHIFT, "KEY_RIGHTSHIFT"},
                {KEY_LEFTCTRL, "KEY_LEFTCTRL"}, {KEY_RIGHTCTRL, "KEY_RIGHTCTRL"},
                {KEY_LEFTALT, "KEY_LEFTALT"}, {KEY_RIGHTALT, "KEY_RIGHTALT"},
            };
            return table;
        }
#endif
    }

    std::string KeyboardInputSource::CodeToLabel(int code)
    {
#ifdef __linux__
        for (const auto& [c, name] : KeyTable())
            if (c == code)
                return name;
#endif
        return "KEY_" + std::to_string(code);
    }

    int KeyboardInputSource::LabelToCode(const std::string& label)
    {
#ifdef __linux__
        for (const auto& [c, name] : KeyTable())
            if (label == name)
                return c;
#endif
        if (label.rfind("KEY_", 0) == 0)
        {
            try { return std::stoi(label.substr(4)); }
            catch (...) { return -1; }
        }
        return -1;
    }

    KeyboardInputSource::KeyboardInputSource()
    {
        SetDefaultBindings();
        OpenDevices();
        if (deviceFds.empty())
        {
            std::cerr << "Warning: KeyboardInputSource found no readable keyboard device under "
                         "/dev/input - keyboard input will not work.\n"
                         "  Most likely fix: add yourself to the 'input' group, then log out and "
                         "back in:\n"
                         "    sudo usermod -aG input $USER\n"
                         "  (/dev/input/eventN is root:input, mode 660 by default - this is a "
                         "one-time setup step, not a bug.)" << std::endl;
        }
        LoadBindings();
    }

    KeyboardInputSource::~KeyboardInputSource()
    {
#ifdef __linux__
        for (int fd : deviceFds)
            close(fd);
#endif
    }

    void KeyboardInputSource::OpenDevices()
    {
#ifdef __linux__
        for (int i = 0; i < 32; ++i)
        {
            std::string path = "/dev/input/event" + std::to_string(i);
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0)
                continue;

            // Heuristic: a real keyboard supports a broad spread of letter
            // keys; a mouse/touchpad's EV_KEY bits (button clicks) never
            // include KEY_A or KEY_Z. Checking both filters out non-keyboard
            // event nodes without needing libudev just to ask "is this a
            // keyboard".
            unsigned long keybits[(KEY_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {};
            bool looksLikeKeyboard = false;
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0)
            {
                auto hasKey = [&](int code) {
                    return (keybits[code / (8 * sizeof(unsigned long))] >> (code % (8 * sizeof(unsigned long)))) & 1;
                };
                looksLikeKeyboard = hasKey(KEY_A) && hasKey(KEY_Z) && hasKey(KEY_SPACE);
            }

            if (looksLikeKeyboard)
                deviceFds.push_back(fd);
            else
                close(fd);
        }
#endif
    }

    void KeyboardInputSource::SetDefaultBindings()
    {
        bindings = {
            {"U", "KEY_W"}, {"D", "KEY_S"}, {"L", "KEY_A"}, {"R", "KEY_D"},
            {"A", "KEY_K"}, {"B", "KEY_J"}, {"START", "KEY_ENTER"}, {"SELECT", "KEY_SPACE"}
        };
    }

    void KeyboardInputSource::Poll()
    {
#ifdef __linux__
        prevKeyDown = keyDown;
        for (int fd : deviceFds)
        {
            struct input_event event{};
            while (read(fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)))
            {
                if (event.type != EV_KEY || event.code >= kKeyMax)
                    continue;
                keyDown[event.code] = event.value != 0; // 0 = up, 1 = down, 2 = autorepeat (still down)
            }
        }
#endif
    }

    bool KeyboardInputSource::IsCodeDown(int code) const
    {
        return code >= 0 && code < kKeyMax && keyDown[static_cast<size_t>(code)];
    }

    bool KeyboardInputSource::IsDown(const std::string& button) const
    {
        auto it = bindings.find(button);
        if (it != bindings.end() && IsCodeDown(LabelToCode(it->second)))
            return true;

        auto alias = arrowAliases.find(button);
        if (alias != arrowAliases.end() && IsCodeDown(LabelToCode(alias->second)))
            return true;

        return false;
    }

    bool KeyboardInputSource::IsDownForPlayer(int player, const std::string& button) const
    {
        if (player == 1)
            return IsDown(button);
        auto it = bindings.find("P2." + button);
        return it != bindings.end() && IsCodeDown(LabelToCode(it->second));
    }

    std::string KeyboardInputSource::PollForCapture()
    {
        for (int code = 0; code < kKeyMax; ++code)
            if (keyDown[static_cast<size_t>(code)] && !prevKeyDown[static_cast<size_t>(code)])
                return CodeToLabel(code);
        return {};
    }

    void KeyboardInputSource::Bind(const std::string& button, const std::string& label)
    {
        if (label.empty())
            return;
        bindings[button] = label;
    }

    void KeyboardInputSource::SaveBindings() const
    {
        std::ofstream f("./keyboard.cfg");
        if (!f)
        {
            std::cerr << "Warning: could not write ./keyboard.cfg - key bindings won't persist." << std::endl;
            return;
        }
        f << "# NES emulator keyboard bindings - button=evdev key label (e.g. KEY_W)\n";
        f << "# Regenerated by the in-emulator remap menu; safe to hand-edit.\n";
        for (const auto& [button, label] : bindings)
            f << button << "=" << label << "\n";
    }

    void KeyboardInputSource::LoadBindings()
    {
        std::ifstream f("./keyboard.cfg");
        if (!f)
            return; // no saved config yet - defaults from SetDefaultBindings() stand.
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
