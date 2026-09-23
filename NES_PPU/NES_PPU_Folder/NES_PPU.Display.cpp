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
#include "NES_PPU.h"
#include "NES_PPU_Register.h"
#include "NES_PPU_Palette.h"
#include "NES_PPU_AttributeTable.h"
#include "../OAM/NES_PPU_OAM.h"
#include "Interrupt.h"
#include "NES_Register.h"
#include <cstdlib>
#include <iostream>
#include <utility>

namespace NES
{
    NES_PPU::Picture NES_PPU::TempDisplay(256, 240);
    bool NES_PPU::draw = false;

    bool NES_PPU::Draw()
    {
        return draw;
    }

    void NES_PPU::Draw(bool value)
    {
        if (value)
            draw = value;
        else
        {
            draw = value;
            xScrollTemp = xScroll;
            yScrollTemp = yScroll;
        }
    }

    // FIXED (this is the "shows Stage
    // 1 then freezes forever" bug): this port has no independent PPU
    // scanline/dot clock; at the time of this fix, the UI's poll loop calling
    // Display() (see NES/main.cpp) was the closest thing to a vblank clock
    // this design had. (UPDATE, later in the same overall
    // effort: Display() is no longer called from the UI thread at all - see
    // NES_Console::RenderFrame()'s own comment - but the vblank-persistence
    // bug this note describes and fixes is unrelated to *which* thread
    // calls Display(), so the fix below still applies unchanged.) Per
    // http://wiki.nesdev.com/w/index.php/NMI: real
    // hardware raises the vblank flag on its own, every single frame,
    // completely independent of software - PPUCTRL bit 7 ("NMI_output")
    // only gates whether that automatic vblank event is *allowed* to
    // interrupt the CPU, and "games typically enable it once during
    // initialization ... and leave it active across all frames" (no
    // per-frame re-enable expected). Display() used to clear PPUCTRL.V() to false right after
    // reading it true once - i.e. treating "NMI enabled" as a one-shot "please render
    // exactly one frame" request that consumes itself, rather than a
    // persistent setting. A game that (like most games, including this one)
    // enables NMI once at boot and never touches PPUCTRL again would get
    // exactly one rendered frame + one NMI, after which PPUCTRL.V() reads
    // false forever - no further frame ever renders and no further NMI ever
    // fires, i.e. a permanent freeze on whatever was on screen at that
    // point. Fixed by no longer clearing the bit here; it's now only ever
    // changed by the game itself (via PPUCTRLFlags::V's setter, still fired
    // through the CPU's normal $2000 write path), matching how real
    // hardware never touches its own enable bit.
    // FIXED (the "hangs right at
    // boot" regression the unofficial-opcode/nestest work exposed): per
    // http://wiki.nesdev.com/w/index.php/PPU_registers, PPUSTATUS's vblank
    // flag (bit 7) "is set at dot 1 of line 241 ... regardless of whether
    // rendering is enabled" - it happens every single vblank, unconditionally.
    // PPUCTRL bit 7 ("NMI_output") only gates whether that automatic event
    // also *interrupts the CPU* (see the NMI-persistence NOTE below on this
    // same function) - it has no bearing on the status flag itself.
    //
    // Previously this only ever called
    // `NES_PPU_Register::PPUSTATUS.V(true)` from *inside* the
    // `if (PPUCTRL.V())` block - so before a game ever writes to PPUCTRL
    // (enabling NMI), the vblank flag could never become true at all. Real
    // NES boot code almost universally starts with exactly this wait
    // (`LDA $2002 / BPL loop`, polling for the very first PPU warm-up
    // vblank) *before* touching PPUCTRL - a chicken-and-egg deadlock that
    // was never reachable before this session's CPU-correctness fixes (the
    // broken CPU never got far enough into a ROM's boot code to hit this
    // exact wait loop). Fixed by setting the status flag unconditionally,
    // every Display() call (this port's per-tick stand-in for a real
    // vblank), independent of whether NMI happens to be enabled yet.
    //
    // FIXED (the "loads fine, CPU
    // runs, PPUMASK shows rendering enabled, but the screen is permanently
    // black" bug found while testing real commercial ROMs beyond Galaga,
    // e.g. Super Mario Bros/NROM and Contra/UxROM): frame composition
    // (DrawBackground/InsertObect/sprite-0-hit below) used to live entirely
    // inside `if (PPUCTRL.V())` - i.e. this port only ever built and
    // displayed a picture when the game's NMI-enable bit happened to be
    // set. Per http://wiki.nesdev.com/w/index.php/PPU_rendering, the PPU
    // renders every single frame whenever PPUMASK's rendering bits are on,
    // completely independently of PPUCTRL bit 7 - that bit only gates
    // whether the automatic per-frame vblank event also interrupts the CPU
    // (see the NMI-persistence NOTE above on this same function), it has no
    // bearing on whether a frame gets rendered. A game that enables
    // background/sprite rendering (PPUMASK) before or without (re-)enabling
    // NMI - which both of these real ROMs do, e.g. while briefly running
    // NMI-less during part of their boot sequence - would previously never
    // get a single frame composited, i.e. a permanent black screen despite
    // the CPU and PPU state otherwise being correct. Fixed by always
    // composing/displaying the frame here, and gating only the actual CPU
    // interrupt (`Interrupt::NMI(true)`) on PPUCTRL.V(), matching real
    // hardware's separation of "vblank happened" from "vblank is allowed to
    // interrupt the CPU".
    // UPDATE (later in the same overall effort - see AdvanceDots()/
    // OnScanlineStart() below): PPUSTATUS.V(true)/Interrupt::NMI(true)/
    // PPUSTATUS.S(false) used to happen right here, unconditionally, once
    // per whole-frame Display() call - the entire "no independent PPU
    // scanline/dot clock" limitation the FIXED note above describes. They
    // now happen from OnScanlineStart(241)/OnScanlineStart(261)
    // respectively, at the real scanline real hardware would raise them,
    // instead of whenever this function next happens to run. Display()
    // itself is still called once per completed 262-scanline frame (from
    // NES_CPU::Run(), gated on AdvanceDots()'s return value) to actually
    // compose the picture - only the vblank/status timing moved, not frame
    // composition itself (see the plan for the per-scanline rendering
    // rewrite that still needs to happen for that part).
    NES_PPU::Picture NES_PPU::Display()
    {
        Draw(true);
        Picture frame(TempDisplay.Width(), TempDisplay.Height());

        // UPDATE (later in the same overall effort - see
        // RenderBackgroundScanline()'s/RenderSpriteScanline()'s own
        // comments): this used to be `InsertObect(true)`/`InsertObect(false)`
        // (deleted) and `DrawBackground(frame, NameTabele())`, each
        // rebuilding its whole layer from a single whole-frame OAM/nametable
        // snapshot right here. All three layers are now built incrementally,
        // one real scanline at a time, by RenderBackgroundScanline()/
        // RenderSpriteScanline() (called from OnScanlineStart() for
        // scanlines 0-239) - by the time Display() runs (once per completed
        // 262-scanline sweep), they already hold a complete frame's worth of
        // content, each row sampled fresh at the real scanline it was
        // needed. Composited in the same order/blend as before (additive,
        // behind-sprites -> background -> front-sprites).
        // Sprite-0-hit (PPUSTATUS.S()) is no longer checked here - see
        // RenderSpriteScanline()'s own FIXED note: it's now set per-
        // scanline, as each scanline's sprite 0 is actually decoded,
        // instead of once here after the whole frame (all 240 scanlines)
        // was already built - by which point it's too late for a game's
        // own mid-frame `BIT $2002` poll to ever observe it in time.
        frame.DrawImage(spriteBehindBuffer, 0, 0);
        frame.DrawImage(backgroundBuffer, 0, 0);
        frame.DrawImage(spriteFrontBuffer, 0, 0);

        NES_PPU_Palette::setAllPaletesAsOld();
        TempDisplay = frame;
        Draw(false);

        return TempDisplay;
    }

