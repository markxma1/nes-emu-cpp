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
#include "Color.h"
#include <vector>
#include <opencv2/core.hpp>

namespace NES_PPU
{
    /// RotateFlip modes actually reached by the ported code (RotateNoneFlipX /
    /// RotateNoneFlipY, used to flip sprite tiles). The other System.Drawing
    /// RotateFlipType values are kept for completeness even
    /// though nothing currently calls them.
    enum class RotateFlipType
    {
        RotateNoneFlipNone,
        Rotate90FlipNone,
        RotateNoneFlipXY,
        Rotate270FlipNone,
        RotateNoneFlipX,
        Rotate90FlipX,
        Rotate270FlipX,
        RotateNoneFlipY
    };

    /// Simple (x, y, width, height) rectangle - System.Drawing.Rectangle's
    /// shape, used by the two-Rectangle DrawImage overload (scrolled blit).
    struct Rect
    {
        int X = 0;      ///< Left edge.
        int Y = 0;      ///< Top edge.
        int Width = 0;  ///< Width (some callers pass a right-edge coordinate instead, see DrawImage()).
        int Height = 0; ///< Height (or bottom-edge coordinate, see Width).
    };

    /// @brief Software framebuffer: a plain pixel grid plus an "info" overlay
    /// layer used for debug outlines.
    ///
    /// This is a UI/graphics primitive, not core emulation logic -
    /// there is no platform bitmap class in plain C++, so this
    /// stores pixels itself (`std::vector<Color>`, row-major) instead of
    /// wrapping a platform bitmap, and only touches OpenCV at the very edge
    /// (`Image()`, building a cv::Mat for display) - everything else here is
    /// plain, readable pixel math.
    class Picture
    {
    public:
        /// Creates a blank width x height picture (all pixels default-constructed Color).
        Picture(int width, int height);
        /// Copy constructor: copies pixels, info layer and mirror settings.
        Picture(const Picture& other);

        /// True if DrawMirror() has enabled mirrored reads.
        bool HaveMirror() const { return haveMirror; }
        /// Enables or disables mirrored reads.
        void HaveMirror(bool v) { haveMirror = v; }

        /// Width in pixels.
        int Width() const { return width; }
        /// Height in pixels.
        int Height() const { return height; }

        /// Width and height pair returned by Size().
        struct SizeInfo
        {
            int Width;  ///< Width in pixels.
            int Height; ///< Height in pixels.
        };
        /// Width and height together.
        SizeInfo Size() const { return { width, height }; }

        /// Returns the visible pixel at (x, y): the info-layer colour if one is set, else the (possibly mirrored) image pixel; Transparent() if out of range.
        Color GetPixel(int x, int y) const;
        /// Sets a pixel on the info (debug outline) layer, which is drawn on top of the image; out-of-range is ignored.
        void SetInfLayerPixel(Color color, int x, int y);
        /// Overwrites the image pixel at (x, y); out-of-range is ignored.
        void SetPixel(Color color, int x, int y);
        /// Additive/alpha blend of `color` onto the existing pixel (see .cpp for
        /// the blend formula, alpha-value quirk included).
        void DrawPixel(Color color, int x, int y);

        /// Renders to a BGR cv::Mat (OpenCV's native channel order) - the one
        /// point where this class talks to OpenCV, only for handing frames to
        /// cv::imshow.
        cv::Mat Image() const;

        // NOTE: despite the parameter names, `width`/`height` here
        // are actually treated as the bottom-right *corner* coordinates, not
        // a width/height extent (the loop is `for i=x;i<width` not
        // `i<x+width`, unlike DrawRectangle below). Every call site
        // already passes corner coordinates, so this is a naming
        // oddity rather than a live bug - kept rather than "fixed"
        // to a width/height signature that would break its callers' math.
        /// Fills a rectangle with `color` (see the note above about the corner-coordinate parameters).
        void FillRectangle(Color color, int x, int y, int width, int height);

        /// Blends `bitmap` onto this picture with its top-left at (x, y), pixel by pixel via DrawPixel().
        void DrawImage(const Picture& bitmap, int x, int y);
        /// Resets the picture to its freshly constructed state (all pixels default, no info layer, no mirror)
        /// without reallocating, so a buffer can be reused every frame.
        void Clear();

        /// Copies `bitmap` onto this picture with its top-left at (x, y), overwriting existing pixels (no blending).
        void DrawNewImage(const Picture& bitmap, int x, int y);
        /// Turns on mirrored reads: pixels at or beyond (x, y) read back from the image shifted by (x, y).
        void DrawMirror(int x, int y);
        /// Draws a one-pixel rectangle outline of the given size at (x, y) on the image.
        void DrawRectangle(Color color, int x, int y, int width, int height);
        /// Like DrawRectangle(), but on the info layer, so it can be shown as a debug outline over the image.
        void DrawInfoRectangle(Color color, int x, int y, int width, int height);
        /// Blends the `srcRec` area of `bitmap` into the `destRec` area, resizing if the sizes differ.
        void DrawImage(const Picture& bitmap, Rect destRec, Rect srcRec);
        /// See the .cpp definition's own comment:
        /// same cropped-copy shape as the additive-blend DrawImage() above,
        /// but a plain overwrite (like the 2-arg DrawNewImage()) instead of
        /// a blend.
        void DrawNewImage(const Picture& bitmap, Rect destRec, Rect srcRec);

        /// Rotates and/or flips the picture in place according to .
        void RotateFlip(RotateFlipType type);

    private:
        struct Mirror { bool HaveMirror = false; int x = 0; int y = 0; };

        int width;
        int height;
        std::vector<Color> img;       // row-major [x + y*width]
        std::vector<Color> infoLayer; // same layout; Color() (all-zero) = unset; empty until the first SetInfLayerPixel()
        bool infoUsed = false;        // true once a non-default info pixel was stored
        Mirror mirror;
        bool haveMirror = false;

        Color& at(std::vector<Color>& plane, int x, int y) { return plane[static_cast<size_t>(x) + static_cast<size_t>(y) * width]; }
        const Color& at(const std::vector<Color>& plane, int x, int y) const { return plane[static_cast<size_t>(x) + static_cast<size_t>(y) * width]; }
        Color getInfLayerPixel(int x, int y) const { return infoLayer.empty() ? Color() : at(infoLayer, x, y); }
        /// True when reads/writes are plain array accesses: no info layer values and no mirror.
        bool plain() const { return !infoUsed && !haveMirror; }

        const std::vector<Color>& getMatrix() const { return img; }
        static std::vector<Color> ResizeArray(const std::vector<Color>& original, int origW, int origH, int rows, int cols);

        void BlendPlain(const Picture& bitmap, int x, int y);
        static Color add(Color c1, Color c2);
        static uint8_t AvarageColor(Color c1, Color c2, char channel);
        static int Range(int value);

        void SameSize(const Picture& bitmap, Rect destRec, Rect srcRec);
        void NotSameSize(const Picture& bitmap, Rect destRec, Rect srcRec);

        void Rotate90FlipNone();
        void RotateNoneFlipY();
        void RotateNoneFlipX();
    };
}
