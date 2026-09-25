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
#include "NES_PPU_Palette.h"
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace NES
{
    void NES_PPU_Palette::InitPalletesFromBMP(const std::string& path)
    {
        cv::Mat pallete;
        if (access(path.c_str(), R_OK) == 0)
            pallete = cv::imread(path, cv::IMREAD_COLOR);
        if (pallete.empty() && !path.empty() && path[0] != '/')
        {
            // A relative path is looked up in the working directory first, then next to the program
            // (test programs are often started from the repository root).
            char exe[4096];
            ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n > 0)
            {
                std::string dir(exe, static_cast<size_t>(n));
                dir = dir.substr(0, dir.rfind('/'));
                std::string relative = path.rfind("./", 0) == 0 ? path.substr(2) : path;
                pallete = cv::imread(dir + "/" + relative, cv::IMREAD_COLOR);
            }
        }
        if (pallete.empty())
            throw std::runtime_error("NES_PPU_Palette: could not load palette bitmap: " + path);
        LoadPallete(pallete);
    }

    void NES_PPU_Palette::LoadPallete(const cv::Mat& pallete)
    {
        for (int j = 0; j < 4; j++)
            LoadRow(pallete, j);
    }

    void NES_PPU_Palette::LoadRow(const cv::Mat& pallete, int j)
    {
        for (int i = 0; i < 16; i++)
        {
            cv::Vec3b px = pallete.at<cv::Vec3b>(j, i); // OpenCV loads BGR
            PPUpalettes[static_cast<size_t>(i + j * 16)] = NES_PPU::Color(px[2], px[1], px[0]);
        }
    }
}