    // See the .h's own comment for the full story (the fast-path renderer
    // for NES_RENDER_FROM_STATE) - reproduces exactly what OnScanlineStart()
    // does for scanline 0 (fresh buffers + tile-cache clear) and then for
    // scanlines 0-239 (RenderBackgroundScanline()/RenderSpriteScanline()),
    // then composites via Display() - but as one direct, synchronous loop
    // instead of driven by AdvanceDots()'s real per-instruction cycle
    // stream, since there is no CPU execution happening at all here.
    NES_PPU::Picture NES_PPU::RenderStaticSnapshot()
    {
        backgroundBuffer = Picture(256, 240);
        spriteBehindBuffer = Picture(256, 240);
        spriteFrontBuffer = Picture(256, 240);
        ClearFreshTileCaches();
        for (int screenY = 0; screenY <= 239; screenY++)
        {
            RenderBackgroundScanline(screenY);
            RenderSpriteScanline(screenY);
        }
        return Display();
    }

    int NES_PPU::currentDot = 0;
    int NES_PPU::currentScanline = 0;
    std::function<void()> NES_PPU::scanlineCallback;
    bool NES_PPU::scanlineIrqClockFired = false;
    NES_PPU::Picture NES_PPU::backgroundBuffer(256, 240);
    NES_PPU::Picture NES_PPU::spriteBehindBuffer(256, 240);
    NES_PPU::Picture NES_PPU::spriteFrontBuffer(256, 240);

