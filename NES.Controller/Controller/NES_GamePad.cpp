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
#include "NES_GamePad.h"
#include <iostream>
#include <cstdlib>
#include "NES_Memory.h"

namespace NES
{
    NES_GamePad::Controller NES_GamePad::Player1;
    NES_GamePad::Controller NES_GamePad::Player2;
    NES_GamePad::Controller NES_GamePad::Player3;
    NES_GamePad::Controller NES_GamePad::Player4;
    InputFlags NES_GamePad::input4016;
    OutputFlags NES_GamePad::output4016;
    int NES_GamePad::P1BID = 0;
    int NES_GamePad::P2BID = 0;
    bool NES_GamePad::strobeHigh = false;

    NES_GamePad::NES_GamePad()
    {
        InitInput4016();
        InitOutput4016();
    }

    // FIXED (to make the controller actually work): InitInput4016/InitOutput4016
    // previously indexed
    // `NES_Memory::Memory` with the *decimal* literal 4016 (= $0FB0, a
    // mirrored zero-page/RAM cell - see NES_Memory::InitMemory's $0800-$0FFF
    // mirror block) instead of the controller port address $4016 (= decimal
    // 16406). As written, $4016 reads/writes (LDA/STA $4016, the CPU's only
    // way to poll the controller - http://wiki.nesdev.com/w/index.php/
    // Standard_controller, http://wiki.nesdev.com/w/index.php/Controller_reading)
    // never touched this class's BeforGet/AfterGet hooks at all, so no game
    // could ever read Player1's button state - this was the actual bug behind
    // "hatte ein paar Fehler" that made the original unplayable. `Joystick`
    // (NES_Memory::InitJoystick(), correctly built from Memory[0x4016] and
    // Memory[0x4017]) shows 0x4016 was always the intended address elsewhere
    // in the same codebase. Corrected to the literal 0x4016 here.
    void NES_GamePad::InitInput4016()
    {
        input4016.address = NES_Memory::Memory[0x4016];
        input4016.address->AfterSet([](uint8_t v)
        {
            strobeHigh = (v & 0x01) != 0;
            // While the strobe bit is high the real shift register is
            // continuously reloaded from the buttons, so the next read after
            // it falls always starts at the first button (A) -
            // http://wiki.nesdev.com/w/index.php/Controller_reading
            if (strobeHigh)
                P1BID = P2BID = 0; // one strobe line reloads both controllers' shift registers
        });
    }

    void NES_GamePad::InitOutput4016()
    {
        output4016.address = NES_Memory::Memory[0x4016];
        output4016.address->BeforGet([]() { getButton(); });
        output4016.address->AfterGet([]() { output4016.address->value(0); });
        // Controller 2 is read at $4017 (writes to it go to the APU frame counter, see NES_APU_Register.cpp).
        auto p2 = NES_Memory::Memory[0x4017];
        p2->value(0x40);
        p2->BeforGet([]() { getButton2(); });
        p2->AfterGet([p2]() { p2->value(0x40); });
    }

    // FIXED (found while
    // investigating why several real commercial ROMs across multiple
    // mappers - Contra/UxROM, Chip 'n Dale/MMC1, Batman III/MMC3, The Lion
    // King/AxROM - never progressed past their title screen even with a
    // simulated START press, while NROM/CNROM titles whose tested frames
    // didn't require any button press first, e.g. Galaga/Super Mario Bros'
    // opening screens, rendered fine): per
    // http://wiki.nesdev.com/w/index.php/Standard_controller ("While S
    // (strobe) is 1, the shift registers in the controllers are continuously
    // reloaded from the button states ... reading $4016 ... will
    // continuously return the current state of the first button" A), and
    // only stops reloading - starts actually shifting through the other 7
    // buttons one per read - once strobe goes low. getButton() previously
    // never consulted the strobe bit at all - P1BID just free-ran, advancing
    // on *every* read regardless of strobe, forever. A single full "strobe
    // high-then-low, then read all 8 buttons" poll happens to still work by
    // coincidence (any 8 consecutive reads land back on a multiple of 8, so
    // P1BID's wraparound accidentally realigns to button A again) - but
    // real boot code commonly does extra, shorter probes first (e.g. a
    // strobe + single read to quickly check just one button, such as "is
    // START held to skip the intro"), each of which permanently shifts
    // P1BID's phase out of sync with what any *later* full 8-read poll
    // expects at each position - so the game ends up reading a mostly
    // arbitrary, unrelated button state instead of the one it actually
    // asked for, indistinguishable from "the player never presses
    // anything". Fixed by pinning P1BID to 0 (always re-reporting button A,
    // never advancing) for as long as strobe is held high, matching real
    // hardware's continuous-reload behavior; P1BID only starts advancing
    // again once strobe goes low.
    //
    // FIXED (second bug in the same area, found via real interactive play -
    // "im Spiel fühlen sich alle Knöpfe gedrückt an, nicht im Menü", i.e.
    // buttons appearing to behave as if several were held down at once
    // during actual gameplay, but not in the ROM-picker menu, which polls
    // input through a completely separate path - see main.cpp's
    // AnySourceDown()): the strobe check above originally read
    // `input4016.strobe()`, i.e. `input4016.address->value() & 0x01` - but
    // `input4016.address` and `output4016.address` are the *same*
    // NES_Memory::Memory[0x4016] cell (both InitInput4016()/InitOutput4016()
    // fetch it), and InitOutput4016()'s AfterGet hook zeroes that entire
    // cell after *every read*. So a game that holds strobe high across
    // several reads in a row (real, legal, and common - e.g. re-reading
    // button A a few times as a crude debounce) saw strobe correctly on the
    // *first* read of that sequence, but every read after the first
    // silently found strobe already cleared back to 0 by the *previous*
    // read's own AfterGet - even though the CPU never actually wrote $4016
    // low again. P1BID then incorrectly started advancing mid-sequence, so
    // the very next "expected button A" read could return B, Select, Start,
    // or any of the other 7 buttons instead - indistinguishable from
    // several buttons being held at once. Fixed by tracking strobe in its
    // own dedicated `strobeHigh` flag (see NES_GamePad.h's NOTE on it),
    // updated only by real writes to $4016 (InitInput4016()'s AfterSet
    // hook), so a later read can never clobber what the CPU actually last
    // wrote.
    bool NES_GamePad::ShiftOut(Controller& pad, int& index)
    {
        if (strobeHigh)
            index = 0; // strobe high: the register keeps reloading, so button A is reported again and again
        // After all 8 buttons have been shifted out a standard controller returns 1 on every further read.
        // http://wiki.nesdev.com/w/index.php/Controller_reading
        bool bit = (index > 7) ? true : pad.Button[static_cast<size_t>(index)].second.load();
        if (!strobeHigh && index <= 7)
            ++index;
        return bit;
    }

    void NES_GamePad::getButton()
    {
        if (NES_GETENV("NES_TRACE_PAD"))
            std::cerr << "[pad] idx=" << P1BID << " strobe=" << strobeHigh << std::endl;
        output4016.SerialControllerData(ShiftOut(Player1, P1BID));
        // Bits 7-5 of a $4016/$4017 read are open bus: the CPU data bus still
        // holds the high byte of the address just read ($40), so bit 6 reads
        // back as 1 - http://wiki.nesdev.com/w/index.php/Open_bus_behavior
        output4016.OpenBus(0x40);
    }

    void NES_GamePad::getButton2()
    {
        auto cell = NES_Memory::Memory[0x4017];
        cell->value(static_cast<uint8_t>(0x40 | (ShiftOut(Player2, P2BID) ? 1 : 0)));
    }
}
