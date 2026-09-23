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
#include "Picture.h"
#include <algorithm>

namespace NES_PPU
{
    Picture::Picture(int width, int height)
        : width(width), height(height),
          img(static_cast<size_t>(width) * static_cast<size_t>(height)),
          infoLayer(static_cast<size_t>(width) * static_cast<size_t>(height))
    {
    }

    // NOTE: performance fix - Picture is UI/graphics plumbing, so this is plain
    // implementation quality. The
    // original version of this copy constructor rebuilt `img`/`infoLayer` one
    // pixel at a time via GetPixel()/SetPixel() (bounds check + mirror-recursion
    // per pixel) instead of just copying the backing vectors - for the 512x480
    // name-table bitmap (NameTabele()'s TempNameTable, copied out on every
    // single Display() call) that's 245,760 branchy per-pixel calls per copy.
    // A direct member-wise copy produces byte-identical results (GetPixel's
    // mirror/info-layer logic only ever resolves to a value already sitting in
    // `img`/`infoLayer`/`mirror`, so copying those fields directly captures the
    // same effective pixels) and is also more complete: the old version never
    // copied `mirror`/`haveMirror` at all, silently leaving every copy in the
    // "no mirror" state regardless of the source's.
    Picture::Picture(const Picture& other)
        : width(other.width), height(other.height),
          img(other.img), infoLayer(other.infoLayer),
          mirror(other.mirror), haveMirror(other.haveMirror)
    {
    }

    Color Picture::GetPixel(int x, int y) const
    {
        if (x >= 0 && y >= 0 && x < width && y < height)
        {
            Color info = getInfLayerPixel(x, y);
            if (info != Color())
                return info;

            // FIXED: this used to recurse into GetPixel(x - mirror.x, y -
            // mirror.y), which re-ran the info-layer check above at the
            // *mirrored* coordinate too - so a debug overlay pixel drawn at
            // e.g. x=0 (the name-table viewer's border) got echoed at the
            // mirror axis (x=mirror.x) as an unintended side effect, even
            // though DrawMirror() only ever meant to mirror the tile *image*
            // data, never the debug info layer. Since NameTabele() bakes
            // that overlay into the exact Picture DrawBackground() samples
            // for real on-screen frames, the echo wasn't just a debug-window
            // cosmetic glitch - it showed up as a genuine stray 1px vertical
            // line in actual gameplay output whenever the scroll viewport
            // straddled the mirror axis. Reading `img` directly here mirrors
            // only the pixel data, matching DrawMirror()'s actual intent.
            if (haveMirror && (((x >= mirror.x) && (mirror.x != 0)) || ((y >= mirror.y) && (mirror.y != 0))))
            {
                int mx = x - mirror.x;
                int my = y - mirror.y;
                if (mx >= 0 && my >= 0 && mx < width && my < height)
                    return at(img, mx, my);
                return Color::Transparent();
            }

            return at(img, x, y);
        }
        return Color::Transparent();
    }

    void Picture::SetInfLayerPixel(Color color, int x, int y)
    {
        if (x >= 0 && y >= 0 && x < width && y < height)
            at(infoLayer, x, y) = color;
    }

    void Picture::SetPixel(Color color, int x, int y)
    {
        if (x >= 0 && y >= 0 && x < width && y < height)
            at(img, x, y) = color;
    }

    void Picture::DrawPixel(Color color, int x, int y)
    {
        SetPixel(add(GetPixel(x, y), color), x, y);
    }

    cv::Mat Picture::Image() const
    {
        cv::Mat mat(height, width, CV_8UC3);
        for (int y = 0; y < height; y++)
        {
            auto* row = mat.ptr<cv::Vec3b>(y);
            for (int x = 0; x < width; x++)
            {
                Color c = GetPixel(x, y);
                row[x] = cv::Vec3b(c.B, c.G, c.R); // OpenCV is BGR
            }
        }
        return mat;
    }

    void Picture::FillRectangle(Color color, int x, int y, int width_, int height_)
    {
        for (int i = x; i < width_; i++)
            for (int j = y; j < height_; j++)
                SetPixel(color, i, j);
    }