    // New: see NES_PPU.h's own comment on this function
    // for why it exists (this port had no independent PPU scanline/dot
    // clock at all - found via three separate real-game bugs this session:
    // MMC1 shift-register corruption from too-frequent NMI, MMC3
    // status-bar CHR-bank splits landing on the wrong scanline, and
    // nametable-streaming desync during horizontal scroll). Real
    // hardware's PPU runs at a fixed 3 dots per CPU cycle (NTSC) and
    // completes one scanline every 341 dots, 262 scanlines/frame (240
    // visible, one post-render line, 20 vblank lines, one pre-render line)
    // - http://wiki.nesdev.com/w/index.php/PPU_rendering. NES_CPU::Run()
    // calls this once per Step(), passing that instruction's real executed
    // cycle count (kCycleTable[opcode]) - not a flat per-frame estimate -
    // so scanline boundaries land at the real CPU-cycle offsets real
    // hardware would cross them at.
    // FIXED (found live, this same session, as a real regression -
    // Chip 'n Dale's title screen rendering almost entirely black despite
    // RenderBackgroundScanline() itself producing correct, live pixel
    // data): "frame completed" used to fire when currentScanline wrapped
    // to 0, i.e. *after* OnScanlineStart(0) had already run for the *next*
    // frame - which resets backgroundBuffer to a blank canvas and
    // re-renders only its very first row (see OnScanlineStart()'s
    // `scanline == 0` branch). NES_Console::RenderFrame() (called by
    // NES_CPU::Run() exactly when this returns true) would then read
    // backgroundBuffer in that just-reset, only-one-row-rebuilt state
    // instead of the fully-built 240-row buffer from the frame that had
    // just actually finished. Fixed by signalling completion one scanline
    // earlier, at 240 (the post-render line, right after the last visible
    // scanline 239 finishes) - backgroundBuffer is fully built at that
    // exact point and nothing touches it again until the *next* frame's
    // scanline 0, safely after RenderFrame() has already read it.
    // FIXED (real bug, found live via Tiny Toon Adventures' missing status-
    // bar HUD - see SetScrollFromPPUAddr()'s own comment for the full
    // investigation this grew out of): real MMC3 hardware clocks its scanline
    // IRQ counter off the PPU's A12 address line rising, which happens
    // during sprite pattern-table fetches for the *next* scanline - dots
    // 257-320 of the *current* one, commonly approximated at dot 260 (see
    // http://wiki.nesdev.com/w/index.php/MMC3, "IRQ Specifics"). This port's
    // mapper scanline-IRQ hook used to fire from OnScanlineStart()'s dot-0
    // boundary instead - up to ~27 CPU cycles later than real hardware.
    // Confirmed live via a save-state trace + real disassembly: Tiny Toon's
    // NMI handler does CLI mid-handler (FE78) specifically so the status-bar
    // split IRQ *can* interrupt it, but only safely *before* the handler's
    // own two-write $2006 pair (FE8A/FE8D) - a ~4-cycle-wide window. This
    // port's ~27-cycle-late clocking landed the IRQ dispatch *inside* that
    // pair instead (confirmed via a $2006-write trace showing the split
    // handler's own address write completing using a stale high-byte latch
    // left over from the NMI handler's still-open first write, corrupting
    // both writes and permanently leaving PPUADDR's shared write-toggle
    // desynced) - this, not a missing $2006-affects-scroll code path, was
    // the actual root cause of the HUD never rendering. Fixed by clocking
    // the mapper hook separately, at dot 260 of the scanline *before* the
    // one it affects, matching real hardware's timing closely enough that
    // this exact interleaving with the game's own CLI-protected write
    // sequence no longer collides.
    bool NES_PPU::AdvanceDots(int cpuCycles)
    {
        int newDot = currentDot + cpuCycles * 3;
        if (!scanlineIrqClockFired && currentDot < 260 && newDot >= 260)
        {
            scanlineIrqClockFired = true;
            int nextScanline = (currentScanline + 1) % 262;
            if (std::getenv("NES_TRACE_MMC3_IRQLATCH"))
                std::cerr << "[dot260clock] currentScanline=" << currentScanline
                          << " nextScanline=" << nextScanline
                          << " PC=0x" << std::hex << NES_Register::PC << std::dec << std::endl;
            if (scanlineCallback && nextScanline >= 0 && nextScanline <= 239)
                scanlineCallback();
        }
        currentDot = newDot;
        bool frameCompleted = false;
        while (currentDot >= 341)
        {
            currentDot -= 341;
            currentScanline = (currentScanline + 1) % 262;
            scanlineIrqClockFired = false;
            OnScanlineStart(currentScanline);
            if (currentScanline == 240)
                frameCompleted = true;
        }
        return frameCompleted;
    }

