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
/// @file nes_render.cpp
/// @brief Offline video renderer: replays a recorded game and writes a finished video.
///
/// `nes-render ROM.nes input.txt out.mp4 [scale=4] [skins folder]`
///
/// Play the game once (any speed, the input recording key `Y` writes `<rom>.inputs.txt`), then let this
/// program replay the same input with the picture rendered as slowly and beautifully as you like: skin
/// layer (HdLayer) at 2-8x with every frame composed (none dropped), sound recorded into the same file. It
/// runs without a window or a speed limit, and needs `ffmpeg` on the PATH. The emulation is deterministic,
/// so the replay shows exactly the recorded game. Scale 1 = plain NES picture without skins.
///
/// Without the skin layer and without input this is also how a training run can save its games: keep
/// only the button log and render it later.
#include "INES.h"
#include "Interrupt.h"
#include "NES_ROM.h"
#include "NES_Console.h"
#include "NES_APU.h"
#include "NES_CPU.h"
#include "NES_GamePad.h"
#include "NES_PPU.h"
#include "HdLayer.h"

#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cstdio>
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
    void SetButton(const std::string& fullName, bool down)
    {
        const bool player2 = fullName.rfind("P2.", 0) == 0;
        const std::string name = player2 ? fullName.substr(3) : fullName;
        for (auto& b : (player2 ? NES::NES_GamePad::Player2 : NES::NES_GamePad::Player1).Button)
            if (b.first == name)
                b.second = down;
    }

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

    std::string Absolute(const std::string& path)
    {
        char abs[4096];
        return realpath(path.c_str(), abs) ? abs : path;
    }
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "usage: nes-render ROM.nes input.txt out.mp4 [scale=4] [skins folder]\n"
                  << "example: ./build/nes-render game.nes game.nes.inputs.txt game.mp4 4 build/skins/game" << std::endl;
        return 2;
    }
    const std::string rom = Absolute(argv[1]);
    const std::string inputPath = Absolute(argv[2]);
    // the output file may not exist yet: make its folder absolute
    std::string out = argv[3];
    if (out[0] != '/')
    {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)))
            out = std::string(cwd) + "/" + out;
    }
    const int scale = argc > 4 ? std::max(1, std::min(8, std::atoi(argv[4]))) : 4;
    const std::string skins = argc > 5 ? Absolute(argv[5]) : "";

    std::map<long long, std::vector<std::pair<std::string, bool>>> events;
    long long lastEvent = 0;
    {
        std::ifstream in(inputPath);
        if (!in)
        {
            std::cerr << "Cannot read the input file " << argv[2] << std::endl;
            return 1;
        }
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            long long f;
            std::string button;
            int down;
            if (ls >> f >> button >> down)
            {
                events[f].push_back({button, down != 0});
                lastEvent = std::max(lastEvent, f);
            }
        }
    }
    long long frames = lastEvent + 120; // two seconds after the last button press
    if (const char* n = std::getenv("NES_RENDER_FRAMES"))
        frames = std::atoll(n);

    const std::string wav = out + ".wav", silent = out + ".video.mp4";
    setenv("NES_AUDIO_WAV", wav.c_str(), 1); // makes the APU record the sound (read on the first Reset)
    ChdirToExecutableDir();
    try
    {
        NES::NES_Console::INIT();
        NES::NES_Console::LoadRom(rom);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Could not load \"" << argv[1] << "\": " << e.what() << std::endl;
        return 1;
    }
    NES::NES_CPU::mod = NES::Mod::none;

    const int width = 256 * scale, height = 240 * scale;
    const std::string command = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr24 -s " + std::to_string(width) + "x" +
                                std::to_string(height) + " -framerate 60.0988 -i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p \"" + silent + "\"";
    FILE* video = popen(command.c_str(), "w");
    if (!video)
    {
        std::cerr << "Could not start ffmpeg (is it installed?)" << std::endl;
        return 1;
    }

    if (scale > 1)
    {
        NES::HdLayer::SetScale(scale);
        NES::HdLayer::SetSynchronous(true); // every frame is composed before the next one runs
        if (!skins.empty())
            NES::HdLayer::SetPackDir(skins);
    }
    NES::NES_CPU::frameHook = [&](long long frame)
    {
        auto it = events.find(frame);
        if (it != events.end())
            for (const auto& [button, down] : it->second)
                SetButton(button, down);
        cv::Mat picture;
        if (scale > 1)
        {
            NES::HdLayer::OnFrame(frame);
            NES::HdLayer::Compose(picture);
        }
        if (picture.empty())
            cv::resize(NES::NES_Console::getDisplay().Image(), picture, cv::Size(width, height), 0, 0, cv::INTER_NEAREST);
        fwrite(picture.data, 1, picture.total() * picture.elemSize(), video);
        if (frame >= frames)
            NES::Interrupt::POWER = false;
    };

    auto start = std::chrono::steady_clock::now();
    NES::NES_Console::Run();
    pclose(video);
    NES::NES_APU::WriteWav(wav.c_str());
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const std::string mux = "ffmpeg -loglevel error -y -i \"" + silent + "\" -i \"" + wav + "\" -c:v copy -c:a aac -b:a 192k -shortest \"" + out + "\"";
    if (std::system(mux.c_str()) != 0)
    {
        std::cerr << "ffmpeg could not add the sound; the silent video is " << silent << " and the sound " << wav << std::endl;
        return 1;
    }
    std::remove(silent.c_str());
    std::remove(wav.c_str());
    std::cout << NES::NES_CPU::completedFrames.load() << " frames rendered at " << width << "x" << height << " in " << sec << " s -> " << out << std::endl;
    return 0;
}
