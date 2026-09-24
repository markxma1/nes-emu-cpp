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
#include <mutex>
#include "NES_PPU_Register.h"
#include "NES_PPU_Palette.h"
#include <algorithm>
#include <iterator>

namespace NES
{
    NES_PPU::Picture NES_PPU::TempPatternTable(128 * 2, 128);
    std::unordered_map<int, BitmapWithInfo> NES_PPU::patternArray;
    bool NES_PPU::DrawRefresh = false;

    /// Converts a tile at a raw pattern-table byte offset (indexing directly
    /// into the full PPU memory, not a bank) to a Picture. Used by the
    /// pattern-table debug view.
    NES_PPU::Picture NES_PPU::Tile_StartAdress(int startAdress, int pallete)
    {
        NES_PPU_Color color = NES_PPU_Palette::getPalette(pallete);
        const AddrVec& PatternTable = NES_PPU_Memory::Memory;
        int ID = GetTileID(startAdress, pallete, PatternTable);
        return CreateTileBitmap(startAdress, color, PatternTable, ID);
    }

    /// Converts a background tile by tile ID, using PPUCTRL's background
    /// pattern-table-select bit to pick the bank.
    NES_PPU::Picture NES_PPU::Tile(uint16_t spriteID, int pallete)
    {
        int startAdress = spriteID * 16;
        NES_PPU_Color color = NES_PPU_Palette::getPalette(pallete);
        const AddrVec& PatternTable = NES_PPU_Memory::PatternTableN[NES_PPU_Register::PPUCTRL.B() ? 1 : 0];
        int ID = GetTileID(startAdress, pallete, PatternTable);
        return CreateTileBitmap(startAdress, color, PatternTable, ID);
    }

    /// Converts an OAM sprite tile by ID, explicit sprite palette and bank.
    NES_PPU::Picture NES_PPU::Tile(uint16_t spriteID, int pallete, int bankID)
    {
        int startAdress = spriteID * 16;
        NES_PPU_Color color = NES_PPU_Palette::getSpriteColorPalette(pallete);
        const AddrVec& PatternTable = NES_PPU_Memory::PatternTableN[static_cast<size_t>(bankID)];
        int ID = GetTileID(startAdress, pallete, PatternTable);
        return CreateTileBitmap(startAdress, color, PatternTable, ID);
    }

    // New: found live as a real regression the moment
    // per-scanline background rendering was first plugged in (Chip 'n
    // Dale's title screen losing all its non-black colors, keeping only
    // stray black outline pixels): CreateTileBitmap()'s patternArray cache
    // (see its own comment - "an unchanged tile+palette is re-blitted from
    // cache instead of re-decoded every frame") was built under the old
    // once-per-*whole-frame* Display() model, where by the time any tile
    // was ever decoded, that entire frame's CPU execution (including
    // whatever palette writes the game's own boot code made) had already
    // finished. With per-scanline rendering, a specific (tile, palette) ID
    // that happens to be needed *before* the game's boot code gets around
    // to writing its real palette colors (VERY plausible during the first
    // real frame or two, since a given nametable tile-row is only ever
    // decoded once per frame either way) gets permanently cached with the
    // wrong (default/black) color - the dirty-flag check that would
    // normally trigger a redecode (NES_PPU_Palette::BGIsNew(), compared
    // against oldValue) gets silently satisfied by
    // NES_PPU_Palette::setAllPaletesAsOld() at that *same* frame's end,
    // before this exact (tile, palette) ID is ever visited again to
    // benefit from the now-correct palette - permanently freezing the
    // wrong color for the lifetime of the cache entry, since most games
    // set their palette once at boot and never touch it again. Bypasses
    // patternArray entirely (same core pixel-unpack math as
    // CreateNewTile(), just never cached) so every call sees genuinely
    // live CHR + palette state, matching this whole redesign's actual
    // goal - correctness over the cache's performance win, which sprites
    // and the debug tooling (still going through the cached Tile()/
    // CreateTileBitmap() path, unaffected by this specific race since
    // they're not driven by the new per-scanline clock) don't need to give
    // up.
    namespace
    {
        // New: see ClearFreshTileCaches()'s own comment
        // for why these exist and why they're safe (a *per-frame* cache,
        // not patternArray's cross-frame one). Keyed by (tileID, palette)
        // for background, (tileID, palette, bank) for sprites - enough
        // distinct combinations that collisions can't happen within either
        // key scheme's own bit budget (tileID: 0-255, palette: 0-3, bank: 0-1).
        std::unordered_map<int, NES_PPU::Picture> freshBackgroundTileCache;
        std::unordered_map<int, NES_PPU::Picture> freshSpriteTileCache;
    }

