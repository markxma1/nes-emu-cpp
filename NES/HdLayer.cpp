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
#include "HdLayer.h"
#include "Profiler.h"
#include "NES_Console.h"
#include "NES_PPU_Memory.h"
#include "NES_PPU_OAM.h"
#include "NES_PPU_Palette.h"
#include "NES_PPU_Register.h"
#include "NES_PPU_AttributeTable.h"
#include "NES_PPU.h"
#include "Picture.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <unordered_map>

namespace NES
{
    namespace fs = std::filesystem;

    std::atomic<int> HdLayer::scale_{0};
    std::atomic<bool> HdLayer::captureRequested_{false};

    namespace
    {
        struct Snapshot
        {
            long long frame = 0;
            cv::Mat image;               // the finished frame, 256x240 BGR
            std::vector<HdCell> cells;
        };

        std::mutex snapshotMutex;
        std::shared_ptr<const Snapshot> latest;

        std::mutex packMutex;
        std::string packDir;
        std::unordered_map<std::string, cv::Mat> skins;        // file stem -> BGRA picture as stored
        std::unordered_map<std::string, cv::Mat> scaledSkins;  // "<stem>@<scale>" -> resized to 8*scale
        long long packStamp = -1;

        long long DirStamp(const std::string& dir)
        {
            struct stat st{};
            if (stat(dir.c_str(), &st) != 0)
                return 0;
            return static_cast<long long>(st.st_mtime) * 1000000000LL + st.st_mtim.tv_nsec;
        }

        void LoadPackLocked()
        {
            skins.clear();
            scaledSkins.clear();
            std::error_code ec;
            const fs::path tiles = fs::path(packDir) / "tiles";
            for (auto it = fs::directory_iterator(tiles, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                if (it->path().extension() != ".png")
                    continue;
                cv::Mat img = cv::imread(it->path().string(), cv::IMREAD_UNCHANGED);
                if (img.empty() || img.cols % 8 != 0 || img.rows != img.cols)
                {
                    std::cerr << "[skins] ignoring " << it->path().filename().string() << " (must be square, size a multiple of 8)" << std::endl;
                    continue;
                }
                if (img.channels() == 3)
                    cv::cvtColor(img, img, cv::COLOR_BGR2BGRA);
                else if (img.channels() == 1)
                    cv::cvtColor(img, img, cv::COLOR_GRAY2BGRA);
                skins[it->path().stem().string()] = img;
            }
            packStamp = DirStamp(tiles.string());
        }

        /// Skin picture for a cell at the given output scale, or an empty Mat.
        const cv::Mat& FindSkin(const HdCell& cell, int scale, const std::string& hashName)
        {
            static const cv::Mat none;
            const std::string specific = hashName + (cell.sprite ? "_s" : "_b") + std::to_string(cell.palette);
            for (const std::string* key : { &specific, &hashName })
            {
                auto it = skins.find(*key);
                if (it == skins.end())
                    continue;
                const std::string cacheKey = *key + "@" + std::to_string(scale);
                auto cached = scaledSkins.find(cacheKey);
                if (cached == scaledSkins.end())
                {
                    cv::Mat resized;
                    const int side = 8 * scale;
                    if (it->second.cols == side)
                        resized = it->second;
                    else
                        cv::resize(it->second, resized, cv::Size(side, side), 0, 0, it->second.cols > side ? cv::INTER_AREA : cv::INTER_CUBIC);
                    cached = scaledSkins.emplace(cacheKey, resized).first;
                }
                return cached->second;
            }
            return none;
        }

        /// Everything the layer needs from the emulator for one frame, copied on the CPU thread
        /// (about 20 KB + the picture). The worker thread then decodes and composes from this copy alone,
        /// with its own memory, without touching any emulator state while the game keeps running.
        struct RawState
        {
            long long frame = 0;
            NES_PPU::Picture picture{256, 240};
            bool bgOn = false, spritesOn = false, tall = false;
            int spriteBank = 0, bgBank = 0;
            int xScroll = 0, yScroll = 0;
            uint8_t oam[64][4] = {};                 // Y, tile, attributes, X of each sprite
            std::array<uint8_t, 4096> chr[2];        // both pattern tables
            uint8_t nametable[4][960] = {};
            uint8_t tilePalette[4][960] = {};        // attribute-table palette of every background tile
            uint8_t spriteRgb[4][4][3] = {};         // resolved colours: [palette][index][R,G,B]
            uint8_t bgRgb[4][4][3] = {};
        };

        void FillColours(HdCell& cell, bool sprite, const RawState& raw)
        {
            const uint8_t(*rgb)[3] = sprite ? raw.spriteRgb[cell.palette] : raw.bgRgb[cell.palette];
            for (int i = 0; i < 4; i++)
                for (int c = 0; c < 3; c++)
                    cell.rgb[i][c] = rgb[i][c];
        }

        void ReadTile(HdCell& cell, const RawState& raw, int bank, int tileIndex)
        {
            std::copy(raw.chr[bank].begin() + tileIndex * 16, raw.chr[bank].begin() + tileIndex * 16 + 16, cell.bytes);
            cell.hash = HdLayer::HashBytes(cell.bytes, 16);
        }
    }