    // New: one hook per real scanline boundary,
    // replacing what Display() used to do unconditionally once per whole
    // frame (see Display()'s own UPDATE note above - only *when* these
    // fire changed here, not *what* they do). Scanline numbering matches
    // http://wiki.nesdev.com/w/index.php/PPU_rendering exactly: 0-239
    // visible, 240 post-render (idle), 241-260 vblank, 261 pre-render.
    void NES_PPU::OnScanlineStart(int scanline)
    {
        if (scanline >= 0 && scanline <= 239)
        {
            // Fresh blank canvas at the start of each frame's background/
            // sprite layers - see RenderBackgroundScanline()'s own comment
            // on why it additively blends (not overwrites) each row: that
            // blend needs a non-stale, freshly black baseline every frame,
            // not last frame's leftover pixels. Same reasoning for the two
            // sprite buffers (see RenderSpriteScanline()).
            if (scanline == 0)
            {
                backgroundBuffer = Picture(256, 240);
                spriteBehindBuffer = Picture(256, 240);
                spriteFrontBuffer = Picture(256, 240);
                ClearFreshTileCaches();
            }

            // Mapper::OnScanline() (real per-scanline hook, see its own
            // comment) - most mappers no-op this; MMC3 uses it to clock its
            // scanline-IRQ counter at the real cadence real hardware would
            // (see SetScanlineCallback()'s own comment for why this is a
            // callback rather than a direct NES_ROM/Mapper dependency).
            //
            // FIXED (moved out of this dot-0 boundary into AdvanceDots()'s
            // own dot-260 check - see its comment for the full reasoning):
            // this used to fire right here, unconditionally, for every
            // scanline 0-239. Real MMC3 hardware clocks around dot 260 of
            // the *previous* scanline instead, up to ~27 CPU cycles earlier
            // than this point - a gap that mattered for at least one real
            // game (Tiny Toon Adventures' CLI-protected NMI handler).
            // RenderBackgroundScanline()/RenderSpriteScanline() below still
            // fire from this dot-0 boundary, matching real hardware: the
            // mapper's bank/CHR changes from the earlier IRQ are already
            // in effect by the time this scanline's own row is decoded.
            RenderBackgroundScanline(scanline);
            RenderSpriteScanline(scanline);
        }
        else if (scanline == 241)
        {
            // TEMPORARY diagnostic aid - opt-in via NES_TRACE_VBLANK241, off
            // by default.
            if (std::getenv("NES_TRACE_VBLANK241"))
                std::cerr << "[VBLANK241] PPUCTRL.V=" << NES_PPU_Register::PPUCTRL.V() << std::endl;
            NES_PPU_Register::PPUSTATUS.V(true);
            if (NES_PPU_Register::PPUCTRL.V())
                Interrupt::NMI(true);
        }
        else if (scanline == 261)
        {
            // "Cleared after reading $2002 and at dot 1 of the pre-render
            // line" (vblank, V) / "cleared ... at dot 1 of the pre-render
            // line" (sprite 0 hit, S) -
            // http://wiki.nesdev.com/w/index.php/PPU_registers. This is in
            // *addition* to, not a replacement for, the existing
            // CPU-read-triggered V clear in NES_PPU_Register.cpp's
            // INITPPUSTATUS() - real hardware clears V on whichever of the
            // two happens first, and clearing an already-clear flag here
            // is harmless. Sprite overflow (O) is never actually computed
            // by this port's renderer yet (declared but unused, same
            // situation S() itself was in before an earlier fix this
            // project made) - cleared here anyway for when it is.
            NES_PPU_Register::PPUSTATUS.V(false);
            NES_PPU_Register::PPUSTATUS.S(false);
            NES_PPU_Register::PPUSTATUS.O(false);
        }
    }

