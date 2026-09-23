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
#include "NES_PPU_AttributeTable.h"
#include "INES.h"
#include <cstdio>
#include <cstdlib>

namespace NES
{
    NES_PPU::Picture NES_PPU::TempNameTable(64 * 8, 60 * 8);

    // FIXED (found via real
    // interactive play, "Mario shows but there's no background at all",
    // confirmed with NES_TRACE_PC catching Super Mario Bros mid-frame with
    // PPUMASK.b=1 (background rendering genuinely enabled) but
    // PPUCTRL.V=0): this had the exact same "gate real rendering on the
    // NMI-enable bit" bug Display() itself was fixed for earlier this
    // session (see Display()'s own FIXED note on PPUSTATUS.V() and frame
    // composition) - except this one was *inside* NameTabele(), one level
    // deeper, so fixing Display()'s own gate alone wasn't enough:
    // Display()'s `DrawBackground(frame, NameTabele())` calls straight into
    // this function, which had its *own*, separate `PPUCTRL.V()` check
    // still gating whether the background bitmap ever got rebuilt.
    // InsertObect() (sprites) has no such gate, so whenever PPUCTRL.V()
    // happened to read false at the moment Display() ran - completely
    // normal, e.g. Super Mario Bros. toggles NMI off and on again as part
    // of its own logic - sprites (Mario, enemies, coins) kept rendering
    // normally while the background silently fell back to returning
    // whatever stale `TempNameTable` was left over (all-black if this is
    // the very first frame, since it's never populated before the first
    // successful rebuild). Per http://wiki.nesdev.com/w/index.php/PPU_rendering,
    // background composition depends only on PPUMASK's rendering bits, same
    // as Display() - not on whether NMI happens to be enabled. Fixed by
    // dropping the PPUCTRL.V() check the same way Display() already was;
    // `display` is now the only thing gating a rebuild (every real call
    // site passes its default `true` - see NES_PPU.h).
    NES_PPU::Picture NES_PPU::NameTabele(bool display)
    {
        if (display)
        {
            Picture bitmap(TempNameTable.Width(), TempNameTable.Height());

            for (int i = 0; i < 4; i++)
            {
                switch (i)
                {
                    case 0:
                        DrowOneNameTable(bitmap, NES_PPU_AttributeTable::AttributeTable(0), 0, 0, 0);
                        break;
                    // FIXED (was a preserved bug, now corrected - the same
                    // vertical/horizontal axis mix-up already fixed in
                    // RewireNameTableMirroring(), independently present
                    // here too: found live via real gameplay, "the Name
                    // Table debug window still shows all 4 quadrants
                    // identical (A=B=C=D) even after that other fix").
                    // Per https://www.nesdev.org/wiki/Mirroring, *vertical*
                    // mirroring pairs $2000/$2800 (slots 0/2, left column -
                    // top mirrors bottom) and $2400/$2C00 (slots 1/3, right
                    // column) - slot 1 (top-right) is real, independent
                    // data under vertical mirroring, not a duplicate of
                    // slot 0, so it must still be drawn; it's slot 2
                    // (bottom-left) that's redundant (mirrors slot 0) and
                    // can be skipped. *Horizontal* mirroring is the mirror
                    // image of that: slot 1 is redundant (mirrors slot 0),
                    // slot 2 is real, independent data. This code had the
                    // two conditions swapped, skipping the wrong slot in
                    // each mode - see DrawMirror()'s own FIXED note for the
                    // matching fix to which half actually gets mirror-
                    // filled in for the slot each case skips.
                    case 1:
                        if (INES::arrangement != INES::Mirror::horisontal)
                            DrowOneNameTable(bitmap, NES_PPU_AttributeTable::AttributeTable(1), 1, 0, 32);
                        break;
                    case 2:
                        if (INES::arrangement != INES::Mirror::vertical)
                            DrowOneNameTable(bitmap, NES_PPU_AttributeTable::AttributeTable(2), 2, 30, 0);
                        break;
                    default:
                        if (INES::arrangement == INES::Mirror::four_screen)
                            DrowOneNameTable(bitmap, NES_PPU_AttributeTable::AttributeTable(3), 3, 30, 32);
                        break;
                }
            }

            DrawMirror(bitmap);
            TempNameTable = bitmap;
        }
        return TempNameTable;
    }

