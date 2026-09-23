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
#include <string>

namespace NES
{
    /// @brief Save/load a full emulation snapshot to/from a local file.
    /// Lets you bookmark a moment right before/after a visible bug, without
    /// having to replay the same input sequence by hand every time, to make
    /// testing practical.
    ///
    /// Captures: CPU registers (NES_Register), the full 64KB CPU address
    /// space (NES_Memory::Memory - this already covers PPUCTRL/PPUMASK/
    /// PPUSTATUS/OAMADDR/etc., since those are just AddressSetup cells
    /// inside it, and covers every mapper's *currently visible* PRG/CHR
    /// window too, since a mapper repaints those cells directly - see
    /// Mapper::WritePrgWindow()/WriteChrWindow()), PPU palette RAM, all 4
    /// physical nametable/attribute banks (not just the logical
    /// NameTableN() view - see NES_PPU_Memory::NameTablePhysicalBanks()'s
    /// own comment for why), CHR-RAM content, OAM, PPUPCADDR/the $2006
    /// write-latch, live scroll position, the $2005/$2006 shared write
    /// toggle, pending NMI/IRQ/BRK, and each mapper's own persistent
    /// register state (see Mapper::SerializeState()'s own comment for why
    /// most mappers need none).
    ///
    /// NOT captured (accepted simplifications for a testing/debugging
    /// tool, not a byte-perfect resume): APU/audio state (may glitch or
    /// reset briefly right after a load), the exact mid-scanline
    /// currentDot()/currentScanline() position (resumes from wherever
    /// AdvanceDots() currently is - self-corrects within one real frame,
    /// same as how a real console's PPU is always "somewhere" mid-frame),
    /// the Draw()-latched xScrollTemp/yScrollTemp debug-overlay-only values
    /// (self-corrects on the next Display() call), and the old cached
    /// Tile()/patternArray debug-tooling cache's dirty flags (self-corrects
    /// the moment anything touches that palette/pattern entry again).
    /// Loading a save made with a *different* ROM than the one currently
    /// running is refused (see Load()'s own comment).
    class NES_SaveState
    {
    public:
        /// Writes a snapshot of the current emulation state to `path`.
        /// Returns false (does not throw) on any I/O failure, so a caller
        /// (NES/main.cpp) can report it without crashing a running game.
        static bool Save(const std::string& path);

        /// Restores a snapshot previously written by Save(). Returns false
        /// (leaves current state untouched) if the file can't be read, is
        /// truncated/corrupt, has the wrong format version, or was saved
        /// under a different ROM (checked via a hash of the currently
        /// loaded PRG-ROM bytes, stored in the file) - a save file only
        /// ever makes sense loaded back into the exact game it came from.
        static bool Load(const std::string& path);
    };
}
