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
#include "NES_PPU.h"
#include "NES_PPU_Register.h"
#include <cstdlib>
#include <iostream>

namespace NES
{
    bool NES_PPU::ScrollXoY = true; // false: next PPUSCROLL write is Y, true: next is X
    int NES_PPU::xScrollTemp = 0;
    int NES_PPU::yScrollTemp = 0;
    int NES_PPU::xScroll = 0;
    int NES_PPU::yScroll = 0;
    int NES_PPU::rawXScroll = 0;
    int NES_PPU::rawYScroll = 0;
    uint16_t NES_PPU::loopyT = 0;
    uint16_t NES_PPU::splitV = 0;
    uint8_t NES_PPU::fineX = 0;
    bool NES_PPU::splitActive = false;
    bool NES_PPU::splitPrevRendered = false;
    int NES_PPU::splitStartLine = 0;

    /// One vertical step of v, as the PPU does at dot 256 of every scanline
    /// (fine Y, then coarse Y with the 29 -> 0 nametable flip).
    static uint16_t IncrementVerticalAddress(uint16_t v)
    {
        int fy = (v >> 12) & 7;
        if (fy < 7)
            return static_cast<uint16_t>(v + 0x1000);
        v = static_cast<uint16_t>(v & ~0x7000);
        int cy = (v >> 5) & 31;
        if (cy == 29)
        {
            cy = 0;
            v ^= 0x0800;
        }
        else
            cy = (cy + 1) & 31;
        return static_cast<uint16_t>((v & ~0x03E0) | (cy << 5));
    }

    void NES_PPU::LoopyWriteControl(uint8_t value)
    {
        loopyT = static_cast<uint16_t>((loopyT & ~0x0C00) | ((value & 0x03) << 10));
    }

    void NES_PPU::LoopyWriteScroll(uint8_t value, bool firstWrite)
    {
        if (firstWrite)
        {
            loopyT = static_cast<uint16_t>((loopyT & ~0x001F) | (value >> 3));
            fineX = value & 0x07;
        }
        else
        {
            loopyT = static_cast<uint16_t>((loopyT & ~0x73E0) | ((value & 0x07) << 12) | ((value >> 3) << 5));
        }
    }

    void NES_PPU::LoopyWriteAddress(uint8_t value, bool firstWrite)
    {
        if (firstWrite)
        {
            loopyT = static_cast<uint16_t>((loopyT & 0x00FF) | ((value & 0x3F) << 8));
            return;
        }
        loopyT = static_cast<uint16_t>((loopyT & 0xFF00) | value);
        // Second $2006 write copies t into v. On a visible scanline with
        // rendering on, that re-points the rest of the frame's rows.
        bool rendering = NES_PPU_Register::PPUMASK.b() || NES_PPU_Register::PPUMASK.s();
        if (NES_GETENV("NES_TRACE_SPLIT"))
            std::cerr << "[$2006 v=t] t=0x" << std::hex << loopyT << std::dec << " scanline=" << currentScanline
                      << " rendering=" << rendering << std::endl;
        // The load always reaches v, even while rendering is switched off
        // (Dracula's status bar does exactly that); rendering then continues
        // from v when it is switched back on.
        if (currentScanline >= 0 && currentScanline < 240)
        {
            splitActive = true;
            splitV = loopyT;
            splitStartLine = currentScanline + 1;
            splitPrevRendered = false;
            // A load before dot 256 is still followed by that scanline's own
            // vertical increment; after dot 256 it has already happened.
            if (rendering && currentDot < 256)
                splitV = IncrementVerticalAddress(splitV);
        }
    }