    // FIXED (the "solid black
    // screen except for a thin red/green border" corruption found while
    // testing real commercial ROMs, e.g. Contra/UxROM, once the PPUCTRL.V()
    // gating bug in Display() was fixed and frames actually started
    // compositing): this used to call `DrawDisplayFrame(bitmap)` (the red
    // on-screen-viewport marker) and `bitmap.DrawInfoRectangle(Color::Green(), ...)`
    // (the green table-boundary marker) directly on `bitmap` before
    // assigning it to the shared `TempNameTable` above - the *exact* Picture
    // `Display()` then samples via `DrawBackground(frame, NameTabele())` to
    // build every real on-screen frame. Picture::GetPixel gives its info
    // layer (which is what DrawInfoRectangle writes to) priority over the
    // real image layer - see Picture.cpp's own FIXED note on a closely
    // related bug (the mirror-echo of this same info layer) - so these two
    // purely-cosmetic debug markers, meant only for the NameTable debug
    // window, silently overwrote real background pixels on every single
    // frame, at every position they geometrically fell on (most visibly the
    // frame edges when XScroll()/YScroll() are near 0, i.e. right at boot -
    // exactly where Contra's black screen showed a persistent red/green
    // outline instead of solid black). nesdev has no concept of these
    // markers at all; they exist purely for this port's own debug tooling
    // (see NameTable.cpp's other FIXED notes on this exact overlay). Fixed
    // by never baking them into `TempNameTable` itself; NameTabeleDebugOverlay()
    // below now draws them on a throwaway copy instead, used only by the
    // debug NameTable window (NES/main.cpp), leaving the bitmap Display()
    // renders from clean.
    //
    // FIXED (real regression from the scanline-accurate PPU redesign,
    // reported live: "Name Table window is black, shows only red/green"):
    // before that redesign, `Display()` called `DrawBackground(frame,
    // NameTabele())` unconditionally every single frame, so `NameTabele(true)`
    // - the only thing that ever repopulates `TempNameTable` - ran as a side
    // effect of just rendering the game, whether or not the debug window was
    // even open. RenderBackgroundScanline() (the new per-scanline renderer)
    // never calls NameTabele() at all - it reads NES_PPU_Memory/the
    // attribute table directly - so once that call site was removed,
    // `TempNameTable` was left permanently at its power-on blank-black
    // value; this function then dutifully overlaid the red viewport and
    // green boundary markers onto that stale, never-updated bitmap every
    // time the debug window redrew (only the markers themselves ever
    // changed, since those are drawn fresh onto a throwaway copy each
    // call). Fixed by calling `NameTabele(true)` here explicitly, so
    // opening the debug window forces its own live rebuild on demand -
    // matching this function's own doc comment on NES_PPU.h, which already
    // said the debug window is the *only* real caller of NameTabele()
    // post-redesign.
    // UPDATE (per explicit user correction, reconstructing intent from an
    // earlier session this context doesn't retain the details of): the
    // green marker used to outline only *half* of the 64x60-tile canvas,
    // not the whole thing - deliberately, to show which half is real,
    // unique nametable data and which half is a hardware *mirror* (a
    // literal pixel-for-pixel duplicate of the other half - see
    // DrawMirror()/INES::arrangement just above, and
    // http://wiki.nesdev.com/w/index.php/Mirroring: with only 2KB of
    // nametable VRAM, vertical mirroring makes the right half of this 2x2
    // grid a hardware-level copy of the left half, horizontal mirroring
    // makes the bottom half a copy of the top half). A later change (this
    // project's own, not the user's) replaced that half-outline with one
    // covering the entire canvas, reasoning "showing the whole window is
    // clearer" without addressing whatever the half-outline had actually
    // been diagnosing at the time. Restored here to again outline only the
    // non-mirrored (real) half for vertical/horizontal mirroring; four-
    // screen mode has no duplicated half (all 4 quadrants are independent
    // VRAM), so the outline covers the whole canvas there, same as
    // single-screen mode (already the *entire* nametable is one mirrored
    // quadrant, so there is no "duplicate half" distinction to draw).
    NES_PPU::Picture NES_PPU::NameTabeleDebugOverlay()
    {
        Picture bitmap = NameTabele(true);
        DrawDisplayFrame(bitmap);

        int w = TempNameTable.Width();
        int h = TempNameTable.Height();
        // RewireNameTableMirroring()'s own bankForSlot table
        // (NES_PPU_Memory.cpp) is the ground truth for which physical bank
        // each quadrant shows, and is itself the citation for this: per
        // https://www.nesdev.org/wiki/Mirroring, *vertical* mirroring pairs
        // $2000/$2800 (left column) and $2400/$2C00 (right column) - a
        // left/right split, needing a *vertical* dividing line (outlining
        // the left half: half width, full height); *horizontal* mirroring
        // pairs $2000/$2400 (top row) and $2800/$2C00 (bottom row) - a
        // top/bottom split, needing a *horizontal* dividing line (top half:
        // full width, half height). A brief regression here (this session,
        // now corrected) swapped these two cases' rectangles while trying
        // to chase down what turned out to be a *different* bug -
        // RewireNameTableMirroring()'s own bankForSlot table had the real
        // vertical/horizontal pairings swapped, not this code; once that
        // was fixed at the source, this overlay's original orientation
        // (restored here) was correct all along.
        if (INES::arrangement == INES::Mirror::vertical)
            bitmap.DrawInfoRectangle(Color::Green(), 0, 0, w / 2, h);
        else if (INES::arrangement == INES::Mirror::horisontal)
            bitmap.DrawInfoRectangle(Color::Green(), 0, 0, w, h / 2);
        else
            bitmap.DrawInfoRectangle(Color::Green(), 0, 0, w, h);

        return bitmap;
    }

