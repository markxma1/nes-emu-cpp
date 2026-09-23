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
#include "Mapper.h"

namespace NES
{
    /// @brief Mapper 4 (MMC3): http://wiki.nesdev.com/w/index.php/MMC3
    ///
    /// Unlike MMC1's serial port, MMC3 decodes a *parallel* register bank
    /// across $8000-$FFFF, selected by address bit 0 (even/odd) within
    /// each $2000 quarter:
    /// - $8000/even: bank select (which of 8 internal registers R0-R7 the
    ///   next $8001 write updates, plus the PRG/CHR bank-layout mode bits)
    /// - $8001/odd: bank data (the value to store into that register)
    /// - $A000/even: mirroring (vertical/horizontal only - no single-screen
    ///   mode, unlike MMC1/AxROM)
    /// - $A001/odd: PRG-RAM enable/write-protect - accepted, not acted on
    ///   (see the class comment on what's out of scope)
    /// - $C000/even, $C001/odd, $E000/even, $E001/odd: the scanline IRQ
    ///   counter's latch/reload/disable/enable registers
    ///
    /// LEARNING NOTE / known limitation, once approximated rather than
    /// skipped entirely, now FIXED for real (see below): the scanline IRQ
    /// is clocked from PPU A12 toggling during background/sprite CHR
    /// fetches, used by many MMC3 games to split the screen - e.g. keeping
    /// a status bar stationary while the playfield scrolls.
    ///
    /// HISTORY, kept for context: this port's PPU used to render a whole
    /// frame at once from a single end-of-frame snapshot rather than
    /// stepping scanline-by-scanline in sync with the CPU, so there was no
    /// real PPU A12 signal to count transitions of. What was implemented
    /// then (OnFrame(), this method's predecessor) was a frame-granularity
    /// *approximation* of the same counter/reload/fire logic real MMC3
    /// hardware uses - see http://wiki.nesdev.com/w/index.php/MMC3 ("IRQ
    /// counter") - clocked a fixed 240 times (NTSC's visible scanline
    /// count) in one burst per rendered frame instead of once per real
    /// scanline. That made a game that merely *waits for the IRQ to fire at
    /// all* (found live via this project's own Tiny Toon Adventures
    /// investigation - stuck in its boot sequence in a pattern consistent
    /// with waiting on an interrupt that never fires) actually progress,
    /// but couldn't reproduce *where on screen* the split happens - a
    /// status-bar-style effect still scrolled with the rest of the
    /// playfield instead of staying fixed. Confirmed live (later in the
    /// same overall effort, after two unrelated NMI-timing fixes elsewhere
    /// didn't change this) as also the real cause of Tiny Toon Adventures'
    /// title screen showing scrambled letters and stray leftover HUD
    /// digits: this ROM rewrites all 6 CHR registers (R0-R5) in a tight
    /// burst dozens of times within a couple of real seconds, with
    /// irqLatch set to a nonzero value (0xC0 observed) - a textbook
    /// status-bar CHR-bank split (re-bank once per real scanline-IRQ so the
    /// top HUD strip and the main playfield/logo art read from different
    /// halves of CHR-ROM). Composing a whole frame from one static bank
    /// snapshot necessarily picked *one* of the two layouts for the
    /// *entire* screen.
    ///
    /// FIXED: this port's PPU now has a real per-scanline clock
    /// (NES_PPU::AdvanceDots()/OnScanlineStart()), which calls
    /// OnScanline() (this method, renamed from OnFrame()) once per real
    /// visible scanline (0-239) instead of bursting 240 clocks once per
    /// frame - see OnScanline()'s own .cpp comment. A game's IRQ-driven
    /// CHR-bank split now takes effect starting at the real scanline it
    /// should, so both the "wait for IRQ" progress issue and the wrong-half
    /// -of-CHR-ROM rendering are fixed by the same underlying change.
    class Mapper_MMC3 : public Mapper
    {
    public:
        void OnScanline() override;
        std::string DescribeChrBanks() const override;

        // New: see Mapper::SerializeState()'s own
        // comment.
        void SerializeState(std::vector<uint8_t>& out) const override;
        void DeserializeState(const uint8_t*& in, const uint8_t* end) override;

    protected:
        void OnInstall() override;
        void WriteRegister(uint16_t address, uint8_t value) override;

    private:
        uint8_t bankSelect = 0;
        uint8_t r[8] = {};

        uint8_t irqLatch = 0;
        uint8_t irqCounter = 0;
        bool irqReloadFlag = false;
        bool irqEnabled = false;

        void WriteBankSelect(uint8_t value);
        void WriteBankData(uint8_t value);
        void WriteMirroring(uint8_t value);
        void WriteIrqLatch(uint8_t value);
        void WriteIrqReload();
        void WriteIrqDisable();
        void WriteIrqEnable();
        void ClockIrqCounter();

        void ApplyPrgBanks();
        void ApplyChrBanks();
    };
}
