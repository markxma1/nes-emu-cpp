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
#include "AddressSetup.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace NES
{
    /// @brief The CPU's write side of $4016: bit 0 is the controller's
    /// "strobe"/OUT line.
    ///
    /// Real hardware puts an 8-bit parallel-in/serial-out shift register (a
    /// 4021 IC) inside each controller. While strobe is held high, that
    /// register is continuously reloaded from the physical button states, so
    /// any $4016/$4017 read during that time keeps returning button A's
    /// state over and over. Only once strobe goes low does the register stop
    /// reloading and start shifting out one latched button per subsequent
    /// read (A, B, Select, Start, Up, Down, Left, Right - the exact order
    /// `Controller::Button` below is built in). This class models that
    /// single strobe bit as the (write-only, from the CPU's view) address
    /// cell it lives on. http://wiki.nesdev.com/w/index.php/Standard_controller
    struct InputFlags
    {
        /// The $4016 address cell whose bit 0 is the strobe line.
        std::shared_ptr<AddressSetup> address;

        /// While strobe is high, the shift register keeps reloading from the
        /// button states and $4016/$4017 reads keep returning button A;
        /// once it goes low, buttons can be read back one at a time.
        bool strobe() const { return (address->value() & 0x01) > 0; }
        /// Sets or clears the strobe bit (bit 0) in the cell.
        void strobe(bool v) { address->value(static_cast<uint8_t>(address->value() & ~0x01)); if (v) address->value(static_cast<uint8_t>(address->value() | 0x01)); }
    };

    /// @brief The CPU's read side of $4016/$4017: what a `LDA $4016` actually
    /// sees.
    ///
    /// On real hardware bit 0 of the read byte is the shift register's
    /// serial output (the current button, shifted out one at a time - see
    /// InputFlags above), and the upper bits float as "open bus" (whatever
    /// was last driven on the CPU data bus, not meaningful controller data -
    /// http://wiki.nesdev.com/w/index.php/Controller_reading, "the D1-D4
    /// pins are only pulled up ... reading $4016/$4017 will typically
    /// contain ... open bus"). `SerialControllerData` models bit 0, `OpenBus`
    /// the rest (mask 0xE0 here rather than the full 0xF8).
    class OutputFlags
    {
    public:
        /// The $4016/$4017 address cell the CPU reads (bit 0 serial data, bits 5-7 open bus).
        std::shared_ptr<AddressSetup> address;

        /// The serial output bit (bit 0): the button currently shifted out.
        bool SerialControllerData() const { return (address->value() & 0x01) > 0; }
        /// Sets the serial output bit (bit 0).
        void SerialControllerData(bool v)
        {
            address->value(static_cast<uint8_t>(address->value() & ~0x01));
            if (v) address->value(static_cast<uint8_t>(address->value() | (0x01 & 0x01)));
        }

        /// The open-bus bits (mask 0xE0) of the read byte.
        uint8_t OpenBus() const { return static_cast<uint8_t>(address->value() & 0xE0); }
        /// Sets the open-bus bits (mask 0xE0) of the read byte.
        void OpenBus(uint8_t v) { address->value(static_cast<uint8_t>(address->value() & ~0xE0)); address->value(static_cast<uint8_t>(address->value() | (v & 0xE0))); }
    };

    // `OutputFlags` covers both $4016 and $4017; no per-port subclasses are
    // needed.

    /// @brief The NES controller port(s): models the $4016/$4017 shift-register
    /// protocol described on InputFlags/OutputFlags above, so a game's normal
    /// "strobe then read 8 times" polling loop gets real button data instead
    /// of open bus.    /// http://wiki.nesdev.com/w/index.php/Standard_controller
    /// http://wiki.nesdev.com/w/index.php/Controller_reading
    class NES_GamePad
    {
    public:
        // FIXED (found via real
        // gameplay testing, "right works, left doesn't"): per
        // http://wiki.nesdev.com/w/index.php/Standard_controller, a real
        // controller's 8 sequential reads return A, B, Select, Start, Up,
        // Down, Left, Right in that exact order - getButton() below reads
        // `Player1.Button[P1BID]` positionally in that same sequence, so
        // this vector's order *is* the wire protocol as far as any game's
        // reading code is concerned. The insertion
        // order was previously A,B,SELECT,START,L,R,U,D - positions 4-7 (meant to be
        // Up,Down,Left,Right) were actually L,R,U,D, a silent transposition
        // (Left/Right swapped with Up/Down). Since no real input was
        // originally wired to Player1 at all (see main.cpp's file-level
        // NOTE), this went unnoticed until real keyboard input was added and
        // enough CPU bugs were fixed for a game to actually respond to it. Fixed
        // to the real U,D,L,R order.
        /// Button order matches the real controller's Up/Down/Left/Right
        /// read order (see the FIXED note above) - `getButton()` reads
        /// `Player1.Button[P1BID]` by this same positional order.
        /// All buttons start released (SELECT used to start pressed, which
        /// made games see Select held until the first key event arrived).
        struct Controller
        {
            /// One (name, pressed) entry per button, in the order they are read from $4016: A, B, SELECT, START, U, D, L, R.
            std::vector<std::pair<std::string, bool>> Button;
            Controller()
                : Button{ {"A", false}, {"B", false}, {"SELECT", false}, {"START", false},
                          {"U", false}, {"D", false}, {"L", false}, {"R", false} }
            {
            }
        };

        /// State of controller 1 (the only one wired to $4016).
        static Controller Player1;
        /// State of controller 2 (not wired to any address yet).
        static Controller Player2;
        /// State of controller 3 (not wired to any address yet).
        static Controller Player3;
        /// State of controller 4 (not wired to any address yet).
        static Controller Player4;

        /// Set-only (no getter).
        static void Input4016(InputFlags v) { input4016 = v; }
        /// The read side of $4016 (what the CPU sees when it reads the port).
        static OutputFlags Output4016() { return output4016; }

        NES_GamePad();

    private:
        static InputFlags input4016;
        static OutputFlags output4016;
        // NOTE: static, since the class only ever has one instance
        // (NES_Console::INIT constructs exactly one NES_GamePad).
        static int P1BID;

        // NOTE: see getButton()'s FIXED note (the
        // strobe-bit one) for why this can't just be
        // `input4016.strobe()`/`address->value() & 0x01` anymore: reading
        // $4016 (getButton() itself) zeroes the *whole* stored byte via
        // output4016's AfterGet hook - see InitOutput4016() - which
        // overwrites whatever strobe bit the CPU last wrote to that same
        // shared address cell. Tracked here instead, updated only by actual
        // writes (InitInput4016()'s AfterSet hook), so a later *read*
        // clearing the cell can never make strobe look low again.
        static bool strobeHigh;

        static void InitInput4016();
        static void InitOutput4016();

        /// Called via OutputFlags::address's BeforGet hook, i.e. runs on
        /// every CPU read of $4016 - this is the emulated equivalent of the
        /// real 4021 shift register clocking out its next serial bit. Writes
        /// the current `P1BID`'th button of Player1 into bit 0
        /// (SerialControllerData) and advances P1BID, wrapping after all 8
        /// buttons (A,B,SELECT,START,U,D,L,R - Controller::Button's fixed
        /// insertion order, matching the real hardware's A/B/Select/Start/
        /// Up/Down/Left/Right read order - see Controller's FIXED note).
        /// NOTE: only Player1 is wired up -
        /// $4017 (Player2) has no equivalent hook here (Player2/3/4 are not wired to any
        /// address).
        static void getButton();
    };
}