    // FIXED (real copy-paste bug, found via code review while investigating
    // a user report of the debug Name Table window's viewport rectangle
    // "jumping entirely back to the start instead of driving in on one side
    // and out the other"): the 512x480 logical canvas (TempNameTable) holds
    // a 256x240 viewport, so a same-axis wraparound copy is needed once the
    // viewport's far edge would cross the canvas edge - i.e. past
    // `canvasSize - viewportSize` on that axis: 512-256=256 for X,
    // 480-240=240 for Y. The X-axis check here compared against 240 (Y's
    // own threshold) instead of 256 - a copy-paste of the Y condition below
    // it that never got its constant updated for X's different axis size.
    //
    // UPDATE: empirically verified (via a throwaway pixel-sweep test,
    // TestDrawDisplayFrameWraparoundIsContinuous() in cpu_check.cpp) that
    // this specific off-by-16 threshold is NOT what caused the reported
    // "jumps to start" symptom - the wraparound copy's own offset
    // (`XScroll() - 512`) is identical either way, and for every XScroll
    // value in the (240, 256] window where the two thresholds disagree,
    // that offset is already far enough negative to land entirely outside
    // the canvas, so firing the copy "early" was a harmless no-op both
    // before and after this fix. Kept anyway as the geometrically-intended
    // value. The real, confirmed cause of the reported jump: this port's
    // scroll model (NES_PPU.Scroll.cpp's AddxScroll()) has no equivalent of
    // real hardware's autonomous per-scanline coarse-X wrap
    // (https://www.nesdev.org/wiki/PPU_scrolling, "Coarse X increment": the
    // PPU itself toggles the nametable-select bit when coarse X wraps from
    // 31 to 0, independent of anything the CPU writes to PPUCTRL) - this
    // port only ever applies PPUCTRL's nametable-select bit at PPUSCROLL
    // write time. A live trace (Chip 'n Dale, both its own attract-mode
    // demo and a user-captured save state) caught the raw PPUSCROLL X byte
    // wrap 0xfe -> 0x00 with PPUCTRL's bit unchanged across the write,
    // which this model reads as XScroll() snapping 254 -> 0 directly
    // (skipping 255-511 entirely) instead of continuing smoothly into the
    // second nametable - exactly the "whole viewport snaps to the origin"
    // symptom reported. Not yet fixed - a real architectural gap (this port
    // has no loopy v/t-register model at all), left for a follow-up rather
    // than a hasty heuristic patched in here.
    void NES_PPU::DrawDisplayFrame(Picture& bitmap)
    {
        bitmap.DrawInfoRectangle(Color::Red(), XScroll(), YScroll(), 256, 240);

        if (XScroll() > 256)
            bitmap.DrawInfoRectangle(Color::Red(), XScroll() - (256 * 2), YScroll(), 256, 240);
        if (YScroll() > 240)
            bitmap.DrawInfoRectangle(Color::Red(), XScroll(), YScroll() - (240 * 2), 256, 240);
        if (XScroll() < 0)
            bitmap.DrawInfoRectangle(Color::Red(), (256 * 2) - XScroll(), YScroll(), 256, 240);
        if (YScroll() < 0)
            bitmap.DrawInfoRectangle(Color::Red(), XScroll(), (240 * 2) - YScroll(), 256, 240);
    }

    // FIXED (was a preserved bug, now corrected - see NameTabele()'s
    // matching FIXED note on case 1/2 for the full story): Picture::
    // DrawMirror(x, y) mirrors any pixel at or past `x` from `x - mirror.x`
    // (a left->right copy) and/or any pixel at or past `y` from
    // `y - mirror.y` (a top->bottom copy) - see Picture.cpp's GetPixel().
    // *Vertical* mirroring (slots 0/2 = left column identical, slots 1/3 =
    // right column identical - https://www.nesdev.org/wiki/Mirroring) needs
    // the *top->bottom* copy (DrawMirror(0, height/2)) to fill in the
    // skipped bottom row from the real top row within each column; only
    // *horizontal* mirroring (slots 0/1 = top row identical, slots 2/3 =
    // bottom row identical) needs the *left->right* copy
    // (DrawMirror(width/2, 0)). This function had the two swapped.
    void NES_PPU::DrawMirror(Picture& bitmap)
    {
        if (INES::arrangement == INES::Mirror::vertical)
            bitmap.DrawMirror(0, TempNameTable.Height() / 2);
        if (INES::arrangement == INES::Mirror::horisontal)
            bitmap.DrawMirror(TempNameTable.Width() / 2, 0);
    }