    // New: called once per real frame (from
    // NES_PPU::OnScanlineStart()'s `scanline == 0` branch, alongside the
    // backgroundBuffer/sprite buffer resets) to bound
    // DecodeBackgroundTileFresh()/DecodeSpriteTileFresh()'s per-frame cache
    // to *exactly* one frame's lifetime. Found live as a real, severe
    // performance regression (Chip 'n Dale gameplay reported "ultra slow"):
    // going fully uncached to fix the patternArray staleness bug (see
    // DecodeBackgroundTileFresh()'s own comment) meant a background tile
    // shared by many nametable cells - the overwhelmingly common case, e.g.
    // a solid wall texture - was fully re-unpacked from raw CHR bits from
    // scratch on *every* scanline it appeared on, up to 15360 times/frame
    // (64 tile-columns x 240 scanlines) versus patternArray's old ~960
    // (once per unique on-screen tile, cached across the whole frame).
    // Caching *within* a frame is safe: the exact staleness bug this
    // session found required a decode to survive *past* a
    // NES_PPU_Palette::setAllPaletesAsOld() frame boundary (see
    // DecodeBackgroundTileFresh()'s own comment for the full mechanism) -
    // a cache that's guaranteed cleared every single frame can never do
    // that, so this keeps both the correctness fix and (for the common
    // case of a tile reused many times within one frame) nearly all of
    // patternArray's original performance.
    void NES_PPU::ClearFreshTileCaches()
    {
        freshBackgroundTileCache.clear();
        freshSpriteTileCache.clear();
    }

    NES_PPU::Picture NES_PPU::DecodeBackgroundTileFresh(uint16_t spriteID, int pallete)
    {
        int key = spriteID * 4 + pallete;
        auto it = freshBackgroundTileCache.find(key);
        if (it != freshBackgroundTileCache.end())
            return it->second;

        int startAdress = spriteID * 16;
        NES_PPU_Color color = NES_PPU_Palette::getPalette(pallete);
        const AddrVec& PatternTable = NES_PPU_Memory::PatternTableN[NES_PPU_Register::PPUCTRL.B() ? 1 : 0];

        Picture bitmap(8, 8);
        for (int j = 0; j < 8; j++)
        {
            for (int i = 0; i < 8; i++)
            {
                int a = (PatternTable[static_cast<size_t>(startAdress + i)]->Value() >> j) & 0x01;
                int b = ((PatternTable[static_cast<size_t>(startAdress + i + 8)]->Value() >> j) & 0x01) << 1;
                uint8_t p = static_cast<uint8_t>(a | b);
                bitmap.SetPixel(color.color[p], 7 - j, i);
            }
        }
        freshBackgroundTileCache.emplace(key, bitmap);
        return bitmap;
    }

    // New: same reasoning as DecodeBackgroundTileFresh()
    // above (see its own comment for the full story - patternArray's cache
    // assumes "decode once per frame is enough", which per-scanline
    // rendering violates), for sprite tiles: same pixel math as the 3-arg
    // Tile() (explicit palette and bank). Cached per-frame the same way -
    // see ClearFreshTileCaches().
    NES_PPU::Picture NES_PPU::DecodeSpriteTileFresh(uint16_t spriteID, int pallete, int bankID)
    {
        int key = (spriteID * 4 + pallete) * 2 + bankID;
        auto it = freshSpriteTileCache.find(key);
        if (it != freshSpriteTileCache.end())
            return it->second;

        int startAdress = spriteID * 16;
        NES_PPU_Color color = NES_PPU_Palette::getSpriteColorPalette(pallete);
        const AddrVec& PatternTable = NES_PPU_Memory::PatternTableN[static_cast<size_t>(bankID)];

        Picture bitmap(8, 8);
        for (int j = 0; j < 8; j++)
        {
            for (int i = 0; i < 8; i++)
            {
                int a = (PatternTable[static_cast<size_t>(startAdress + i)]->Value() >> j) & 0x01;
                int b = ((PatternTable[static_cast<size_t>(startAdress + i + 8)]->Value() >> j) & 0x01) << 1;
                uint8_t p = static_cast<uint8_t>(a | b);
                bitmap.SetPixel(color.color[p], 7 - j, i);
            }
        }
        freshSpriteTileCache.emplace(key, bitmap);
        return bitmap;
    }

