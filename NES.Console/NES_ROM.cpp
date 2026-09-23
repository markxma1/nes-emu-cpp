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
#include "NES_ROM.h"
#include "INES.h"
#include "MapperFactory.h"
#include "NES_PPU_Memory.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace NES
{
    std::vector<uint8_t> NES_ROM::b;
    int NES_ROM::end = 0;
    std::unique_ptr<Mapper> NES_ROM::currentMapper;

    void NES_ROM::LoadRom(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file)
            throw std::runtime_error("NES_ROM::LoadRom: could not open " + filePath);
        auto size = file.tellg();
        file.seekg(0);
        b.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(b.data()), size);

        INES::ReadeHeader(b);

        // FIXED (found via real in-app ROM switching, not a fresh launch -
        // "Monster Truck Rally sauber, aber Grafik-Mischmasch sobald man
        // scrollt, nachdem man vorher ein anderes Spiel hatte"): per
        // http://wiki.nesdev.com/w/index.php/Mirroring, a cartridge's
        // nametable mirroring is fixed by its own PCB wiring (the iNES
        // header bit this ROM's INES::ReadeHeader() just parsed into
        // INES::arrangement above) - but NES_PPU_Memory's *physical* wiring
        // of that arrangement (RewireNameTableMirroring(), see its own
        // LEARNING NOTE) was only ever applied once, from the
        // NES_PPU_Memory constructor at program startup. A mapper with its
        // own runtime mirroring register (MMC1/MMC3/AxROM) re-applies it
        // itself whenever that register changes, but NROM/UxROM/CNROM never
        // touch it again - so switching from any ROM into one of those
        // three via the in-app ROM picker (NES/main.cpp's switchRom()) left
        // the *previous* ROM's mirroring wiring in place. Both ROMs'
        // nametables still held their own correct tile IDs (CreatePhysicalNameTableBanks()'s
        // permanent per-bank storage was never touched), so a freshly loaded
        // title screen usually looked fine - it just doesn't yet scroll
        // across the mirror boundary; the corruption only became visible
        // once real gameplay scrolling crossed into a nametable half wired
        // to the wrong physical bank. Fixed by re-applying the freshly
        // parsed header's arrangement on every load, before the mapper's
        // own Install() (below) gets a chance to override it with its own
        // register-driven mirroring, exactly like a real cartridge swap
        // would.
        NES_PPU_Memory::RewireNameTableMirroring();

        int prgStart = 0x10 + (INES::trainer ? 512 : 0);
        int prgEnd = prgStart + INES::PRGROMSize;
        int chrEnd = prgEnd + INES::CHRROMSize;
        end = chrEnd; // ReadeTitle() below still expects this

        if (static_cast<size_t>(chrEnd) > b.size())
        {
            throw std::runtime_error("NES_ROM::LoadRom: file is smaller than its header's declared "
                                      "PRG+CHR-ROM size - truncated/corrupt ROM?");
        }

        std::vector<uint8_t> prgRom(b.begin() + prgStart, b.begin() + prgEnd);
        std::vector<uint8_t> chrRom;
        if (INES::CHRROMSize > 0)
            chrRom.assign(b.begin() + prgEnd, b.begin() + chrEnd);
        // else: CHRROMSize == 0 means CHR-RAM - chrRom stays empty, see
        // Mapper::HasChrRam().

        int mapperNumber = INES::Lmapper | (INES::Hmapper << 4);
        try
        {
            currentMapper = CreateMapper(mapperNumber);
        }
        catch (const std::runtime_error&)
        {
            // Some real-world ROM dumps have an incorrect mapper byte in
            // their header (hand-edited/mislabeled - not uncommon on ROMs
            // from unofficial sources) while their actual PRG/CHR size
            // would fit plain NROM regardless of what the header claims.
            // Rather than hard-failing a ROM that would otherwise load and
            // run fine, fall back to NROM when the size makes that
            // plausible, and say so - only re-throw (a genuinely
            // unsupported mapper) when the size doesn't fit.
            if (prgRom.size() <= 32768 && chrRom.size() <= 8192)
            {
                std::cerr << "Warning: ROM declares mapper " << mapperNumber
                           << ", which isn't supported, but its PRG/CHR-ROM size fits plain NROM - "
                              "loading as NROM instead of failing." << std::endl;
                currentMapper = CreateMapper(0);
            }
            else
            {
                throw;
            }
        }
        currentMapper->Install(std::move(prgRom), std::move(chrRom));

        ReadeTitle();
    }

    void NES_ROM::ReadeTitle()
    {
        if (static_cast<size_t>(end) < b.size())
            INES::title.assign(b.begin() + end, b.end());
        else
            INES::title.clear();
    }

    // See this function's own header comment.
    // FNV-1a over the raw .nes file bytes: simple, dependency-free, and
    // more than sufficient to catch "wrong ROM" (the only thing this is
    // for) rather than being a cryptographic guarantee.
    uint64_t NES_ROM::RomHash()
    {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (uint8_t byte : b)
        {
            hash ^= byte;
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }
}