    // FIXED (new design - the direct fix for the Chip 'n
    // Dale nametable-streaming-during-scroll bug this whole redesign was
    // undertaken for): renders exactly one real on-screen scanline (256x1
    // pixels) of pure background into the persistent backgroundBuffer,
    // sampling NES_PPU_Memory/attribute-table/CHR-bank state *fresh* at
    // the moment this specific scanline is needed - not from a single
    // once-per-frame snapshot the way NameTabele()/DrawBackground() (still
    // used only by the debug Name Table viewer - see NES_PPU.h's own
    // comment) built the whole background. A game streaming new nametable
    // columns into VRAM as it scrolls now shows up starting at the real
    // scanline that data was written before, instead of racing an
    // arbitrary once-per-frame sample point.
    //
    // Reads `xScroll`/`yScroll` directly (the *live* scroll position - see
    // NES_PPU.Scroll.cpp) rather than the Draw()-latched XScroll()/YScroll()
    // getters DrawBackground() uses: that latch exists specifically to hold
    // one stable value across an entire whole-frame blit, which no longer
    // applies now that each scanline is sampled at its own real moment in
    // time - using the live value here is what makes a scroll write
    // mid-frame actually take effect starting at the right scanline
    // instead of only from the next frame onward.
    //
    // Quadrant layout matches DrowOneNameTable()'s existing 512x480 logical
    // canvas exactly (top-left=nametable 0, top-right=1, bottom-left=2,
    // bottom-right=3 - see NameTabele()) - reused here at one-tile-row
    // granularity instead of the whole canvas. All 4 logical nametable
    // slots are always validly aliased to a real physical bank regardless
    // of mirroring mode (see NES_PPU_Memory::RewireNameTableMirroring()),
    // so this always decodes real data with no mirroring-mode special
    // casing needed (unlike the older quadrant-skip-and-copy approach).
    //
    // PERFORMANCE NOTE: recomputes the full attribute-table decode + all 32
    // tile lookups on *every* call, even though a whole tile-row's worth of
    // scanlines (up to 8) share the same (nrLeft, localTileRow) key and so
    // often produce an identical result. A first version of this function
    // cached the decoded row keyed by (nrLeft, localTileRow) to avoid
    // NES_PPU_AttributeTable::AttributeTable()'s uncached 960-entry
    // re-decode on every one of up to 480 calls/frame - but that cache had
    // no way to notice the *underlying VRAM* changing between two calls
    // that happen to share the same key (e.g. a title screen's real tile
    // data arriving after the very first, pre-boot call for that tile-row
    // had already cached blank/garbage data) - found live as a real visual
    // regression (Chip 'n Dale's title screen going almost entirely black)
    // the moment this function was first plugged in, exactly the kind of
    // staleness this whole redesign exists to eliminate. Removed rather
    // than fixed with a real dirty-flag (e.g. hooking AddressSetup::isNew()
    // across the relevant nametable/attribute cells) - correctness first;
    // revisit only if this measurably matters (Tile()'s own cache already
    // makes the pattern-table half of this cheap either way).
    void NES_PPU::RenderBackgroundScanline(int screenY)
    {
        // FIXED (real bug, found while investigating a reported Bram
        // Stoker's Dracula "flickers between frames, sometimes normal
        // sometimes text/garbage" symptom): per
        // http://wiki.nesdev.com/w/index.php/PPU_registers ($2001 PPUMASK
        // bit 3, "1: Show background") and
        // http://wiki.nesdev.com/w/index.php/PPU_rendering ("If the
        // background or sprites are disabled ... the backdrop color is
        // shown"), real hardware only fetches/draws nametable tiles for a
        // scanline when PPUMASK's background-enable bit is set; when it's
        // clear, that scanline shows the universal background color
        // (palette index $3F00) instead, not whatever nametable data
        // happens to be sitting in VRAM. This function had no such check at
        // all - unlike RenderSpriteScanline() right below it, which already
        // correctly gates on `PPUMASK.s()` - so background tiles were
        // decoded and drawn unconditionally on every scanline regardless of
        // this bit. Games routinely clear the background-enable bit for a
        // frame (or part of one) specifically to hide an in-progress
        // nametable/attribute-table rewrite (e.g. during a room/screen
        // transition) - with this bug, our renderer would show whatever
        // half-written VRAM state existed at that exact moment instead of
        // the correct solid backdrop color, producing exactly this kind of
        // transient single-frame garbage. Confirmed live: replaying the
        // user's own recorded Dracula input (166 events/3772 frames) and
        // dumping consecutive frames around the point of a real, observed
        // one-frame corruption showed $2001 being written 0x00 (both
        // background and sprites disabled) right at the scanlines
        // immediately preceding it, via `NES_TRACE_WRITE=2001`.
        if (!NES_PPU_Register::PPUMASK.b())
        {
            backgroundBuffer.FillRectangle(NES_PPU_Palette::UniversalBackgroundColor(), 0, screenY, 256, screenY + 1);
            return;
        }

        if (std::getenv("NES_TRACE_YSCROLL_SPLIT") && screenY >= 185 && screenY <= 200)
            std::cerr << "[bg-scanline] screenY=" << screenY << " yScroll=" << yScroll
                      << " xScroll=" << xScroll << std::endl;
        int logicalY = ((yScroll + screenY) % 480 + 480) % 480;
        int tileRow = logicalY / 8;
        int pixelRowWithinTile = logicalY % 8;
        int localTileRow = (tileRow < 30) ? tileRow : tileRow - 30;
        int nrLeft = (tileRow < 30) ? 0 : 2;
        int nrRight = (tileRow < 30) ? 1 : 3;
        if (std::getenv("NES_TRACE_YSCROLL_SPLIT") && screenY >= 216 && screenY <= 239)
            std::cerr << "[bg-bottom] screenY=" << screenY << " yScroll=" << yScroll << " tileRow=" << tileRow
                      << " nrLeft=" << nrLeft << std::endl;

        Picture logicalRow(64 * 8, 8);
        const std::vector<int> attrLeft = NES_PPU_AttributeTable::AttributeTable(nrLeft);
        const std::vector<int> attrRight = NES_PPU_AttributeTable::AttributeTable(nrRight);
        for (int col = 0; col < 32; col++)
        {
            int k = localTileRow * 32 + col;
            uint16_t tLeft = static_cast<uint16_t>(
                NES_PPU_Memory::NameTableN[static_cast<size_t>(nrLeft)][static_cast<size_t>(k)]->Value());
            logicalRow.DrawNewImage(DecodeBackgroundTileFresh(tLeft, attrLeft[static_cast<size_t>(k)]), col * 8, 0);
            uint16_t tRight = static_cast<uint16_t>(
                NES_PPU_Memory::NameTableN[static_cast<size_t>(nrRight)][static_cast<size_t>(k)]->Value());
            logicalRow.DrawNewImage(DecodeBackgroundTileFresh(tRight, attrRight[static_cast<size_t>(k)]), (32 + col) * 8, 0);
        }

        // TEMPORARY diagnostic aid - opt-in via NES_TRACE_TILE0_COLOR, off by
        // default. Dumps tile ID 0's decoded (0,0) pixel color at a handful
        // of scanlines, to check whether tile 0 (which fills the otherwise-
        // blank bottom nametable rows) actually decodes differently before
        // vs. after a mid-frame CHR-bank switch.
        if (std::getenv("NES_TRACE_TILE0_COLOR") && (screenY == 50 || screenY == 150 || screenY == 220))
        {
            NES_PPU::Color c = DecodeBackgroundTileFresh(0, attrLeft[static_cast<size_t>(localTileRow * 32)]).GetPixel(0, 0);
            std::cerr << "[tile0color] screenY=" << screenY << " R=" << (int)c.R << " G=" << (int)c.G
                      << " B=" << (int)c.B << " A=" << (int)c.A
                      << " PPUCTRL.B()=" << NES_PPU_Register::PPUCTRL.B() << std::endl;
        }

        // X wraparound into the doubled 0..511 logical space, same
        // thresholds DrawBackground() already used (see its own comment) -
        // only reproduced along X here since Y-wraparound was already
        // resolved above by picking the right source tile-row directly.
        //
        // Deliberately DrawImage() (additive blend, respects alpha) here,
        // *not* DrawNewImage() (plain overwrite): a background tile's
        // palette index 0 resolves to Color::Transparent() (A=0,
        // R=255,G=255,B=255 - see NES_PPU_Palette), matching how real
        // hardware always treats index 0 as "show the
        // universal background color", not literal white. DrawBackground()
        // relied on that same additive blend to let its (freshly black,
        // never-yet-painted) `frame` show through wherever an incoming
        // pixel's alpha is 0 (`add()`'s "A=0 degenerates to the *existing*
        // pixel's channel" rule - see Picture.cpp). Found live as a real
        // regression: an earlier version of this function used
        // DrawNewImage() here instead, which ignores alpha entirely and
        // overwrote every "transparent" background pixel with literal
        // white - only safe because `backgroundBuffer` is now reset to a
        // fresh, all-black Picture at the start of every frame (see
        // OnScanlineStart()'s `scanline == 0` branch) specifically so this
        // blend has a correct, non-stale black baseline to blend onto,
        // rather than last frame's leftover pixels.
        // FIXED (same copy-paste bug as DrawDisplayFrame()'s own X-axis
        // check, see its FIXED note in NES_PPU.NameTable.cpp for the full
        // derivation and why this one, too, turns out to be a harmless
        // no-op in practice: 512-256=256 is the real threshold this axis
        // needs, not 240 (Y's threshold, wrongly copied here) - kept for
        // correctness even though the wraparound copy's own offset
        // (`xScroll - 512`) already falls entirely outside logicalRow's
        // bounds for every xScroll value where the two thresholds disagree).
        backgroundBuffer.DrawImage(logicalRow, Rect{0, screenY, 256, 1}, Rect{xScroll, pixelRowWithinTile, 256, 1});
        if (xScroll > 256)
            backgroundBuffer.DrawImage(logicalRow, Rect{0, screenY, 256, 1},
                                        Rect{xScroll - (256 * 2), pixelRowWithinTile, 256, 1});
        if (xScroll < 0)
            backgroundBuffer.DrawImage(logicalRow, Rect{0, screenY, 256, 1},
                                        Rect{(256 * 2) - xScroll, pixelRowWithinTile, 256, 1});
    }

