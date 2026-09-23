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
#include "Mapper.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NES
{
    /// @brief Loads a .nes (iNES) ROM file and hands its PRG-ROM/CHR-ROM
    /// data to the right Mapper (see the Mappers/ folder) for the header's
    /// declared mapper number. Every ROM is dispatched
    /// by its header's declared mapper number, rather than being treated as
    /// plain NROM (mapper 0) regardless. http://wiki.nesdev.com/w/index.php/INES
    class NES_ROM
    {
    public:
        static void LoadRom(const std::string& filePath);

        /// The currently-installed mapper, or nullptr before any ROM has
        /// been loaded - see NES_Console::INIT()'s scanline-callback
        /// registration, which calls Mapper::OnScanline() through this. A
        /// raw, non-owning pointer: currentMapper itself stays private so
        /// only LoadRom() ever replaces/destroys it.
        static Mapper* CurrentMapper() { return currentMapper.get(); }

        // User-requested save/load-state feature
        // (see NES_SaveState's own comment). A cheap identity check so
        // NES_SaveState::Load() can refuse a save file that was made under
        // a *different* ROM - loading Chip and Dale state into Tetris
        // would corrupt both, not just produce a wrong picture.
        static uint64_t RomHash();

    private:
        static std::vector<uint8_t> b;
        static int end;
        /// Owns the currently-installed mapper's AddressSetup::AfterSet
        /// hooks on $8000-$FFFF for as long as it's alive - see
        /// Mapper::Install()'s comment on why replacing this on the next
        /// LoadRom() call is safe.
        static std::unique_ptr<Mapper> currentMapper;

        static void ReadeTitle();
    };
}