    // Called at the start of each visible scanline, before it is drawn.
    // While a split is active, derive this row's scroll from splitV.
    void NES_PPU::ApplySplitScroll(int scanline)
    {
        if (!splitActive || scanline < splitStartLine)
            return;
        bool rendering = NES_PPU_Register::PPUMASK.b() || NES_PPU_Register::PPUMASK.s();
        // v only advances (and only re-copies t's horizontal bits) on
        // scanlines the PPU actually rendered.
        if (scanline > splitStartLine && splitPrevRendered)
            splitV = IncrementVerticalAddress(splitV);
        splitPrevRendered = rendering;
        if (rendering)
            splitV = static_cast<uint16_t>((splitV & ~0x041F) | (loopyT & 0x041F));

        int coarseX = splitV & 31;
        int coarseY = (splitV >> 5) & 31;
        int fineY = (splitV >> 12) & 7;
        int ntX = (splitV >> 10) & 1;
        int ntY = (splitV >> 11) & 1;
        xScroll = ntX * 256 + coarseX * 8 + fineX;
        int logicalY = ntY * 240 + (coarseY % 30) * 8 + fineY;
        yScroll = logicalY - scanline;
    }

    /// PPUSCROLL is write-only on real hardware; a CPU read of $2005 isn't
    /// meaningful, so this always returns 0 (deliberately).
    uint8_t NES_PPU::Scroll()
    {
        return 0;
    }

    void NES_PPU::Scroll(uint8_t value)
    {
        LoopyWriteScroll(value, ScrollXoY);
        if (ScrollXoY)
        {
            XScroll(value);
        }
        else
        {
            // FIXED: this used to do `if (value > 239) value -= 255;` on a
            // `uint8_t`, and compound assignment on a byte re-truncates the
            // int result back to a byte immediately, same as an explicit
            // uint8_t cast would.
            //
            // Per http://wiki.nesdev.com/w/index.php/PPU_scrolling, raw
            // PPUSCROLL Y writes of 240-255 are the documented "attribute
            // glitch" range: there is no nametable row 240-255 on real
            // hardware, so writing a value there makes the PPU read
            // attribute-table bytes as if they were tile indices. This
            // branch normalizes such an out-of-range write by reinterpreting
            // it as a small negative offset (treating it as a signed byte:
            // 240 -> -16, ..., 255 -> -1) - exactly the kind of value
            // DrawBackground()/DrawDisplayFrame() already special-case via
            // their "if (YScroll() < 0)" branches for scrolling just past a
            // nametable's top edge.
            //
            // That byte truncation discarded that sign before
            // YScroll(int) ever saw it: in mod-256 arithmetic, subtracting
            // 255 is the same as adding 1, so the old net effect was
            // `value + 1` (240 became 241 - still positive, still outside
            // the intended 0-239 range) instead of the intended -16. Fixed
            // here by keeping the result as a signed int (`value - 256`
            // instead of `value - 255` truncated to uint8_t), so YScroll()
            // actually receives the negative offset DrawBackground() is
            // built to handle.
            if (value > 239)
                YScroll(static_cast<int>(value) - 256);
            else
                YScroll(value);
        }
        ScrollXoY = !ScrollXoY;
    }

    /// Latched X scroll used for rendering; see Draw()'s comment in
    /// NES_PPU.h/.Display.cpp for why this can lag the live `xScroll` by one frame.
    int NES_PPU::XScroll()
    {
        return xScrollTemp;
    }