    void Picture::DrawImage(const Picture& bitmap, int x, int y)
    {
        for (int i = x; i < x + bitmap.width; i++)
            for (int j = y; j < y + bitmap.height; j++)
                DrawPixel(bitmap.GetPixel(i - x, j - y), i, j);
    }

    void Picture::DrawNewImage(const Picture& bitmap, int x, int y)
    {
        for (int i = x; i < x + bitmap.width; i++)
            for (int j = y; j < y + bitmap.height; j++)
                SetPixel(bitmap.GetPixel(i - x, j - y), i, j);
    }

    void Picture::DrawMirror(int x, int y)
    {
        mirror.x = x;
        mirror.y = y;
        HaveMirror(true);
    }

    void Picture::DrawRectangle(Color color, int x, int y, int width_, int height_)
    {
        for (int i = x; i < x + width_; i++)
            for (int j = y; j < y + height_; j++)
                if (j == y || i == x || j == y + height_ - 1 || i == x + width_ - 1)
                    SetPixel(color, i, j);
    }

    void Picture::DrawInfoRectangle(Color color, int x, int y, int width_, int height_)
    {
        for (int i = x; i < x + width_; i++)
            for (int j = y; j < y + height_; j++)
                if (j == y || i == x || j == y + height_ - 1 || i == x + width_ - 1)
                    SetInfLayerPixel(color, i, j);
    }

    void Picture::DrawImage(const Picture& bitmap, Rect destRec, Rect srcRec)
    {
        if (srcRec.Width == destRec.Width || srcRec.Height == destRec.Height)
            SameSize(bitmap, destRec, srcRec);
        else
            NotSameSize(bitmap, destRec, srcRec);
    }

    void Picture::NotSameSize(const Picture& bitmap, Rect destRec, Rect srcRec)
    {
        Picture temp1(srcRec.Width, srcRec.Height);
        for (int i = srcRec.X; i < srcRec.Width + srcRec.X; i++)
            for (int j = srcRec.Y; j < srcRec.Height + srcRec.Y; j++)
                temp1.SetPixel(bitmap.GetPixel(i, j), i - srcRec.X, j - srcRec.Y);

        int W2 = destRec.Width - destRec.X;
        int H2 = destRec.Height - destRec.Y;
        std::vector<Color> resized = ResizeArray(temp1.getMatrix(), temp1.width, temp1.height, W2, H2);
        Picture temp2(W2, H2);
        temp2.img = resized;

        for (int i = destRec.X; i < width; i++)
            for (int j = destRec.Y; j < height; j++)
                DrawPixel(temp2.GetPixel(i - destRec.X, j - destRec.Y), i, j);
    }

    void Picture::SameSize(const Picture& bitmap, Rect destRec, Rect srcRec)
    {
        for (int i = srcRec.X; i < srcRec.Width + srcRec.X; i++)
            for (int j = srcRec.Y; j < srcRec.Height + srcRec.Y; j++)
                DrawPixel(bitmap.GetPixel(i, j), i - srcRec.X + destRec.X, j - srcRec.Y + destRec.Y);
    }

    // Added for the scanline-accurate PPU redesign
    // (see NES_PPU::RenderBackgroundScanline()): the Rect-based DrawImage()
    // above additively blends (via DrawPixel/add()), which is exactly right
    // for compositing sprites/background onto a frame that already has
    // other layers drawn into it, but wrong for writing a *persistent*
    // per-scanline background buffer that's rebuilt fresh every real frame
    // - blending onto last frame's stale pixel there would average old and
    // new colors instead of replacing them. Same Rect-based cropped-copy
    // shape as SameSize() above, just via the plain overwrite SetPixel()
    // already used by the 2-arg DrawNewImage() rather than the additive
    // DrawPixel(). Deliberately doesn't need a NotSameSize()-style resizing
    // path - every call site controls both Rects itself and always passes
    // matching dimensions.
    void Picture::DrawNewImage(const Picture& bitmap, Rect destRec, Rect srcRec)
    {
        for (int i = srcRec.X; i < srcRec.Width + srcRec.X; i++)
            for (int j = srcRec.Y; j < srcRec.Height + srcRec.Y; j++)
                SetPixel(bitmap.GetPixel(i, j), i - srcRec.X + destRec.X, j - srcRec.Y + destRec.Y);
    }

