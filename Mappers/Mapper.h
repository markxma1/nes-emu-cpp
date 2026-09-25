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
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include <vector>

namespace NES
{
    /// @brief Base interface for one NES cartridge mapper chip - the
    /// hardware on the cartridge board that decides which PRG-ROM/CHR-ROM
    /// bytes are currently visible to the CPU/PPU (and, for some boards,
    /// which physical nametable page the PPU currently shows).
    ///
    /// Earlier versions never dispatched on a mapper number at all and
    /// always treated every ROM as plain NROM (see NES_ROM.cpp's LEARNING
    /// NOTE, now fixed by this folder). Each concrete mapper below (Mapper_NROM.h,
    /// Mapper_MMC1.h, ...) is built fresh against its own cited nesdev page.
    ///
    /// http://wiki.nesdev.com/w/index.php/Mapper - background on what a
    /// mapper chip actually is and why cartridges need one at all: the CPU
    /// only has 32KB of address space for PRG-ROM ($8000-$FFFF) and the PPU
    /// only 8KB for CHR-ROM ($0000-$1FFF), but games often ship far more
    /// data than that - a mapper's registers (almost always driven by CPU
    /// writes to the PRG-ROM address range itself, since a cartridge's
    /// PRG-ROM chip doesn't drive the bus on a write) let the game swap
    /// which slice of its own ROM is currently mapped into those windows.
    ///
    /// A concrete mapper owns the *entire* raw PRG-ROM/CHR-ROM byte arrays
    /// from the .nes file for as long as it's installed (not just a fixed
    /// 32KB/8KB window the old NROM-only loader could hold), and repaints
    /// NES_Memory::Memory[$8000-$FFFF] / NES_PPU_Memory::PatternTable
    /// directly - via AddressSetup::value(), which (unlike Value()) never
    /// re-triggers a hook - whenever a CPU write to one of its bank-select
    /// registers changes which window into that data should currently be
    /// visible. See WritePrgWindow()/WriteChrWindow() below.
    class Mapper
    {
    public:
        virtual ~Mapper();

        /// Takes ownership of the ROM's raw PRG-ROM/CHR-ROM bytes, applies
        /// this mapper's power-on-default bank configuration, and installs
        /// an AddressSetup::AfterSet hook on every address in $8000-$FFFF
        /// that routes the write to WriteRegister() - real cartridges don't
        /// decode individual PRG-ROM address lines for this, any write
        /// anywhere in that range reaches the mapper's register logic (see
        /// each concrete mapper for which bits of the address it actually
        /// cares about). `chrRom` is empty when the board has no CHR-ROM at
        /// all (a CHR-RAM board - see HasChrRam()); never call Install()
        /// with an empty chrRom for a board that isn't CHR-RAM, and vice
        /// versa (NES_ROM.cpp only ever passes CHR-ROM bytes when
        /// INES::CHRROMSize > 0).
        void Install(std::vector<uint8_t> prgRom, std::vector<uint8_t> chrRom);

        /// Called once per real visible scanline (0-239), from
        /// NES_PPU::OnScanlineStart() (via a callback registered by
        /// NES_Console::INIT() - see NES_PPU.h's own comment on
        /// SetScanlineCallback() for why the indirection: NES_PPU has no
        /// compile-time dependency on NES_ROM/Mapper at all, and this keeps
        /// it that way). A no-op for every mapper except MMC3 (see
        /// Mapper_MMC3::OnScanline()'s own comment for why this exists and
        /// what it approximates) - default implementation here so
        /// NES_Console doesn't need to know or care which mappers actually
        /// use it.
        ///
        /// UPDATE (see git history/this project's own scanline-accurate-PPU
        /// redesign notes): this used to be OnFrame(), called once per whole rendered frame right
        /// before NES_PPU::Display() composited it - now called once per
        /// real scanline instead, since composing a frame is no longer a
        /// single end-of-frame snapshot (see NES_PPU::AdvanceDots()).
        virtual void OnScanline() {}

        /// Human-readable list of the current CHR bank registers (debug viewer).
        virtual std::string DescribeChrBanks() const { return ""; }