    // FIXED (new design - the direct fix for the sprite half
    // of the scanline-accurate PPU redesign, replacing the old
    // InsertObect(), deleted): renders exactly the sprites that cover one
    // real on-screen scanline into spriteBehindBuffer/spriteFrontBuffer
    // (picked per sprite via its OAM priority bit), sampling OAM state
    // *fresh* at the moment this specific scanline is needed - not from a
    // single once-per-frame snapshot the way InsertObect() built the whole
    // sprite layer. Also reproduces real hardware's "at most 8 sprites per
    // scanline" limit (http://wiki.nesdev.com/w/index.php/PPU_OAM
    // "Sprite evaluation") for the first time - InsertObect() drew every
    // OAM-matching sprite unconditionally, regardless of how many shared a
    // scanline.
    //
    // Reuses DecodeSpriteTileFresh() (not the cached Tile()) for the same
    // reason RenderBackgroundScanline() does - see its own comment.
    //
    // Per-sprite decode/flip logic (bank/index selection in 8x8 vs 8x16
    // mode, flip handling) is unchanged from InsertObect() - see its own
    // FIXED note, preserved below verbatim - only *what* the decoded
    // tile(s) get drawn onto changed: a small local spriteCanvas instead of
    // directly onto the frame, so that exactly one real on-screen row can
    // be sliced out of it and blitted into the right buffer at the right
    // scanline, instead of drawing the whole sprite at once.
    void NES_PPU::RenderSpriteScanline(int screenY)
    {
        if (!NES_PPU_Register::PPUMASK.s())
            return;

        int spriteHeight = NES_PPU_Register::PPUCTRL.H() ? 16 : 8;
        int spritesOnThisLine = 0;

        for (size_t i = 0; i < NES_PPU_OAM::SpriteTile.size() && spritesOnThisLine < 8; i++)
        {
            try
            {
                const auto& SpriteYc = NES_PPU_OAM::SpriteYc[i];
                // See the FIXED note this function inherits from InsertObect()
                // below for why "+1" (real Y recovered from OAM's stored Y-1).
                int spriteTopY = SpriteYc->Value() + 1;
                int rowWithinSprite = screenY - spriteTopY;
                if (rowWithinSprite < 0 || rowWithinSprite >= spriteHeight)
                    continue;
                ++spritesOnThisLine;

                const NES_PPU_OAM::Byte2& Attribute = NES_PPU_OAM::SpriteAttribute[i];
                const NES_PPU_OAM::Byte1& SpriteTile = NES_PPU_OAM::SpriteTile[i];
                const auto& SpriteXc = NES_PPU_OAM::SpriteXc[i];

                Picture spriteCanvas(8, spriteHeight);

                // FIXED (this is the
                // "eine Halfte ist richtig, andere falsch" sprite-corruption
                // bug): OAM byte 1 means two different things depending on
                // PPUCTRL's sprite-size bit H ($2000 bit 5):
                //  - 8x8 mode (H=0): the FULL byte is the tile index within
                //    whichever pattern table PPUCTRL's S bit ($2000 bit 3)
                //    selects for *all* sprites - see
                //    http://wiki.nesdev.com/w/index.php/PPU_registers ("Sprite
                //    pattern table address for 8x8 sprites").
                //  - 8x16 mode (H=1): bit 0 selects the bank *per sprite* and
                //    bits 7-1 give the top subtile's index (bottom = index+1) -
                //    see http://wiki.nesdev.com/w/index.php/PPU_OAM ("Byte 1").
                // NES_PPU_OAM::Byte1::Number()/Bank() always apply the *8x16*
                // interpretation - masking off bit 0 as a per-sprite bank
                // selector - regardless of PPUCTRL.H(). PPUCTRL.S() and PPUCTRL.H() were declared but
                // never read anywhere in the project. In the
                // far more common 8x8 mode this corrupts roughly half of all
                // sprites: an even tile index by chance still points at the
                // right graphic, but an odd one silently loses its low bit
                // (drawn as index-1) and gets routed to a bank chosen by that
                // same stray bit instead of the game's actual PPUCTRL.S()
                // setting - exactly a "half look right, half look wrong"
                // pattern. Fixed here by branching on PPUCTRL.H():
                if (!NES_PPU_Register::PPUCTRL.H())
                {
                    // 8x8 mode: full byte as tile index, bank from PPUCTRL.S().
                    uint16_t tileIndex = SpriteTile.adress->Value();
                    int bank = NES_PPU_Register::PPUCTRL.S() ? 1 : 0;
                    Picture tile = DecodeSpriteTileFresh(tileIndex, Attribute.Palette(), bank);

                    if (Attribute.FlipH())
                        tile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                    if (Attribute.FlipV())
                        tile.RotateFlip(RotateFlipType::RotateNoneFlipY);

                    spriteCanvas.DrawNewImage(tile, 0, 0);
                }
                else
                {
                    // 8x16 mode: bit 0 of the byte is the bank, bits 7-1 give
                    // the top subtile's index (bottom subtile = index+1),
                    // both subtiles drawn 8px apart. Per
                    // http://wiki.nesdev.com/w/index.php/PPU_OAM ("Sprite
                    // size"): "vertical flip flips each of the subtiles and
                    // also exchanges their position; the odd-numbered tile of
                    // a vertically flipped sprite is drawn on top."
                    int bank = SpriteTile.Bank() ? 1 : 0;
                    uint16_t topIndex = SpriteTile.Number();
                    uint16_t bottomIndex = static_cast<uint16_t>(topIndex + 1);

                    Picture topTile = DecodeSpriteTileFresh(topIndex, Attribute.Palette(), bank);
                    Picture bottomTile = DecodeSpriteTileFresh(bottomIndex, Attribute.Palette(), bank);

                    if (Attribute.FlipH())
                    {
                        topTile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                        bottomTile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                    }
                    if (Attribute.FlipV())
                    {
                        topTile.RotateFlip(RotateFlipType::RotateNoneFlipY);
                        bottomTile.RotateFlip(RotateFlipType::RotateNoneFlipY);
                        std::swap(topTile, bottomTile);
                    }

                    spriteCanvas.DrawNewImage(topTile, 0, 0);
                    spriteCanvas.DrawNewImage(bottomTile, 0, 8);
                }

                // FIXED (real bug, found live via Tiny Toon Adventures: the
                // status bar/HUD - lives, character icon, score, timer -
                // never appeared at all; a user-provided screenshot showed
                // the nametable rows it should occupy sitting at all-zero,
                // meaning the game's own HUD-drawing code never ran):
                // SpriteZeroHit() (deleted, see its own removed comment)
                // only ever ran *once per whole frame*, from Display(),
                // *after* every one of that frame's 240 scanlines had
                // already been rendered by RenderBackgroundScanline()/this
                // function - a leftover from before this port's
                // scanline-accurate PPU redesign, never updated to match
                // it. Per http://wiki.nesdev.com/w/index.php/PPU_OAM
                // ("Sprite zero hits"), games commonly poll PPUSTATUS's S
                // bit with a tight `BIT $2002 / BVC loop` as a *mid-frame*
                // synchronization point - e.g. exactly the "wait for the
                // playfield to finish, then write fresh status-bar tile
                // data into the nametable rows about to scroll into view"
                // sequence a HUD needs. With S only ever becoming visible
                // to the CPU after the *entire* frame was already built,
                // such a wait loop could never see it at the real-hardware
                // moment the game expects - so the HUD-drawing code that
                // waits on it, gated behind an interrupt/poll that
                // (from the game's perspective) never resolves in time,
                // was never reached. Fixed by checking sprite 0's hit
                // per-scanline instead, at the same granularity every other
                // part of this redesign already uses: as soon as this
                // scanline's slice of sprite 0 is decoded, check it against
                // this exact row of backgroundBuffer (already rendered for
                // this scanline by RenderBackgroundScanline() above, called
                // first) and set S() the moment a real overlap is found -
                // not fully dot-exact (same documented scope limit as
                // everywhere else in this redesign), but now landing on the
                // right *scanline*, which is what a polling loop like this
                // actually needs.
                if (i == 0 && !NES_PPU_Register::PPUSTATUS.S() && NES_PPU_Register::PPUMASK.b())
                {
                    int spriteScreenX = SpriteXc->Value();
                    for (int sx = 0; sx < 8; sx++)
                    {
                        int screenX = spriteScreenX + sx;
                        if (screenX < 0 || screenX >= 256)
                            continue;
                        if (spriteCanvas.GetPixel(sx, rowWithinSprite).A != 0 &&
                            backgroundBuffer.GetPixel(screenX, screenY).A != 0)
                        {
                            NES_PPU_Register::PPUSTATUS.S(true);
                            // TEMPORARY diagnostic aid - opt-in via
                            // NES_TRACE_2002POLL, off by default (same
                            // switch as INITPPUSTATUS()'s $2002-read trace).
                            if (std::getenv("NES_TRACE_2002POLL"))
                                std::cerr << "[sprite0hit] screenY=" << screenY << " spriteX=" << spriteScreenX
                                          << std::endl;
                            break;
                        }
                    }
                }

                // See the FIXED note above for why "+0, +1" (X has no
                // documented offset; Y is recovered via spriteTopY, already
                // "+1"-adjusted above).
                Picture& targetBuffer = Attribute.Priority() ? spriteBehindBuffer : spriteFrontBuffer;
                targetBuffer.DrawImage(spriteCanvas, Rect{SpriteXc->Value(), screenY, 8, 1},
                                        Rect{0, rowWithinSprite, 8, 1});
            }
            catch (...)
            {
            }
        }
    }