    std::vector<Color> Picture::ResizeArray(const std::vector<Color>& original, int origW, int origH, int rows, int cols)
    {
        std::vector<Color> result(static_cast<size_t>(rows) * static_cast<size_t>(cols));
        int minRows = std::min(rows, origW);
        int minCols = std::min(cols, origH);
        for (int i = 0; i < minRows; i++)
            for (int j = 0; j < minCols; j++)
                result[static_cast<size_t>(i) + static_cast<size_t>(j) * rows] = original[static_cast<size_t>(i) + static_cast<size_t>(j) * origW];
        return result;
    }

    Color Picture::add(Color c1, Color c2)
    {
        uint8_t a = std::max(c1.A, c2.A);
        return Color(AvarageColor(c1, c2, 'R'), AvarageColor(c1, c2, 'G'), AvarageColor(c1, c2, 'B'), a);
    }

    // NOTE: for a non-opaque, non-zero alpha this treats `c2.A`
    // (0-255) as if it were already a 0..1 blend fraction, so the formula is
    // dimensionally wrong for any alpha strictly between 0 and 255. It's
    // dormant because every Color ever constructed has A==0
    // (Color::Transparent(), the default) or A==255 (every named/palette
    // colour) - both of which this formula happens to handle correctly (A=0
    // degenerates to `c1`'s channel, A=255 takes the early-return branch).
    uint8_t Picture::AvarageColor(Color c1, Color c2, char channel)
    {
        if (c2.A == 255)
        {
            switch (channel)
            {
                case 'R': return c2.R;
                case 'G': return c2.G;
                case 'B': return c2.B;
            }
        }
        switch (channel)
        {
            case 'R': return static_cast<uint8_t>(c2.A * c2.R + (1 - c2.A) * c1.R);
            case 'G': return static_cast<uint8_t>(c2.A * c2.G + (1 - c2.A) * c1.G);
            case 'B': return static_cast<uint8_t>(c2.A * c2.B + (1 - c2.A) * c1.B);
        }
        return 255;
    }

    int Picture::Range(int value)
    {
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        return value;
    }

    void Picture::RotateFlip(RotateFlipType type)
    {
        switch (type)
        {
            case RotateFlipType::RotateNoneFlipNone:
                break;
            case RotateFlipType::Rotate90FlipNone:
                Rotate90FlipNone();
                break;
            case RotateFlipType::RotateNoneFlipXY:
                RotateNoneFlipX();
                RotateNoneFlipY();
                break;
            case RotateFlipType::Rotate270FlipNone:
                Rotate90FlipNone();
                RotateFlip(RotateFlipType::RotateNoneFlipXY);
                break;
            case RotateFlipType::RotateNoneFlipX:
                RotateNoneFlipX();
                break;
            case RotateFlipType::Rotate90FlipX:
                Rotate90FlipNone();
                RotateNoneFlipX();
                break;
            case RotateFlipType::Rotate270FlipX:
                Rotate90FlipNone();
                RotateNoneFlipY();
                break;
            case RotateFlipType::RotateNoneFlipY:
                RotateNoneFlipY();
                break;
        }
    }

    void Picture::Rotate90FlipNone()
    {
        std::vector<Color> temp = img;
        for (int x = 0; x < width; x++)
            for (int y = 0; y < height; y++)
                SetPixel(temp[static_cast<size_t>(y) + static_cast<size_t>(x) * width], x, y);
    }

    void Picture::RotateNoneFlipY()
    {
        std::vector<Color> temp = img;
        for (int x = 0; x < width; x++)
            for (int y = 0; y < height; y++)
                SetPixel(temp[static_cast<size_t>(x) + static_cast<size_t>(height - y - 1) * width], x, y);
    }

    void Picture::RotateNoneFlipX()
    {
        std::vector<Color> temp = img;
        for (int x = 0; x < width; x++)
            for (int y = 0; y < height; y++)
                SetPixel(temp[static_cast<size_t>(width - x - 1) + static_cast<size_t>(y) * width], x, y);
    }
}