    /// Combines the palette number with this pattern's position in the full
    /// 16KB PPU memory into one cache key (so the same tile decoded via two
    /// different banks/mirrors still hits the same cache entry only when it's
    /// genuinely the same underlying AddressSetup cell).
    ///
    /// NOTE: performance fix, not a behavior change. This used to compute the
    /// "position within Memory" via `IndexOf(PatternTable[startAdress])`
    /// (std::find) - an O(n) linear scan over the full ~16K-entry address space, run for
    /// every tile (background + sprites), every frame. AddressSetup already
    /// carries an `id` field set to exactly this index when the cell is first
    /// constructed in NES_PPU_Memory::CreateMemory() (`std::make_shared<AddressSetup>(i)`),
    /// and pattern-table addresses (0x0000-0x1FFF) are never among the ranges
    /// NES_PPU_Memory::MemoryMirror() re-aliases, so that id is stable and
    /// already exactly what IndexOf()/std::find() was computing the hard way.
    /// `AddressSetup::ID()` is otherwise unused anywhere else in this
    /// project, so reading it here changes nothing
    /// observable - just turns an O(n) search into an O(1) field read.
    int NES_PPU::GetTileID(int startAdress, int pallete, const AddrVec& PatternTable)
    {
        int index = PatternTable[static_cast<size_t>(startAdress)]->ID();
        return pallete | (index << 8);
    }

    /// Creates a Bitmap from data saved in PPU addresses, going through the
    /// tile cache (patternArray) so an unchanged tile+palette is re-blitted
    /// from cache instead of re-decoded every frame.
    NES_PPU::Picture NES_PPU::CreateTileBitmap(int startAdress, const NES_PPU_Color& color, const AddrVec& PatternTable, int ID)
    {
        if (isNew(startAdress, PatternTable, color, ID))
        {
            if (isNewPattern(startAdress, PatternTable, ID))
            {
                BitmapWithInfo bitmap = CreateNewTile(startAdress, color, PatternTable);
                AddTileToPatternArray(ID, bitmap);
                return DrawRefreshFrame(bitmap.Image(), Color::Red());
            }
            else
            {
                BitmapWithInfo bitmap = UpdateTile(ID, color);
                AddTileToPatternArray(ID, bitmap);
                return DrawRefreshFrame(bitmap.Image(), Color::Blue(), bitmap.isNew);
            }
        }
        else
        {
            return patternArray.at(ID).Image();
        }
    }

    BitmapWithInfo NES_PPU::UpdateTile(int ID, const NES_PPU_Color& color)
    {
        bool isNewFlag = false;
        for (uint8_t cID : patternArray.at(ID).cID)
            isNewFlag = isNewFlag || color.isNewColor[cID];

        if (isNewFlag)
        {
            const auto& pattern = patternArray.at(ID).Pattern();
            Picture bitmap(8, 8);
            for (int j = 0; j < 8; j++)
                for (int i = 0; i < 8; i++)
                    bitmap.SetPixel(color.color[pattern[static_cast<size_t>(7 - j)][static_cast<size_t>(i)]], 7 - j, i);
            return BitmapWithInfo(bitmap, pattern, patternArray.at(ID).cID, true);
        }
        patternArray.at(ID).isNew = false;
        return patternArray.at(ID);
    }

    NES_PPU::Picture NES_PPU::DrawRefreshFrame(const Picture& bitmap, Color pen, bool isNew)
    {
        Picture result = bitmap;
        if (DrawRefresh && isNew)
            result.DrawInfoRectangle(pen, 0, 0, 7, 7);
        return result;
    }

    BitmapWithInfo NES_PPU::CreateNewTile(int startAdress, const NES_PPU_Color& color, const AddrVec& PatternTable)
    {
        std::array<std::array<uint8_t, 8>, 8> pattern{};
        Picture bitmap(8, 8);
        std::vector<uint8_t> cID;

        for (int j = 0; j < 8; j++)
        {
            for (int i = 0; i < 8; i++)
            {
                int a = (PatternTable[static_cast<size_t>(startAdress + i)]->Value() >> j) & 0x01;
                int b = ((PatternTable[static_cast<size_t>(startAdress + i + 8)]->Value() >> j) & 0x01) << 1;
                uint8_t p = static_cast<uint8_t>(a | b);
                pattern[static_cast<size_t>(7 - j)][static_cast<size_t>(i)] = p;

                if (std::find(cID.begin(), cID.end(), p) == cID.end())
                    cID.push_back(p);

                bitmap.SetPixel(color.color[p], 7 - j, i);
            }
        }
        PatternTable[static_cast<size_t>(startAdress)]->setAsOld();
        return BitmapWithInfo(bitmap, pattern, cID);
    }