    // New: user-requested debug tool (see NES/main.cpp's
    // "O = OAM Viewer" key), built while investigating whether a game's
    // status-bar HUD digits (Tiny Toon Adventures/Bram Stoker's Dracula)
    // come from the background nametable or from sprites parked below the
    // visible screen and moved on-screen only when needed. Draws *every*
    // one of OAM's 64 sprite slots at its real, raw OAM (X, Y) - including
    // ones with Y>=240 (per http://wiki.nesdev.com/w/index.php/PPU_OAM
    // "Sprite Y coordinate": "usual placement is Y' = 255 or greater to be
    // sure it's offscreen"), which real hardware never renders but this
    // view deliberately does anyway, on a canvas taller than the real
    // 256x240 screen so those parked-offscreen sprites are visible instead
    // of silently clipped - a red line marks the real screen's bottom edge
    // (scanline 240, where vblank begins) so it's immediately visible which
    // sprites are actually on-screen right now versus waiting in the
    // off-screen "parking" area a game moves them out of on demand. Unlike
    // RenderSpriteScanline() above (which this reuses the tile-decode/flip
    // logic from), this ignores the real 8-sprites-per-scanline hardware
    // limit and OAM priority/transparency ordering entirely - it's a raw
    // memory dump rendered as a picture, not a faithful re-render of what
    // the PPU would actually output.
    NES_PPU::Picture NES_PPU::OAMDebugOverlay()
    {
        constexpr int kCanvasHeight = 280; // 256 (real Y range) + 16 (tallest sprite) + margin
        Picture canvas(256, kCanvasHeight);
        canvas.DrawRectangle(Color::Red(), 0, 240, 256, 1);

        int spriteHeight = NES_PPU_Register::PPUCTRL.H() ? 16 : 8;
        for (size_t i = 0; i < NES_PPU_OAM::SpriteTile.size(); i++)
        {
            try
            {
                const auto& SpriteYc = NES_PPU_OAM::SpriteYc[i];
                const auto& SpriteXc = NES_PPU_OAM::SpriteXc[i];
                const NES_PPU_OAM::Byte2& Attribute = NES_PPU_OAM::SpriteAttribute[i];
                const NES_PPU_OAM::Byte1& SpriteTile = NES_PPU_OAM::SpriteTile[i];
                int spriteTopY = SpriteYc->Value() + 1; // see RenderSpriteScanline()'s own FIXED note

                Picture spriteCanvas(8, spriteHeight);
                if (!NES_PPU_Register::PPUCTRL.H())
                {
                    uint16_t tileIndex = SpriteTile.adress->Value();
                    int bank = NES_PPU_Register::PPUCTRL.S() ? 1 : 0;
                    Picture tile = DecodeSpriteTileFresh(tileIndex, Attribute.Palette(), bank);
                    if (Attribute.FlipH())
                        tile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                    if (Attribute.FlipV())
                        tile.RotateFlip(RotateFlipType::RotateNoneFlipY);
                    spriteCanvas.DrawNewImage(tile, 0, 0);
                }
                else
                {
                    int bank = SpriteTile.Bank() ? 1 : 0;
                    uint16_t topIndex = SpriteTile.Number();
                    uint16_t bottomIndex = static_cast<uint16_t>(topIndex + 1);
                    Picture topTile = DecodeSpriteTileFresh(topIndex, Attribute.Palette(), bank);
                    Picture bottomTile = DecodeSpriteTileFresh(bottomIndex, Attribute.Palette(), bank);
                    if (Attribute.FlipH())
                    {
                        topTile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                        bottomTile.RotateFlip(RotateFlipType::RotateNoneFlipX);
                    }
                    if (Attribute.FlipV())
                    {
                        topTile.RotateFlip(RotateFlipType::RotateNoneFlipY);
                        bottomTile.RotateFlip(RotateFlipType::RotateNoneFlipY);
                        std::swap(topTile, bottomTile);
                    }
                    spriteCanvas.DrawNewImage(topTile, 0, 0);
                    spriteCanvas.DrawNewImage(bottomTile, 0, 8);
                }

                if (spriteTopY >= 0 && spriteTopY < kCanvasHeight)
                    canvas.DrawImage(spriteCanvas, SpriteXc->Value(), spriteTopY);
            }
            catch (...)
            {
            }
        }
        return canvas;
    }
}