    void NES_PPU::DrowOneNameTable(Picture& image, const std::vector<int>& Attribute, int Nr, uint16_t X, uint16_t Y)
    {
        // FIXED: this used to do
        // `Parallel.For(X, X + 30 - 1, ...)`, a toExclusive of X+29, so the
        // loop variable only ever reached X+28 - one row short of the 30
        // tile rows a name table actually has
        // (http://wiki.nesdev.com/w/index.php/PPU_nametables: "each nametable
        // ... is 30 rows of 32 tiles each"). The bottom tile row of every
        // name-table quadrant was therefore never drawn (left as whatever
        // the destination Picture already had). This bitmap isn't just the
        // debug nametable viewer - Display() also samples it directly via
        // DrawBackground(frame, NameTabele()), so this bug cut a real row of
        // background tiles from the bottom of every on-screen frame too.
        // Fixed to `X + 30`, covering i in [X, X+29].
        //
        // FIXED (real regression, reported live: "the black spots in the
        // Name Table window are white" - comparing the debug window
        // side-by-side against the actual game window at the same scroll
        // position): this used to place each decoded tile with
        // DrawNewImage() (plain overwrite via SetPixel(), alpha-blind - see
        // Picture.cpp). A background tile's palette index 0 always decodes
        // to Color::Transparent() (A=0, but R=255,G=255,B=255 - see
        // NES_PPU_Palette::getBGColorPalette()), matching how real hardware
        // treats index 0 as "show the universal background color" rather
        // than an actual opaque white pixel. RenderBackgroundScanline()
        // (the real, on-screen renderer) already blends this correctly via
        // DrawImage() (alpha-aware - see its own comment) onto a
        // freshly-black canvas, so index-0 pixels there correctly resolve
        // to black/the background color. This debug-only path never got
        // that same fix, so it painted literal opaque white everywhere a
        // real frame would show black - exactly the mismatch reported.
        // Fixed the same way: DrawImage() (blend) instead of DrawNewImage()
        // (overwrite); `image` starts as a freshly-constructed, all-black
        // Picture every call (see NameTabele() above), giving this blend
        // the same non-stale black baseline RenderBackgroundScanline()'s
        // backgroundBuffer relies on.
        //
        // FIXED (real regression, reported live via a real ROM - Chip and
        // Dale - once the two bugs above were already fixed: the debug
        // window still showed entirely black tiles, even though the exact
        // same tile IDs rendered with full color in the real game window):
        // this used to decode tiles via the cached `Tile()`/
        // `CreateTileBitmap()` path (`patternArray`) - the exact same
        // once-per-*frame* cache whose staleness bug motivated
        // DecodeBackgroundTileFresh() in the first place (see
        // NES_PPU.Tile.cpp's own comment on it for the full mechanism). This
        // debug-only path never got that fix: with NameTabeleDebugOverlay()
        // now gated to only run while the debug window is open (see
        // NES_Console::RenderFrame()'s own comment), the *first* decode of
        // a given (tile, palette) pair here can just as easily land on a
        // frame before the game's CHR-bank switch (MMC1 games commonly
        // swap CHR banks between a menu/dialogue screen and real gameplay,
        // e.g. Chip and Dale) - permanently caching the wrong graphic for
        // the lifetime of that cache entry, same failure mode as the
        // original bug. Fixed by switching to DecodeBackgroundTileFresh()
        // (bypasses patternArray, only cached *within* one real frame - see
        // ClearFreshTileCaches()), matching what RenderBackgroundScanline()
        // already does for exactly this reason.
        for (int i = X; i < X + 30; i++)
        {
            for (int j = Y; j < Y + 32; j++)
            {
                int k = K(X, Y, i, j);
                int c = Attribute[static_cast<size_t>(k)];
                int t = NES_PPU_Memory::NameTableN[static_cast<size_t>(Nr)][static_cast<size_t>(k)]->Value();
                Picture temp = DecodeBackgroundTileFresh(static_cast<uint16_t>(t), c);
                image.DrawImage(temp, j * 8, i * 8);
            }
        }
    }

    int NES_PPU::K(uint16_t X, uint16_t Y, int i, int j)
    {
        return ((i - X) * 32) + (j - Y);
    }
}
