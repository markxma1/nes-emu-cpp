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
/// @file Settings.h
/// @brief User settings (window size, sound volume, second gamepad) kept in `settings.cfg`.
///
/// The file lives next to the executable (like `keyboard.cfg` and `gamepad.cfg`), has one
/// `key=value` per line and is written by the settings program `tools/nes_settings.py` (or by the
/// window-size hotkeys). The emulator looks at the file's modification time about twice a second
/// and applies changes while the game runs, so nothing has to be restarted.
#include <algorithm>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace NES
{
    /// The values of settings.cfg. Every field has a default, so a missing file or line is fine.
    struct Settings
    {
        int scale = 3;         ///< game window: every NES pixel is scale x scale screen pixels (1-8)
        int viewerScale = 1;   ///< same for the debug windows (name table, pattern table, ...) (1-4)
        int volume = 100;      ///< sound volume in percent (0-100)
        bool twoPads = false;  ///< first gamepad = player 1, second gamepad = player 2
        int hdScale = 0;       ///< skin layer (HdLayer): 0 = off, 2-8 = picture is enlarged this many times

        static constexpr const char* kFile = "./settings.cfg";

        /// Reads the file; unknown or broken lines are ignored, values are clamped to their range.
        static Settings Load()
        {
            Settings s;
            std::ifstream f(kFile);
            std::string line;
            while (std::getline(f, line))
            {
                auto eq = line.find('=');
                if (line.empty() || line[0] == '#' || eq == std::string::npos)
                    continue;
                std::string key = line.substr(0, eq), value = line.substr(eq + 1);
                int number = 0;
                try { number = std::stoi(value); } catch (...) { continue; }
                if (key == "scale") s.scale = std::clamp(number, 1, 8);
                else if (key == "viewer_scale") s.viewerScale = std::clamp(number, 1, 4);
                else if (key == "volume") s.volume = std::clamp(number, 0, 100);
                else if (key == "two_pads") s.twoPads = number != 0;
                else if (key == "hd_scale") s.hdScale = number <= 0 ? 0 : std::clamp(number, 2, 8);
            }
            return s;
        }

        /// Writes the file (used by the window-size hotkeys).
        void Save() const
        {
            std::ofstream f(kFile);
            f << "# NES emulator settings - edited by tools/nes_settings.py (safe to hand-edit)\n"
              << "scale=" << scale << "\nviewer_scale=" << viewerScale << "\nvolume=" << volume
              << "\ntwo_pads=" << (twoPads ? 1 : 0) << "\nhd_scale=" << hdScale << "\n";
        }

        /// Modification time of a file in seconds (0 if it does not exist); used to notice changes.
        static long long ModifiedTime(const char* path)
        {
            struct stat st{};
            return stat(path, &st) == 0 ? static_cast<long long>(st.st_mtime) * 1000000000LL + st.st_mtim.tv_nsec : 0;
        }
    };
}
