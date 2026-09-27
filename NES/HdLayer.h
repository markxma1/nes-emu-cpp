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
/// @file HdLayer.h
/// @brief "Skins": replaces sprites and background tiles by higher-resolution pictures at display time.
///
/// This layer sits *outside* the emulator core. It only reads what the core already exposes
/// (OAM, pattern tables, name tables, scroll, palettes and the finished 256x240 frame) and never
/// changes what the game or the PPU do. Nothing is written back into the emulated NES.
///
/// How it works, in short (details in EFFECTS.md):
///  1. At the end of every frame (CPU thread, OnFrame()) the layer lists all 8x8 "cells" that make
///     up the picture: the tiles of every sprite (OAM) and the background tiles under the current
///     scroll. A cell is identified by a hash of its 16 pattern-table bytes.
///  2. Each cell is decoded again here and compared with the finished frame. Only pixels that match
///     (this tile really is what was drawn there) are treated as "visible"; this protects against
///     wrong guesses (CHR bank switched mid-frame, sprite hidden behind another, ...).
///  3. Compose() (UI thread) enlarges the frame by the HD scale and paints the skin picture of every
///     cell that has one over its visible pixels.
///
/// A skin pack is a folder `skins/<rom name>/tiles/` with one PNG (RGBA) per tile:
/// `<hash>.png` for every use of the tile, or `<hash>_s<palette>.png` / `<hash>_b<palette>.png` for
/// sprites (s) / background (b) drawn with one specific palette. The PNG is the tile in its
/// unflipped orientation; its size is any multiple of 8 (it is resized to the HD scale).
/// tools/nes_skin_editor.py creates and edits such packs.
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace NES
{
    /// One 8x8 piece of the picture (sprite tile or background tile) as it was drawn.
    struct HdCell
    {
        int x = 0, y = 0;          ///< screen position of the top-left pixel (may be partly off screen)
        bool sprite = false;       ///< true: sprite tile, false: background tile
        bool flipH = false, flipV = false;
        uint8_t palette = 0;       ///< palette number 0-3
        uint8_t bytes[16] = {};    ///< the tile's pattern bytes (plane 0 rows, then plane 1 rows)
        uint8_t rgb[4][3] = {};    ///< the four palette colours (index 0 of sprites is transparent)
        int visible = 0;           ///< how many of its pixels are really visible in the frame (0-64)
        uint64_t visibleMask = 0;  ///< bit (y*8+x) set: that screen pixel of the cell shows this tile
        uint64_t hash = 0;         ///< hash of `bytes`
    };

    /// A group of tiles that belong together (a 2x2 player ship, a big enemy) with ONE picture for the whole
    /// group. The picture may be larger than the group's silhouette (`overflow`). Stored as
    /// `objects/<name>.obj` (text) + `objects/<name>.png` in the skin pack:
    ///     layer s            (s = sprites, b = background)
    ///     size 16 16         (width and height of the group in NES pixels)
    ///     margin 0           (extra NES pixels of picture on every side, only used with overflow)
    ///     overflow 0         (1: paint the whole picture, also beyond the tiles' own pixels)
    ///     tile <dx> <dy> <hash> <flipH> <flipV> [<palette>]     (position relative to the group's top-left;
    ///                                                           palette is optional - a real game rarely reuses
    ///                                                           one tile shape for two differently-coloured
    ///                                                           enemies, but Galaga's entrance animation does
    ///                                                           exactly that, so a tile that recorded its real
    ///                                                           palette only matches that one; no palette
    ///                                                           recorded (older files) matches any palette)
    /// The group is found in a frame when all its tiles are present at these relative positions, also mirrored
    /// horizontally and/or vertically (all flip flags of the tiles then toggle together).
    struct HdObject
    {
        struct Tile { int dx = 0, dy = 0; uint64_t hash = 0; bool flipH = false, flipV = false; int palette = -1; };
        std::string name;
        bool sprite = true;
        int width = 0, height = 0, margin = 0;
        bool overflow = false;
        std::vector<Tile> tiles;
        cv::Mat picture;   ///< BGRA, (width+2*margin)*k x (height+2*margin)*k pixels for any k >= 1
    };

    class HdLayer
    {
    public:
        /// 0 = off, otherwise the output is 256*scale x 240*scale pixels (2-8).
        static void SetScale(int scale);
        static int Scale() { return scale_.load(std::memory_order_relaxed); }
        /// Folder of the skin pack (`skins/<rom>`); the tiles are looked up in its `tiles` subfolder.
        static void SetPackDir(const std::string& dir);
        /// Reads the tile PNGs again when the folder changed (cheap check, call a few times a second).
        static void ReloadPackIfChanged();
        /// CPU thread, after a frame was rendered: copies what the layer needs (about 20 KB and the picture) and
        /// hands it to the worker thread, which decodes, verifies and composes from that copy alone.
        static void OnFrame(long long frame);
        /// UI thread: the newest finished HD picture (256*scale x 240*scale, BGR). False if there is none yet.
        /// The cv::Mat shares its buffer with the layer; treat it as read-only.
        static bool Compose(cv::Mat& out);
        /// The next processed frame also writes `capture/cap_<frame>.json` + `.png` into the pack folder
        /// (input for the skin editor).
        static void RequestCapture() { captureRequested_ = true; }
        /// For measurements: do everything inside OnFrame() on the calling thread instead of on the worker.
        static void SetSynchronous(bool on);
        /// Number of tile pictures currently loaded from the pack.
        static size_t SkinCount();

        /// 64-bit FNV-1a hash of 16 tile bytes, as 16 hex digits (the file name of a tile).
        static std::string HashName(uint64_t hash);
        static uint64_t HashBytes(const uint8_t* bytes, int count);

        /// Reads the text of an .obj file (see HdObject); false if it is not valid.
        static bool ParseObject(const std::string& text, HdObject& out);
        /// Finds every place where an object's tiles are all present in `cells` (not yet `consumed`), paints its
        /// picture onto `hd` and marks the member cells as consumed. Returns the number of matches painted.
        static int PaintObjects(cv::Mat& hd, int scale, const std::vector<HdCell>& cells,
                                const std::vector<HdObject>& objects, std::vector<char>& consumed);

        /// Pure helpers, exposed for tests --------------------------------------------------------
        /// Palette index (0-3) of pixel (x, y) of a tile given its 16 bytes, before any flip.
        static int PixelIndex(const uint8_t bytes[16], int x, int y);
        /// Marks which pixels of `cell` are visible in `frame` (BGR 256x240); sets cell.visible and cell.visibleMask.
        /// A cell of which fewer than 40% of the opaque pixels match is treated as not drawn there at all.
        static void MeasureVisibility(HdCell& cell, const cv::Mat& frame);
        /// Paints the skin `tile` (BGRA, exactly 8*scale x 8*scale) of `cell` onto `hd` (BGR, scale*256 x scale*240)
        /// for the pixels flagged in cell.visibleMask.
        static void PaintCell(cv::Mat& hd, int scale, const HdCell& cell, const cv::Mat& tile);
        /// Number of objects loaded from the pack.
        static size_t ObjectCount();

    private:
        static std::atomic<int> scale_;
        static std::atomic<bool> captureRequested_;
    };
}
