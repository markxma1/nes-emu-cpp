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

namespace NES
{
    /// Controller ($2000) > write. Various flags controlling PPU operation.
    /// Bit layout VPHB SINN. http://wiki.nesdev.com/w/index.php/PPU_registers
    struct PPUCTRLFlags
    {
        std::shared_ptr<AddressSetup> adress;

        /// nametable select (NN): base nametable ($2000/$2400/$2800/$2C00).
        uint8_t N() const { return static_cast<uint8_t>(adress->value() & 0x3); }
        void N(uint8_t v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x3)); adress->value(static_cast<uint8_t>(adress->value() | (v & 0x3))); }

        /// increment mode (I): VRAM address increment per PPUDATA access (0: +1, 1: +32).
        bool I() const { return (adress->value() & 0x4) > 0; }
        void I(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x4)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x4)); }

        /// sprite tile select (S): sprite pattern table for 8x8 sprites.
        bool S() const { return (adress->value() & 0x8) > 0; }
        void S(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x8)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x8)); }

        /// background tile select (B): background pattern table ($0000/$1000).
        bool B() const { return (adress->value() & 0x10) > 0; }
        void B(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x10)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x10)); }

        /// sprite height (H): sprite size (0: 8x8, 1: 8x16).
        bool H() const { return (adress->value() & 0x20) > 0; }
        void H(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x20)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x20)); }

        /// PPU master/slave (P).
        bool P() const { return (adress->value() & 0x40) > 0; }
        void P(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x40)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x40)); }

        /// NMI enable (V): generate an NMI at the start of vblank.
        bool V() const { return (adress->value() & 0x80) > 0; }
        void V(bool v);
    };

    /// Mask ($2001) > write. Controls rendering of sprites/background and colour effects.
    /// Bit layout BGRs bMmG.
    struct PPUMASKFlags
    {
        std::shared_ptr<AddressSetup> adress;

        /// greyscale (G).
        bool N0() const { return (adress->value() & 0x1) > 0; }
        void N0(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x1)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x1)); }

        /// background left column enable (m).
        bool m() const { return (adress->value() & 0x2) > 0; }
        void m(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x2)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x2)); }

        /// sprite left column enable (M).
        bool M() const { return (adress->value() & 0x4) > 0; }
        void M(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x4)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x4)); }

        /// background enable (b).
        bool b() const { return (adress->value() & 0x8) > 0; }
        void b(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x8)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x8)); }

        /// sprite enable (s).
        bool s() const { return (adress->value() & 0x10) > 0; }
        void s(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x10)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x10)); }

        /// color emphasis (BGR).
        uint8_t BGR() const { return static_cast<uint8_t>(adress->value() & 0xE0); }
        void BGR(uint8_t v) { adress->value(static_cast<uint8_t>(adress->value() & ~0xE0)); adress->value(static_cast<uint8_t>(adress->value() | (v & 0xE0))); }
    };

    /// Status ($2002) < read. VSO----- ; read resets the $2005/$2006 write pair.
    struct PPUSTATUSFlags
    {
        std::shared_ptr<AddressSetup> adress;

        /// sprite overflow (O).
        bool O() const { return (adress->value() & 0x20) > 0; }
        void O(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x20)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x20)); }

        /// sprite 0 hit (S).
        bool S() const { return (adress->value() & 0x40) > 0; }
        void S(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x40)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x40)); }

        /// vblank (V).
        bool V() const { return (adress->value() & 0x80) > 0; }
        void V(bool v) { adress->value(static_cast<uint8_t>(adress->value() & ~0x80)); if (v) adress->value(static_cast<uint8_t>(adress->value() | 0x80)); }
    };

    /// @brief The PPU's memory-mapped registers ($2000-$2007, $4014).
    class NES_PPU_Register
    {
    public:
        // TEMPORARY diagnostic aid - opt-in via NES_TRACE_HUDCLEAR_FOLLOWUP
        // (see NES_CPU.cpp's own Step()), off (0) by default. Set by
        // INITPPUDATA()'s AfterSet hook the instant it sees the write that
        // clears Tiny Toon Adventures' status-bar nametable rows to 0;
        // NES_CPU::Step() then logs the next N executed instructions'
        // raw (PC, opcode) pairs so the control flow right after that
        // clear can be inspected without needing a separate, possibly
        // wrongly-banked memory dump.
        static long hudClearFollowupTraceRemaining;
        static PPUCTRLFlags PPUCTRL;
        static PPUMASKFlags PPUMASK;
        static PPUSTATUSFlags PPUSTATUS;

        /// OAM read/write address.
        static std::shared_ptr<AddressSetup> OAMADDR;
        /// OAM data read/write.
        static std::shared_ptr<AddressSetup> OAMDATA;
        /// Fine scroll position (two writes: X, Y).
        static std::shared_ptr<AddressSetup> PPUSCROLL;
        /// PPU read/write address (two writes: MSB, LSB).
        static std::shared_ptr<AddressSetup> PPUADDR;
        /// PPU data read/write.
        static std::shared_ptr<AddressSetup> PPUDATA;
        /// OAM DMA high address.
        static std::shared_ptr<AddressSetup> OAMDMA;

        /// Address of the PPU's internal VRAM pointer.
        static uint16_t PPUPCADDR;

        NES_PPU_Register();

        static void InitialAtPower();
        static void InitialOnReset();

        // New: user-requested save/load-state feature
        // (see NES_SaveState's own comment). Public (rather than a purely
        // private implementation detail) specifically so NES_SaveState can
        // capture/restore the one piece of $2006 write-sequence state that
        // isn't backed by an AddressSetup cell (and so isn't already
        // covered by a save snapshotting NES_Memory::Memory's raw bytes) -
        // same reasoning already applied to a few other fields this
        // session (e.g. NES_PPU::ClearFreshTileCaches()).
        static uint8_t GetPpuAddrHighLatch() { return ppuAddrHighLatch; }
        static void SetPpuAddrHighLatch(uint8_t value) { ppuAddrHighLatch = value; }

    private:
        static void INITPPUCTRL();
        static void INITPPUMASK();
        static void INITPPUSTATUS();
        static void INITOAMADDR();
        static void INITOAMDATA();
        static void INITPPUSCROLL();
        static void INITPPUADDR();
        static void INITPPUDATA();
        static void INITOAMDMA();
        static void PPUSCROLLRESSET();

        /// New: see INITPPUADDR()'s own FIXED note: holds
        /// $2006's first (high-byte) write between the two writes of a
        /// real address-set sequence, now that the shared NES_PPU::ScrollXoY
        /// write-toggle (not a blind "every write shifts left 8" assumption)
        /// decides which half of the sequence a given write is.
        static uint8_t ppuAddrHighLatch;
    };
}