    bool NES_PPU::isNew(int startAdress, const AddrVec& PatternTable, const NES_PPU_Color& color, int ID)
    {
        bool isnew = isNewPattern(startAdress, PatternTable, ID);
        isnew |= color.isNewPalette;
        return isnew;
    }

    bool NES_PPU::isNewPattern(int startAdress, const AddrVec& PatternTable, int ID)
    {
        bool isnew = patternArray.find(ID) == patternArray.end();
        isnew |= PatternTable[static_cast<size_t>(startAdress)]->isNew();
        return isnew;
    }

    /// Puts a tile into the cache buffer, replacing any existing entry for the same ID.
    void NES_PPU::AddTileToPatternArray(int ID, const BitmapWithInfo& bitmap)
    {
        auto it = patternArray.find(ID);
        if (it != patternArray.end())
            it->second = bitmap;
        else
            patternArray.emplace(ID, bitmap);
    }

    /// Creates a Bitmap with the patterns used for the running game.
    /// @param PN palette number
    NES_PPU::Picture NES_PPU::DecodeTileFromChr(const ChrSnapshot& chr, int startAddress, const NES_PPU_Color& color)
    {
        Picture bitmap(8, 8);
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
            {
                int a = (chr.data[static_cast<size_t>(startAddress + i)] >> j) & 0x01;
                int b = ((chr.data[static_cast<size_t>(startAddress + i + 8)] >> j) & 0x01) << 1;
                bitmap.SetPixel(color.color[static_cast<uint8_t>(a | b)], 7 - j, i);
            }
        return bitmap;
    }

    NES_PPU::Picture NES_PPU::PatternTable(int PN)
    {
        std::shared_ptr<const ChrSnapshot> shown;
        {
            std::lock_guard<std::mutex> lock(chrPublishMutex);
            int v = chrView;
            if (v >= 0 && v < static_cast<int>(publishedChrStates.size()))
                shown = publishedChrStates[static_cast<size_t>(v)];
        }
        if (shown)
        {
            Picture snap(128 * 2, 128);
            NES_PPU_Color color = NES_PPU_Palette::getPalette(PN);
            for (int i = 0; i < 16; i++)
                for (int j = 0; j < 16; j++)
                {
                    int t = (i * 16 + j) * 16;
                    snap.DrawImage(DecodeTileFromChr(*shown, t, color), j * 8, i * 8);
                    snap.DrawImage(DecodeTileFromChr(*shown, t + 4096, color), (j + 16) * 8, i * 8);
                }
            return snap;
        }
        Picture bitmap(TempPatternTable);
        if (NES_PPU_Register::PPUCTRL.V())
        {
            int k = 0;
            for (int i = 0; i < 16; i++)
            {
                for (int j = 0; j < 16; j++)
                {
                    int t = (k++) * 16;
                    bitmap.DrawImage(Tile_StartAdress(t, PN), j * 8, i * 8);
                    bitmap.DrawImage(Tile_StartAdress(t + 4096, PN), (j + 16) * 8, i * 8);
                }
            }
            TempPatternTable = bitmap;
        }
        return bitmap;
    }

    NES_PPU::Picture NES_PPU::DecodeBackgroundTileFromSnapshot(uint16_t tileID, int palette, const ChrSnapshot& chr)
    {
        int start = (NES_PPU_Register::PPUCTRL.B() ? 0x1000 : 0) + tileID * 16;
        NES_PPU_Color color = NES_PPU_Palette::getPalette(palette);
        Picture bitmap(8, 8);
        for (int j = 0; j < 8; j++)
        {
            for (int i = 0; i < 8; i++)
            {
                int a = (chr.data[static_cast<size_t>(start + i)] >> j) & 0x01;
                int b = ((chr.data[static_cast<size_t>(start + i + 8)] >> j) & 0x01) << 1;
                bitmap.SetPixel(color.color[static_cast<uint8_t>(a | b)], 7 - j, i);
            }
        }
        return bitmap;
    }
}