    uint64_t HdLayer::HashBytes(const uint8_t* bytes, int count)
    {
        uint64_t h = 1469598103934665603ULL;
        for (int i = 0; i < count; i++)
        {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::string HdLayer::HashName(uint64_t hash)
    {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
        return buf;
    }

    int HdLayer::PixelIndex(const uint8_t bytes[16], int x, int y)
    {
        return ((bytes[y] >> (7 - x)) & 1) | (((bytes[y + 8] >> (7 - x)) & 1) << 1);
    }

    void HdLayer::MeasureVisibility(HdCell& cell, const cv::Mat& frame)
    {
        cell.visible = 0;
        cell.visibleMask = 0;
        int opaque = 0, match = 0;
        uint64_t mask = 0;
        for (int py = 0; py < 8; py++)
        {
            for (int px = 0; px < 8; px++)
            {
                const int idx = PixelIndex(cell.bytes, cell.flipH ? 7 - px : px, cell.flipV ? 7 - py : py);
                if (cell.sprite && idx == 0)
                    continue;
                opaque++;
                const int sx = cell.x + px, sy = cell.y + py;
                if (sx < 0 || sy < 0 || sx >= frame.cols || sy >= frame.rows)
                    continue;
                const cv::Vec3b& p = frame.at<cv::Vec3b>(sy, sx);
                if (p[0] == cell.rgb[idx][2] && p[1] == cell.rgb[idx][1] && p[2] == cell.rgb[idx][0])
                {
                    match++;
                    mask |= 1ULL << (py * 8 + px);
                }
            }
        }
        if (opaque == 0 || match * 100 < opaque * 40)
            return;
        cell.visible = match;
        cell.visibleMask = mask;
    }

    void HdLayer::PaintCell(cv::Mat& hd, int scale, const HdCell& cell, const cv::Mat& tile)
    {
        for (int py = 0; py < 8; py++)
        {
            for (int px = 0; px < 8; px++)
            {
                if (!(cell.visibleMask & (1ULL << (py * 8 + px))))
                    continue;
                const int fx = cell.flipH ? 7 - px : px, fy = cell.flipV ? 7 - py : py;
                for (int j = 0; j < scale; j++)
                {
                    const cv::Vec4b* src = tile.ptr<cv::Vec4b>(fy * scale + j);
                    cv::Vec3b* dst = hd.ptr<cv::Vec3b>((cell.y + py) * scale + j);
                    for (int i = 0; i < scale; i++)
                    {
                        const cv::Vec4b& s = src[fx * scale + i];
                        cv::Vec3b& d = dst[(cell.x + px) * scale + i];
                        const int a = s[3];
                        if (a == 255)
                            d = cv::Vec3b(s[0], s[1], s[2]);
                        else if (a > 0)
                            for (int c = 0; c < 3; c++)
                                d[c] = static_cast<uint8_t>((s[c] * a + d[c] * (255 - a)) / 255);
                    }
                }
            }
        }
    }

    void HdLayer::SetScale(int scale)
    {
        scale_.store(scale <= 0 ? 0 : std::clamp(scale, 2, 8));
    }

    void HdLayer::SetPackDir(const std::string& dir)
    {
        std::lock_guard<std::mutex> l(packMutex);
        packDir = dir;
        LoadPackLocked();
    }

    void HdLayer::ReloadPackIfChanged()
    {
        std::lock_guard<std::mutex> l(packMutex);
        if (packDir.empty())
            return;
        const std::string tiles = (fs::path(packDir) / "tiles").string();
        if (DirStamp(tiles) != packStamp)
            LoadPackLocked();
    }

    size_t HdLayer::SkinCount()
    {
        std::lock_guard<std::mutex> l(packMutex);
        return skins.size();
    }

    namespace
    {
        void WriteCapture(const Snapshot& snap)
        {
            const fs::path dir = fs::path(packDir) / "capture";
            std::error_code ec;
            fs::create_directories(dir, ec);
            const std::string stem = "cap_" + std::to_string(snap.frame);
            cv::imwrite((dir / (stem + ".png")).string(), snap.image);
            std::ofstream f(dir / (stem + ".json"));
            f << "{\"frame\":" << snap.frame << ",\"cells\":[\n";
            bool first = true;
            for (const HdCell& c : snap.cells)
            {
                char hex[33];
                for (int i = 0; i < 16; i++)
                    std::snprintf(hex + i * 2, 3, "%02x", c.bytes[i]);
                f << (first ? "" : ",\n") << "{\"x\":" << c.x << ",\"y\":" << c.y << ",\"s\":" << (c.sprite ? 1 : 0)
                  << ",\"fh\":" << (c.flipH ? 1 : 0) << ",\"fv\":" << (c.flipV ? 1 : 0) << ",\"pal\":" << int(c.palette)
                  << ",\"hash\":\"" << HdLayer::HashName(c.hash) << "\",\"bytes\":\"" << hex << "\",\"vis\":" << c.visible
                  << ",\"rgb\":[";
                for (int i = 0; i < 4; i++)
                    f << (i ? "," : "") << "[" << int(c.rgb[i][0]) << "," << int(c.rgb[i][1]) << "," << int(c.rgb[i][2]) << "]";
                f << "]}";
                first = false;
            }
            f << "\n]}\n";
            std::cout << "[skins] captured frame " << snap.frame << " -> " << (dir / stem).string() << ".json (" << snap.cells.size() << " tiles)" << std::endl;
        }

        /// Worker side, no emulator state involved: list the cells of the copied frame and check them against its picture.
        std::shared_ptr<Snapshot> BuildSnapshot(const RawState& raw)
        {
            NES_PROFILE_HOT("hd_cells");
            auto snap = std::make_shared<Snapshot>();
            snap->frame = raw.frame;
            snap->image = raw.picture.Image();

            if (raw.bgOn)
            {
                const int xs = raw.xScroll, ys = raw.yScroll;
                for (int row = 0; row < 31; row++)
                {
                    const int logicalY = ((ys + row * 8) % 480 + 480) % 480;
                    const int tileRow = logicalY / 8;
                    const int localRow = tileRow < 30 ? tileRow : tileRow - 30;
                    const int ntLeft = tileRow < 30 ? 0 : 2;
                    for (int col = 0; col < 33; col++)
                    {
                        const int logicalX = ((xs + col * 8) % 512 + 512) % 512;
                        const int tileCol = logicalX / 8;
                        const int nt = tileCol < 32 ? ntLeft : ntLeft + 1;
                        const int k = localRow * 32 + tileCol % 32;
                        HdCell cell;
                        cell.sprite = false;
                        cell.x = col * 8 - xs % 8;
                        cell.y = row * 8 - ys % 8;
                        cell.palette = raw.tilePalette[nt][k];
                        ReadTile(cell, raw, raw.bgBank, raw.nametable[nt][k]);
                        FillColours(cell, false, raw);
                        HdLayer::MeasureVisibility(cell, snap->image);
                        if (cell.visible)
                            snap->cells.push_back(cell);
                    }
                }
            }

            if (raw.spritesOn)
            {
                for (int i = 0; i < 64; i++)
                {
                    const uint8_t attr = raw.oam[i][2];
                    const int x = raw.oam[i][3];
                    const int y = raw.oam[i][0] + 1;
                    if (y >= 240)
                        continue;
                    const int parts = raw.tall ? 2 : 1;
                    for (int part = 0; part < parts; part++)
                    {
                        HdCell cell;
                        cell.sprite = true;
                        cell.x = x;
                        cell.flipH = (attr & 0x40) != 0;
                        cell.flipV = (attr & 0x80) != 0;
                        cell.palette = attr & 3;
                        int bank, index;
                        if (!raw.tall)
                        {
                            bank = raw.spriteBank;
                            index = raw.oam[i][1];
                            cell.y = y;
                        }
                        else
                        {
                            bank = raw.oam[i][1] & 1;
                            const int top = raw.oam[i][1] & 0xFE;
                            const bool second = (part == 1) != cell.flipV; // vertical flip swaps the two halves
                            index = second ? top + 1 : top;
                            cell.y = y + part * 8;
                        }
                        ReadTile(cell, raw, bank, index & 0xFF);
                        FillColours(cell, true, raw);
                        HdLayer::MeasureVisibility(cell, snap->image);
                        if (cell.visible)
                            snap->cells.push_back(cell);
                    }
                }
            }
            return snap;
        }

        /// Worker side: the enlarged picture of a snapshot with all skins painted.
        void ComposeSnapshot(const Snapshot& snap, int scale, cv::Mat& out)
        {
            NES_PROFILE_HOT("hd_compose");
            cv::resize(snap.image, out, cv::Size(256 * scale, 240 * scale), 0, 0, cv::INTER_NEAREST);
            std::lock_guard<std::mutex> l(packMutex);
            if (skins.empty())
                return;
            for (const HdCell& cell : snap.cells)
            {
                const cv::Mat& tile = FindSkin(cell, scale, HdLayer::HashName(cell.hash));
                if (!tile.empty())
                    HdLayer::PaintCell(out, scale, cell, tile);
            }
        }

        /// The worker thread: takes the newest copied frame (older ones that arrived meanwhile are dropped),
        /// builds and composes it, and publishes the result for the UI.
        class Worker
        {
        public:
            Worker() : thread_([this] { Run(); }) {}
            ~Worker()
            {
                {
                    std::lock_guard<std::mutex> l(m_);
                    stop_ = true;
                }
                cv_.notify_all();
                thread_.join();
            }
            void Submit(std::unique_ptr<RawState> raw)
            {
                {
                    std::lock_guard<std::mutex> l(m_);
                    pending_ = std::move(raw);
                }
                cv_.notify_one();
            }
            bool Latest(cv::Mat& out)
            {
                std::lock_guard<std::mutex> l(m_);
                if (result_.empty())
                    return false;
                out = result_; // shares the buffer; the worker always writes a fresh Mat
                return true;
            }

        private:
            void Run()
            {
                for (;;)
                {
                    std::unique_ptr<RawState> raw;
                    {
                        std::unique_lock<std::mutex> l(m_);
                        cv_.wait(l, [this] { return stop_ || pending_; });
                        if (stop_)
                            return;
                        raw = std::move(pending_);
                    }
                    const int scale = HdLayer::Scale();
                    if (scale == 0)
                        continue;
                    std::shared_ptr<Snapshot> snap = BuildSnapshot(*raw);
                    if (captureNow_.exchange(false))
                        WriteCapture(*snap);
                    cv::Mat out;
                    ComposeSnapshot(*snap, scale, out);
                    std::lock_guard<std::mutex> l(m_);
                    result_ = std::move(out);
                }
            }
            std::mutex m_;
            std::condition_variable cv_;
            std::unique_ptr<RawState> pending_;
            cv::Mat result_;
            bool stop_ = false;
            std::thread thread_;
        public:
            std::atomic<bool> captureNow_{false};
        };

        Worker& TheWorker()
        {
            static Worker worker;
            return worker;
        }

        std::atomic<bool> synchronous{false};
        cv::Mat synchronousResult;
    }

    void HdLayer::SetSynchronous(bool on) { synchronous.store(on); }

    void HdLayer::OnFrame(long long frame)
    {
        if (scale_.load(std::memory_order_relaxed) == 0)
            return;
        NES_PROFILE_HOT("hd_capture_state");
        static const char* captureAt = std::getenv("NES_HD_CAPTURE_FRAME"); // test/automation switch
        if (captureAt && frame == std::atoll(captureAt))
            RequestCapture();

        // Copy what the emulator holds for this frame; from here on the worker uses only this copy.
        auto raw = std::make_unique<RawState>();
        raw->frame = frame;
        raw->picture = NES_Console::getDisplay();
        raw->bgOn = NES_PPU_Register::PPUMASK.b();
        raw->spritesOn = NES_PPU_Register::PPUMASK.s();
        raw->tall = NES_PPU_Register::PPUCTRL.H();
        raw->spriteBank = NES_PPU_Register::PPUCTRL.S() ? 1 : 0;
        raw->bgBank = NES_PPU_Register::PPUCTRL.B() ? 1 : 0;
        raw->xScroll = NES_PPU::xScroll;
        raw->yScroll = NES_PPU::yScroll;
        for (int i = 0; i < 64; i++)
        {
            raw->oam[i][0] = NES_PPU_OAM::SpriteYc[static_cast<size_t>(i)]->Value();
            raw->oam[i][1] = NES_PPU_OAM::SpriteTile[static_cast<size_t>(i)].adress->Value();
            raw->oam[i][2] = NES_PPU_OAM::SpriteAttribute[static_cast<size_t>(i)].adress->Value();
            raw->oam[i][3] = NES_PPU_OAM::SpriteXc[static_cast<size_t>(i)]->Value();
        }
        for (int bank = 0; bank < 2; bank++)
            for (size_t i = 0; i < 4096; i++)
                raw->chr[bank][i] = NES_PPU_Memory::PatternTableN[static_cast<size_t>(bank)][i]->Value();
        for (int nt = 0; nt < 4; nt++)
        {
            const std::vector<int> attributes = NES_PPU_AttributeTable::AttributeTable(nt);
            for (size_t k = 0; k < 960; k++)
            {
                raw->nametable[nt][k] = NES_PPU_Memory::NameTableN[static_cast<size_t>(nt)][k]->Value();
                raw->tilePalette[nt][k] = static_cast<uint8_t>(attributes[k]);
            }
        }
        for (int pal = 0; pal < 4; pal++)
        {
            NES_PPU_Color sprite = NES_PPU_Palette::getSpriteColorPalette(pal);
            NES_PPU_Color bg = NES_PPU_Palette::getBGColorPalette(pal);
            for (int i = 0; i < 4; i++)
            {
                NES_PPU::Color c = sprite.color[static_cast<size_t>(i)];
                raw->spriteRgb[pal][i][0] = c.R; raw->spriteRgb[pal][i][1] = c.G; raw->spriteRgb[pal][i][2] = c.B;
                c = i == 0 ? NES_PPU_Palette::UniversalBackgroundColor() : bg.color[static_cast<size_t>(i)];
                raw->bgRgb[pal][i][0] = c.R; raw->bgRgb[pal][i][1] = c.G; raw->bgRgb[pal][i][2] = c.B;
            }
        }
        if (captureRequested_.exchange(false))
            TheWorker().captureNow_ = true;

        if (synchronous.load())
        {
            // for measuring: everything on this thread
            auto snap = BuildSnapshot(*raw);
            if (TheWorker().captureNow_.exchange(false))
                WriteCapture(*snap);
            ComposeSnapshot(*snap, scale_.load(), synchronousResult);
            return;
        }
        TheWorker().Submit(std::move(raw));
    }

    bool HdLayer::Compose(cv::Mat& out)
    {
        if (scale_.load(std::memory_order_relaxed) == 0)
            return false;
        if (synchronous.load())
        {
            out = synchronousResult;
            return !out.empty();
        }
        return TheWorker().Latest(out);
    }
}
