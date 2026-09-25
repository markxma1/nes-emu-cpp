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
/// @file nes_bench.cpp
/// @brief Headless speed test: runs a ROM for N frames with no window and no speed limit.
///
/// `nes-bench ROM.nes [frames] [input.txt]` prints frames per second and how many times
/// faster than a real NES (60.1 fps) the emulator ran. The optional input file uses the
/// same "frame BUTTON 0|1" lines as `NES_PLAYBACK_INPUT`. Set `NES_BENCH_RENDER_EVERY=N` to draw only every
/// Nth frame (0 = never), see NES_PPU::SetRenderPixels(). This is the starting point for
/// running the emulator without any UI, e.g. to let a learning agent play many times
/// faster than real time.
#include "INES.h"
#include "Interrupt.h"
#include "NES_ROM.h"
#include "NES_Console.h"
#include "NES_CPU.h"
#include "NES_GamePad.h"
#include "Profiler.h"
#include "NES_PPU.h"
#include "NES_Memory.h"

#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace
{
    void ChdirToExecutableDir()
    {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return;
        buf[n] = 0;
        std::string p(buf);
        p = p.substr(0, p.rfind('/'));
        if (chdir(p.c_str()) != 0)
            std::cerr << "warning: could not change to " << p << std::endl;
    }

    void SetButton(const std::string& name, bool down)
    {
        for (auto& b : NES::NES_GamePad::Player1.Button)
            if (b.first == name)
                b.second = down;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: nes-bench ROM.nes [frames=1000] [input.txt]\n"
                  << "example: ./build/nes-bench tests/roms/timing.nes 600" << std::endl;
        return 2;
    }
    const std::string rom = argv[1];
    const long long frames = argc > 2 ? std::atoll(argv[2]) : 1000;
    const long long renderEvery = std::getenv("NES_BENCH_RENDER_EVERY") ? std::atoll(std::getenv("NES_BENCH_RENDER_EVERY")) : -1;

    std::map<long long, std::vector<std::pair<std::string, bool>>> events;
    if (argc > 3)
    {
        std::ifstream in(argv[3]);
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            long long f;
            std::string button;
            int down;
            if (ls >> f >> button >> down)
                events[f].push_back({button, down != 0});
        }
    }

    // Resolve the ROM before changing directory (the palette asset lives next to the binary).
    char abs[4096];
    const std::string romPath = realpath(rom.c_str(), abs) ? abs : rom;
    NES::Profiler::InitFromEnvironment(); // before changing folder: a relative NES_PROFILE path stays where you started
    ChdirToExecutableDir();

    try
    {
        NES::NES_Console::INIT();
        NES::NES_Console::LoadRom(romPath);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Could not load \"" << rom << "\": " << e.what() << "\n"
                  << "Give the path of a real .nes file, for example the test ROM shipped with this project:\n"
                  << "    ./build/nes-bench tests/roms/timing.nes 600" << std::endl;
        return 1;
    }
    NES::NES_CPU::mod = NES::Mod::none; // no real-time throttle

    NES::NES_CPU::frameHook = [&](long long frame)
    {
        auto it = events.find(frame);
        if (it != events.end())
            for (const auto& [button, down] : it->second)
                SetButton(button, down);
        if (renderEvery >= 0) // render only every Nth frame (0 = never)
            NES::NES_PPU::SetRenderPixels(renderEvery > 0 && (frame + 1) % renderEvery == 0);
        if (frame >= frames)
            NES::Interrupt::POWER = false;
    };

    auto start = std::chrono::steady_clock::now();
    NES::NES_Console::Run(); // reset + run; returns when POWER goes false
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    long long done = NES::NES_CPU::completedFrames.load();
    if (const char* dump = std::getenv("NES_BENCH_DUMP")) // picture of the last frame, to check what was measured
        cv::imwrite(dump, NES::NES_Console::getDisplay().Image());
    if (std::getenv("NES_BENCH_RAM_HASH")) // fingerprint of CPU RAM: equal hashes = same game state
    {
        uint64_t h = 1469598103934665603ull;
        for (int a = 0; a < 0x800; a++)
            h = (h ^ NES::NES_Memory::Memory[static_cast<size_t>(a)]->value()) * 1099511628211ull;
        std::cout << "ram hash " << std::hex << h << std::dec << std::endl;
    }
    std::cout << done << " frames in " << sec << " s = " << done / sec << " fps = " << done / sec / 60.0988
              << "x real time (" << sec / done * 1000 << " ms/frame)" << std::endl;
    return 0;
}
