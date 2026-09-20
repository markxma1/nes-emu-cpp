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
/// @brief CPU-correctness harness, not a port of anything - new test-only
/// code exercising the already-ported NES_CPU against nestest.nes, the
/// standard 6502 core conformance ROM (http://www.nesdev.org/wiki/Emulator_tests
/// "the best test to start with when getting a CPU emulator working").
/// nestest.log is a golden trace (PC + registers before every single
/// instruction) recorded from Nintendulator, an emulator whose CPU core is
/// known-correct; this replays the same instruction stream through
/// NES_CPU::Step() and diffs register state against that log line by line,
/// stopping at the first mismatch - much more direct than reverse-engineering
/// a real game ROM's machine code by hand to find a CPU bug (see the git
/// history/commit messages around the "STAGE 1 never progresses" Galaga
/// investigation this was built to help finish).
///
/// nestest's own documented convention (same wiki page): running it through
/// a real reset vector lands in an interactive/visual test mode; setting PC
/// to $C000 directly at power-up instead enters its automated mode, which is
/// what nestest.log was captured from.
#include "NES_Console.h"
#include "NES_CPU.h"
#include "NES_Register.h"
#include "NES_PPU_Register.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    struct ExpectedState
    {
        uint16_t pc;
        uint8_t a, x, y, p, sp;
        int lineNumber;
        std::string raw;
    };

    /// Finds "TOKEN:" in `line` and parses the following hex byte/word.
    uint32_t ParseField(const std::string& line, const std::string& token, size_t width)
    {
        size_t pos = line.find(token + ":");
        if (pos == std::string::npos)
            throw std::runtime_error("nestest.log line missing field \"" + token + "\": " + line);
        pos += token.size() + 1;
        return static_cast<uint32_t>(std::stoul(line.substr(pos, width), nullptr, 16));
    }

    ExpectedState ParseLine(const std::string& line, int lineNumber)
    {
        ExpectedState s{};
        s.lineNumber = lineNumber;
        s.raw = line;
        s.pc = static_cast<uint16_t>(std::stoul(line.substr(0, 4), nullptr, 16));
        s.a = static_cast<uint8_t>(ParseField(line, "A", 2));
        s.x = static_cast<uint8_t>(ParseField(line, "X", 2));
        s.y = static_cast<uint8_t>(ParseField(line, "Y", 2));
        s.p = static_cast<uint8_t>(ParseField(line, "P", 2));
        s.sp = static_cast<uint8_t>(ParseField(line, "SP", 2));
        return s;
    }
}

int main(int argc, char** argv)
{
    const std::string romPath = argc > 1 ? argv[1] : "nestest.nes";
    const std::string logPath = argc > 2 ? argv[2] : "nestest.log";

    std::ifstream logFile(logPath);
    if (!logFile)
    {
        std::cerr << "Could not open " << logPath << std::endl;
        return 2;
    }

    try
    {
        NES::NES_Console::INIT();
        NES::NES_Console::LoadRom(romPath);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load " << romPath << ": " << e.what() << std::endl;
        return 2;
    }

    // nestest's automated-test entry point and documented starting register
    // state (both the wiki page and nestest.log's first line agree:
    // A:00 X:00 Y:00 P:24 SP:FD, PC:C000) - see the file header comment.
    NES::NES_PPU_Register::InitialAtPower();
    NES::NES_Register::PC = 0xC000;
    NES::NES_Register::S = 0xFD;
    NES::NES_Register::P.P = 0x24;
    NES::NES_Register::A = 0;
    NES::NES_Register::X = 0;
    NES::NES_Register::Y = 0;

    std::string line;
    int lineNumber = 0;
    int matched = 0;
    while (std::getline(logFile, line))
    {
        ++lineNumber;
        if (line.empty())
            continue;

        ExpectedState expected = ParseLine(line, lineNumber);

        uint16_t actualPC = NES::NES_Register::PC;
        uint8_t actualA = NES::NES_Register::A;
        uint8_t actualX = NES::NES_Register::X;
        uint8_t actualY = NES::NES_Register::Y;
        uint8_t actualP = NES::NES_Register::P.P;
        uint8_t actualSP = NES::NES_Register::S;

        if (actualPC != expected.pc || actualA != expected.a || actualX != expected.x ||
            actualY != expected.y || actualP != expected.p || actualSP != expected.sp)
        {
            std::cerr << "MISMATCH at nestest.log line " << expected.lineNumber
                       << " (" << matched << " instructions matched correctly first)\n"
                       << "  expected: " << expected.raw << "\n"
                       << "  actual:   PC=" << std::hex << actualPC
                       << " A=" << static_cast<int>(actualA)
                       << " X=" << static_cast<int>(actualX)
                       << " Y=" << static_cast<int>(actualY)
                       << " P=" << static_cast<int>(actualP)
                       << " SP=" << static_cast<int>(actualSP) << std::dec << "\n";
            if (expected.lineNumber > 5003)
                std::cerr << "  (past line 5003 - nestest's unofficial/illegal opcode "
                             "tests start around here; this may be an unimplemented "
                             "unofficial opcode rather than an official-opcode bug)\n";
            return 1;
        }

        ++matched;
        NES::NES_CPU::Step();
    }

    std::cout << "PASS: all " << matched << " instructions matched nestest.log" << std::endl;
    return 0;
}
