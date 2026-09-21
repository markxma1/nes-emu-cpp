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
#include "Picture.h"
#include "Color.h"
#include "BitmapWithInfo.h"
#include "NES_PPU_Color.h"
#include "NES_PPU_Memory.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace NES
{
    /// @brief The PPU's rendering pipeline: turns PPU memory (pattern/name/
    /// attribute tables, OAM, palettes) into displayable NES_PPU::Picture
    /// framebuffers. Port of the NES_PPU/NES_PPU_Folder C# partial class,
    /// combining NES_PPU.cs, .Display.cs, .Tile.cs, .NameTable.cs and
    /// .Scroll.cs into this one header (C++ has no partial classes).
    /// http://wiki.nesdev.com/w/index.php/PPU
    ///
    /// Naming note: this class (`NES::NES_PPU`) and the `::NES_PPU` namespace
    /// holding the graphics primitives (Picture, Color, ...) share the name
    /// NES_PPU, exactly like the C# original's class `NES.NES_PPU` coexisted
    /// with its `using NES_PPU;` (the ClassLibrary1 namespace) - the alias
    /// members below (`Picture`, `Color`, ...) are what let callers elsewhere
    /// in this port keep writing the unqualified `NES_PPU::Picture` etc. they
    /// already do (e.g. NES_Console.cpp) and have it still resolve to the
    /// graphics types rather than this class.
    class NES_PPU
    {
    public:
        using Picture = ::NES_PPU::Picture;
        using Color = ::NES_PPU::Color;
        using Rect = ::NES_PPU::Rect;
        using RotateFlipType = ::NES_PPU::RotateFlipType;

        // --- NES_PPU.cs ---
        NES_PPU();

        static Picture PaletteTable();

        // --- NES_PPU.Tile.cs ---

        /// Set from the debug pattern-table/name-table viewers (deferred UI
        /// tooling) to outline freshly-rendered tiles; false in the core-only
        /// build for now, kept as a real public field to match the C# shape.
        static bool DrawRefresh;

        /// Converts a tile at a raw pattern-table byte offset to a Picture.
        static Picture Tile_StartAdress(int startAdress, int pallete);
        /// Converts a background tile by sprite/tile ID, using PPUCTRL's
        /// background pattern-table-select bit.
        static Picture Tile(uint16_t spriteID, int pallete);
        /// Converts an OAM sprite tile by ID, explicit palette and bank.
        static Picture Tile(uint16_t spriteID, int pallete, int bankID);

        /// NEW, no C# equivalent - see the .cpp definition's own comment:
        /// same pixel math as the 2-arg Tile() above (background tile,
        /// PPUCTRL.B()-selected bank), but *never* goes through
        /// patternArray's cross-call bitmap cache - used by
        /// RenderBackgroundScanline() specifically because that cache's
        /// "decode once per frame is enough" assumption is false once a
        /// frame is built incrementally, one scanline at a time, during
        /// the same real time window the CPU may still be writing palette
        /// data.
        static Picture DecodeBackgroundTileFresh(uint16_t spriteID, int pallete);

        /// NEW, no C# equivalent - same reasoning as DecodeBackgroundTileFresh()
        /// above, for sprite tiles (explicit palette and bank, same as the
        /// 3-arg Tile()) - used by RenderSpriteScanline().
        static Picture DecodeSpriteTileFresh(uint16_t spriteID, int pallete, int bankID);

        /// NEW, no C# equivalent - see NES_PPU.Tile.cpp's own comment for
        /// the full story: bounds DecodeBackgroundTileFresh()/
        /// DecodeSpriteTileFresh()'s per-frame tile-bitmap cache to exactly
        /// one frame's lifetime. Called from OnScanlineStart()'s
        /// `scanline == 0` branch, alongside the backgroundBuffer/sprite
        /// buffer resets. Public (rather than a private implementation
        /// detail) since it's directly exercised by
        /// tests/cpu/cpu_check.cpp's regression test for this cache.
        static void ClearFreshTileCaches();

        /// Builds the pattern-table debug view (both 4KB banks side by side).
        static Picture PatternTable(int PN);

        // --- NES_PPU.NameTable.cs ---

        /// Renders all 4 (mirrored down to 1-4 as appropriate) name tables
        /// into one 64x60-tile bitmap. Misspelling ("Tabele") kept verbatim,
        /// matching the C# original and this port's other call sites.
        static Picture NameTabele(bool display = true);

        /// Debug-only: same bitmap as NameTabele(), but with the current
        /// on-screen viewport (red) and table-boundary (green) markers drawn
        /// on top of a throwaway copy - see NameTabele()'s own FIXED note on
        /// why these must never be baked into the shared bitmap
        /// NameTabele()/Display() actually renders real frames from.
        static Picture NameTabeleDebugOverlay();

        // --- NES_PPU.Display.cs ---

        /// Renders one full frame (background + sprites-behind + sprites-in-front).
        static Picture Display();

        /// NEW, no C# equivalent - renders one full frame from *whatever*
        /// live PPU/OAM/CHR/palette state is currently poked into memory,
        /// by directly looping RenderBackgroundScanline()/
        /// RenderSpriteScanline() over scanlines 0-239 and compositing via
        /// Display() - without running any CPU cycles or scanline-timed
        /// mapper IRQ/bank-switch hooks at all. Built for a debug tool that
        /// loads a saved per-frame RAM/VRAM/OAM/CHR snapshot (see
        /// NES/main.cpp's NES_RENDER_FROM_STATE) and wants a picture back
        /// in milliseconds instead of re-running the whole emulator from
        /// power-on to reach that frame - dramatically faster for scanning
        /// many frames, at a real, known cost: a mapper that changes CHR
        /// banks *mid-frame* (e.g. MMC3's scanline-IRQ-driven status-bar
        /// split - see Mapper_MMC3::OnScanline()) only has ONE CHR/palette
        /// state to render with here, so a frame that genuinely looks
        /// different in its top half vs. bottom half on real hardware will
        /// render with only one of those halves correct. Fine for the vast
        /// majority of frames (most have no mid-frame bank switch at all);
        /// for a frame suspected of one, a full real replay remains the
        /// only accurate source.
        static Picture RenderStaticSnapshot();

        /// Debug-only: renders every one of OAM's 64 sprites at its raw
        /// (X, Y) position, including ones parked off the bottom of the
        /// real screen (Y>=240) that real hardware never displays - see
        /// its own .cpp comment for why (a game's status-bar/HUD digits are
        /// sometimes sprites moved on-screen only when needed, not
        /// background tiles).
        static Picture OAMDebugOverlay();

        // --- NEW, no C# equivalent: real scanline/dot clock ---
        // See NES_PPU.Display.cpp's AdvanceDots()/OnScanlineStart() for the
        // full story - this is the fix for this port having no independent
        // PPU timing at all (found via three separate real-game bugs this
        // session: MMC1 shift-register corruption from too-frequent NMI,
        // MMC3 status-bar CHR-bank splits landing on the wrong scanline,
        // and nametable-streaming desync during horizontal scroll).

        /// Advances the PPU's clock by `cpuCycles * 3` dots (NTSC's fixed
        /// 3:1 PPU:CPU ratio - NTSC-only, matching NES_Console::INIT()'s
        /// hardcoded Mod::NTSC default), crossing scanline boundaries as
        /// needed and firing the relevant per-scanline hooks. Returns true
        /// exactly once per completed 262-scanline frame (scanline wraps
        /// 261->0) - NES_CPU::Run() uses this to know when to publish a
        /// newly composed frame via NES_Console::RenderFrame().
        static bool AdvanceDots(int cpuCycles);

        /// Debug/test introspection only - see tests/cpu/cpu_check.cpp.
        static int CurrentScanline() { return currentScanline; }
        static int CurrentDot() { return currentDot; }

        /// NEW, no C# equivalent - registers the callback OnScanlineStart()
        /// invokes once per real *visible* scanline (0-239), i.e. the
        /// per-scanline equivalent of Mapper::OnScanline() (see its own
        /// comment). NES_PPU deliberately has no compile-time dependency on
        /// NES_ROM/Mapper at all (same reasoning NES_Console::getDisplay()'s
        /// own comment already documented for the old per-frame
        /// Mapper::OnFrame() hook) - NES_Console::INIT() registers a
        /// callback here once, at startup, that closes over
        /// NES_ROM::CurrentMapper() instead. A no-op std::function by
        /// default, so calling this is safe even before INIT() runs (e.g.
        /// from a test harness that never calls it).
        static void SetScanlineCallback(std::function<void()> callback) { scanlineCallback = std::move(callback); }

        // --- NES_PPU.Scroll.cs ---

        /// @brief PPUSCROLL ($2005) register emulation.
        ///
        /// On real hardware $2005 is a single write-only port shared with
        /// $2006's write toggle `w`: the first write after `w` is reset sets
        /// the X scroll, the second sets Y, and `w` then resets itself
        /// (`ScrollXoY` below is this port's stand-in for `w`). See
        /// http://wiki.nesdev.com/w/index.php/PPU_scrolling ("$2005 and
        /// $2006 share a common write toggle w").
        ///
        /// This class does not model the PPU's internal v/t/x/w scroll
        /// registers bit-for-bit; instead `xScroll`/`yScroll` are a logical
        /// scroll position across a *doubled* nametable space (0..511 for X,
        /// 0..479 for Y - see AddxScroll()/AddyScroll()), which DrawBackground()
        /// then blits from with wraparound. Reads always return 0, matching
        /// the C# original (PPUSCROLL is write-only on real hardware, so a
        /// CPU read of $2005 isn't meaningful here either).
        static uint8_t Scroll();
        static void Scroll(uint8_t v);

        static int xScroll;
        static int yScroll;

        /// Raw, pre-AddxScroll()/AddyScroll() scroll value last written via
        /// $2005 (X: 0-255; Y: -16..239, see Scroll(uint8_t)'s own comment on
        /// the attribute-glitch range) - kept so RecomputeXScroll()/
        /// RecomputeYScroll() can re-derive xScroll/yScroll if PPUCTRL's
        /// nametable-select bits change *after* this write, without needing
        /// a fresh $2005 write to pick it up. See their own .cpp comment for
        /// why this exists.
        static int rawXScroll;
        static int rawYScroll;
        /// Re-derives xScroll/yScroll from rawXScroll/rawYScroll and
        /// PPUCTRL's *current* nametable-select bits - called from
        /// NES_PPU_Register's $2000 write hook (INITPPUCTRL()) so a same-
        /// frame PPUCTRL write that arrives *after* the $2005 write it
        /// logically belongs with (see this port's own real-game trace,
        /// documented in NES_PPU.Scroll.cpp) still takes effect.
        static void RecomputeXScroll();
        static void RecomputeYScroll();

        /// PPUSCROLL/PPUADDR's shared internal write-toggle `w`: true = next
        /// $2005/$2006 write is the *first* of its pair (X for $2005, high
        /// byte for $2006), false = next is the second. Real hardware wires
        /// $2005 and $2006 to the exact same `w` flip-flop -
        /// http://wiki.nesdev.com/w/index.php/PPU_scrolling ("$2005 and
        /// $2006 share a common write toggle w") - so this port's own
        /// NES_PPU_Register.cpp INITPPUADDR() write handler shares this same
        /// flag rather than keeping a separate one for $2006. Public (like
        /// xScroll/yScroll above) so NES_PPU_Register.cpp's $2002 read
        /// handler can reset it directly - see that function's own FIXED
        /// note for why only *this* flag, and not xScroll/yScroll/PPUPCADDR
        /// themselves, may ever be touched by reading $2002.
        static bool ScrollXoY;

    private:
        // --- NES_PPU.cs ---
        static Picture TempPaletteTable;

        // --- NES_PPU.Tile.cs ---
        static Picture TempPatternTable;
        static std::unordered_map<int, BitmapWithInfo> patternArray;

        static int GetTileID(int startAdress, int pallete, const AddrVec& PatternTable);
        static Picture CreateTileBitmap(int startAdress, const NES_PPU_Color& color, const AddrVec& PatternTable, int ID);
        static BitmapWithInfo UpdateTile(int ID, const NES_PPU_Color& color);
        static Picture DrawRefreshFrame(const Picture& bitmap, Color pen, bool isNew = true);
        static BitmapWithInfo CreateNewTile(int startAdress, const NES_PPU_Color& color, const AddrVec& PatternTable);
        static bool isNew(int startAdress, const AddrVec& PatternTable, const NES_PPU_Color& color, int ID);
        static bool isNewPattern(int startAdress, const AddrVec& PatternTable, int ID);
        static void AddTileToPatternArray(int ID, const BitmapWithInfo& bitmap);

        // --- NES_PPU.NameTable.cs ---
        static Picture TempNameTable;

        static void DrawDisplayFrame(Picture& bitmap);
        static void DrawMirror(Picture& bitmap);
        static void DrowOneNameTable(Picture& image, const std::vector<int>& Attribute, int Nr, uint16_t X, uint16_t Y);
        static int K(uint16_t X, uint16_t Y, int i, int j);

        // --- NES_PPU.Display.cs ---
        static Picture TempDisplay;
        static bool draw;

        // --- NEW, no C# equivalent: real scanline/dot clock state, see
        // AdvanceDots()'s own comment above and in the .cpp.
        static int currentDot;
        static int currentScanline;
        static void OnScanlineStart(int scanline);
        static std::function<void()> scanlineCallback;

        // NEW, no C# equivalent - see AdvanceDots()'s own .cpp comment on
        // why the mapper scanline-IRQ clock now fires mid-scanline (dot 260)
        // instead of at OnScanlineStart()'s dot-0 boundary. Reset to false
        // every time currentScanline advances so the mid-scanline check
        // fires at most once per scanline.
        static bool scanlineIrqClockFired;

        // --- NEW, no C# equivalent: per-scanline background rendering, see
        // RenderBackgroundScanline()'s own .cpp comment (this is the fix
        // for Chip 'n Dale's nametable-streaming-during-scroll desync bug).
        // Replaces the old DrawBackground()/NameTabele() whole-frame-
        // snapshot path for the *real* rendered frame - NameTabele()/
        // DrawBackground() themselves are kept only for the debug Name
        // Table viewer window (NES/main.cpp's N key), which still wants a
        // "whole stable frame" view rather than per-scanline sampling.
        static Picture backgroundBuffer;

    public:
        /// NEW, no C# equivalent - renders exactly one real on-screen
        /// scanline of background into the persistent backgroundBuffer (see
        /// the .cpp's own comment). Public (like ClearFreshTileCaches()
        /// above) specifically so tests/cpu/cpu_check.cpp's PPUMASK
        /// background-enable regression test can drive a single scanline in
        /// isolation, without needing to replay a full AdvanceDots()
        /// 262-scanline sweep (whose currentScanline/xScroll/yScroll state
        /// is process-global and would make a test order-dependent on
        /// whatever earlier tests left behind).
        static void RenderBackgroundScanline(int screenY);

        /// Debug/test introspection only - same reasoning as
        /// RenderBackgroundScanline() above. Returns whatever color is
        /// currently sitting in the persistent background layer at (x, y).
        static Color BackgroundBufferPixel(int x, int y) { return backgroundBuffer.GetPixel(x, y); }

    private:

        // --- NEW, no C# equivalent: per-scanline sprite rendering, see
        // RenderSpriteScanline()'s own .cpp comment. Replaces the old
        // InsertObect() whole-frame snapshot path (deleted - see its own
        // git history/comment trail) for the *real* rendered frame; one
        // buffer per OAM priority bit, matching InsertObect(true/false)'s
        // old two calls.
        static Picture spriteBehindBuffer;
        static Picture spriteFrontBuffer;
        static void RenderSpriteScanline(int screenY);

        /// Private latch flag: while true (mid-Display()), XScroll/YScroll's
        /// getters keep returning the *previous* frame's latched scroll so a
        /// frame renders with a stable scroll value even if PPUSCROLL is
        /// written again mid-frame; only when it flips back to false do
        /// xScrollTemp/yScrollTemp pick up the new live xScroll/yScroll.
        /// UPDATE: only the debug Name Table viewer (NameTabele()/
        /// NameTabeleDebugOverlay(), which still wants a stable whole-frame
        /// view) still depends on this latch - the real rendered frame no
        /// longer does, see RenderBackgroundScanline()'s own comment on why
        /// it reads the live xScroll/yScroll instead.
        static bool Draw();
        static void Draw(bool v);

        // --- NES_PPU.Scroll.cs ---

        static int xScrollTemp;
        static int yScrollTemp;

        /// Latched X scroll used for actual rendering (see Draw()'s comment
        /// above: holds the *previous* frame's value while a frame is mid-render).
        static int XScroll();
        /// Sets the live X scroll and folds in PPUCTRL's X-nametable-select bit
        /// via AddxScroll(). See NES_PPU.Scroll.cpp for the C# original's
        /// discarded-return-value bug this fixes.
        static void XScroll(int v);
        /// Latched Y scroll, see XScroll()'s comment.
        static int YScroll();
        /// Sets the live Y scroll and folds in PPUCTRL's Y-nametable-select bit
        /// via AddyScroll().
        static void YScroll(int v);

        /// Adds 240 to `value` when PPUCTRL's bit 1 (Y nametable select) is
        /// set, i.e. it reproduces the real PPU's "9th bit of Y scroll lives
        /// in PPUCTRL bit 1" behavior by mapping it onto a 0..479 logical
        /// space (two 240-row nametables stacked) instead of a raw v-register
        /// bit. http://wiki.nesdev.com/w/index.php/PPU_scrolling
        /// ("Write the 9th bit of X and Y to bits 0 and 1 ... of PPUCTRL").
        static int AddyScroll(int value);
        /// Same as AddyScroll() but for PPUCTRL bit 0 (X nametable select),
        /// adding 256 for a 0..511 logical X space.
        static int AddxScroll(int value);
    };
}