        // New: user-requested save/load-state feature
        // (see NES_SaveState's own comment for the full story). Most
        // mappers here (NROM/UxROM/CNROM/AxROM) have no persistent
        // bank-index state at all - WriteRegister() computes a bank offset
        // straight from the incoming CPU byte and immediately repaints
        // NES_Memory::Memory/NES_PPU_Memory::PatternTable (see each one's
        // own .cpp) - so restoring those memory arrays' raw byte content
        // (which NES_SaveState already does unconditionally) already puts
        // back the exact PRG/CHR window a save captured, with nothing
        // mapper-specific left to restore. Only MMC1 and MMC3 keep real
        // internal register state (bank-select latches, the MMC3 IRQ
        // counter) that a *subsequent* register write would recompute
        // banks from - if that state isn't restored too, the very next
        // bank switch after a load would silently revert to whatever
        // power-on-default state OnInstall() left behind. Default no-op
        // covers every mapper that doesn't need this.
        /// Appends this mapper's internal register state to `out` for save states (default: nothing).
        virtual void SerializeState(std::vector<uint8_t>& out) const { (void)out; }
        /// Restores state written by SerializeState(), advancing `in` (never reading past `end`).
        virtual void DeserializeState(const uint8_t*& in, const uint8_t* end) { (void)in; (void)end; }

    protected:
        /// PRG ROM contents of the cartridge.
        std::vector<uint8_t> prg;
        /// CHR data of the cartridge (empty when the board uses CHR RAM).
        std::vector<uint8_t> chr;

        /// True if the cartridge has no CHR ROM and therefore uses CHR RAM.
        bool HasChrRam() const { return chr.empty(); }

        /// Applies this mapper's power-on-default bank configuration (e.g.
        /// bank 0 everywhere). Called once, from Install(), after prg/chr
        /// are populated.
        virtual void OnInstall() = 0;

        /// Handles a CPU write anywhere in $8000-$FFFF - almost always a
        /// mapper register write (see Install()'s comment on why every
        /// address in that range reaches here, not just specific ones).
        virtual void WriteRegister(uint16_t address, uint8_t value) = 0;

        /// Copies `length` bytes starting at `prg[romByteOffset]` (wrapping
        /// if the requested window runs past the end of `prg` - real
        /// hardware wraps/mirrors when a bank-select register has more bits
        /// than the cartridge actually populates with ROM) into
        /// NES_Memory::Memory starting at CPU address `cpuStart`. Not const:
        /// records the [cpuStart, cpuStart+length) range so Install()'s
        /// write hook (see its FIXED note) can tell whether a given register
        /// write's own address was actually repainted by this call.
        void WritePrgWindow(int cpuStart, size_t romByteOffset, int length);

        /// Same as WritePrgWindow(), but into NES_PPU_Memory::PatternTable
        /// (PPU $0000-$1FFF) from `chr`. A no-op on a CHR-RAM board
        /// (HasChrRam()) - CHR-RAM's contents come from the CPU writing
        /// $2007 during gameplay, already handled generically by the
        /// existing PPUDATA plumbing (NES_PPU_Register.cpp); a mapper has
        /// nothing of its own to paint there. Doesn't need the same
        /// same-address-repaint tracking as WritePrgWindow(): CHR writes
        /// never land back in the CPU's own $8000-$FFFF address space, so
        /// they can never re-corrupt the very cell a register write just
        /// came in on.
        void WriteChrWindow(int ppuStart, size_t romByteOffset, int length) const;

    public:
        /// Forget which ROM bytes are currently mapped, so the next bank writes copy everything again
        /// (call after anything that may have changed the mapped cells, e.g. loading a save state).
        void InvalidateBankCache()
        {
            prgWindowCache.clear();
            chrWindowCache.clear();
        }

    private:
        /// What a window currently shows: ROM offset and length. A bank write that asks for the same
        /// thing again is skipped instead of copying kilobytes cell by cell.
        struct WindowCache
        {
            size_t offset;
            int length;
        };
        std::unordered_map<int, WindowCache> prgWindowCache;
        mutable std::unordered_map<int, WindowCache> chrWindowCache;
        /// Pointer to the current 1 KB piece of PRG / CHR ROM for every 1 KB of CPU / PPU address space.
        /// The mapped memory cells read through these (see AddressSetup::MapRom()).
        const uint8_t* prgSlots[64] = {};
        mutable const uint8_t* chrSlots[8] = {};
        bool prgMapped[64] = {};
        mutable bool chrMapped[8] = {};
        void UnmapAllRom();
        /// Set of CPU-address ranges WritePrgWindow() repainted during the
        /// *current* WriteRegister() dispatch - see Install()'s FIXED note.
        /// Cleared at the start of each dispatch.
        std::vector<std::pair<int, int>> prgWindowsTouchedThisWrite;

        bool PrgWindowCoveredAddress(int address) const;
    };
}