    void NES_PPU::XScroll(int value)
    {
        // FIXED: this used to call
        // `AddxScroll(value)` for its side-effect but discarded the return
        // value - unlike YScroll's setter, which does `value =
        // AddyScroll(value)`. So PPUCTRL's nametable-X-select bit (N & 1,
        // http://wiki.nesdev.com/w/index.php/PPU_scrolling, "Write the 9th
        // bit of X ... to bit[s] 0 ... of PPUCTRL") never actually offset
        // xScroll/xScrollTemp the way the Y-axis equivalent offsets
        // yScroll/yScrollTemp - horizontal nametable switching via PPUCTRL
        // was silently broken. Fixed by assigning the result, matching
        // YScroll(int)'s pattern.
        //
        // FIXED (real bug, found via a live trace + a user-captured before/
        // after save-state pair pinning down the exact moment: Chip 'n
        // Dale's own NMI handler writes $2005 (X) *before* $2000 (PPUCTRL) -
        // c08b: STA $2005 then c09e: STA $2000, disassembled from a save
        // taken right at the bug - both perfectly legal on real hardware,
        // since $2005 and $2000 write different bit fields of the same
        // shared `t` register there, combined only once at actual render
        // time, regardless of write order. This port's simplified model
        // instead baked `AddxScroll()`'s decision in immediately, at $2005-
        // write time, using whatever PPUCTRL's nametable-select bit
        // *currently* was - i.e. the *previous* frame's bit, since the
        // game's write to the *new* bit (from its own zero-page $fd) always
        // arrives a few instructions later in the same handler. Confirmed
        // live: a save taken right after the reported "jump" showed
        // PPUCTRL.N=1 (game had already flipped the bit) but xScroll=0 (not
        // 256) - the value the earlier, stale-PPUCTRL computation had
        // already locked in and never revisited. Fixed by storing the raw,
        // pre-add value (rawXScroll) and re-deriving xScroll from it in
        // RecomputeXScroll(), called again from NES_PPU_Register's $2000
        // write hook - so a same-frame PPUCTRL write arriving after this
        // one still takes effect instead of being silently dropped.
        if (NES_GETENV("NES_TRACE_YSCROLL_SPLIT"))
            std::cerr << "[XScroll] value=" << value << " scanline=" << CurrentScanline() << std::endl;
        rawXScroll = value;
        RecomputeXScroll();
    }

    // New: see XScroll(int)'s own FIXED note on the
    // write-order bug this exists to close. Re-derives xScroll purely from
    // the last raw $2005 X write and PPUCTRL's *current* nametable-select
    // bit - safe to call any number of times (idempotent for unchanged
    // inputs), so both XScroll(int) (a new raw write) and PPUCTRL's own
    // write hook (a same-frame PPUCTRL change after that write) can trigger
    // it without either one needing to know about the other's timing.
    void NES_PPU::RecomputeXScroll()
    {
        int value = AddxScroll(rawXScroll);
        if (splitActive)
            return;
        xScroll = value;
        if (!Draw())
            xScrollTemp = value;
    }

    /// Latched Y scroll used for rendering; see XScroll()'s comment.
    int NES_PPU::YScroll()
    {
        return yScrollTemp;
    }

    void NES_PPU::YScroll(int value)
    {
        // FIXED - same write-order bug as XScroll(int), see its own FIXED
        // note; fixed the same way (store the raw value, re-derive via
        // RecomputeYScroll() so a later PPUCTRL write still takes effect).
        if (NES_GETENV("NES_TRACE_YSCROLL_SPLIT"))
            std::cerr << "[YScroll] value=" << value << " scanline=" << CurrentScanline() << std::endl;
        rawYScroll = value;
        RecomputeYScroll();
    }

    // New: see RecomputeXScroll()'s own comment, same
    // reasoning for the Y axis.
    void NES_PPU::RecomputeYScroll()
    {
        int value = AddyScroll(rawYScroll);
        if (splitActive)
            return;
        yScroll = value;
        if (!Draw())
            yScrollTemp = value;
    }

    /// Real hardware stores the scroll position's 9th X bit and 9th Y bit as
    /// PPUCTRL ($2000) bits 0 and 1 respectively - see
    /// http://wiki.nesdev.com/w/index.php/PPU_scrolling ("Write the 9th bit
    /// of X and Y to bits 0 and 1, respectively, of PPUCTRL"). This class
    /// reproduces that by adding a full nametable's worth of pixels (240 for
    /// Y, 256 for X in AddxScroll() below) instead of setting a raw bit,
    /// giving a flat 0..479 (Y) / 0..511 (X) logical scroll space that
    /// DrawBackground() then samples from with wraparound.
    int NES_PPU::AddyScroll(int value)
    {
        int add = ((NES_PPU_Register::PPUCTRL.N() & 2) == 0) ? 0 : 240;
        value += add;
        return value;
    }

    /// See AddyScroll() - same idea for PPUCTRL bit 0 (X nametable select).
    int NES_PPU::AddxScroll(int value)
    {
        int add = ((NES_PPU_Register::PPUCTRL.N() & 1) == 0) ? 0 : 256;
        value += add;
        return value;
    }
}
