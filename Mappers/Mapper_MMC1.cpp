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
#include "Mapper_MMC1.h"
#include "INES.h"
#include "NES_PPU_Memory.h"
#include "Interrupt.h"

namespace NES
{
    void Mapper_MMC1::SerializeState(std::vector<uint8_t>& out) const
    {
        out.push_back(shiftRegister);
        out.push_back(static_cast<uint8_t>(writeCount));
        out.push_back(control);
        out.push_back(chrBank0);
        out.push_back(chrBank1);
        out.push_back(prgBank);
    }

    void Mapper_MMC1::DeserializeState(const uint8_t*& in, const uint8_t* end)
    {
        if (end - in < 6)
            return; // truncated/foreign save data - leave power-on-default state alone
        shiftRegister = *in++;
        writeCount = *in++;
        control = *in++;
        chrBank0 = *in++;
        chrBank1 = *in++;
        prgBank = *in++;
        // Re-derive the *visible* PRG/CHR windows and mirroring from the
        // restored registers, matching what WriteControl() already does
        // after any real register write - NES_SaveState separately
        // restores NES_Memory::Memory/NES_PPU_Memory::PatternTable's raw
        // bytes too, but re-applying here keeps this mapper's own notion
        // of "currently visible window" consistent with those registers
        // for the *next* real write, not just the moment right after load.
        ApplyMirroring();
        ApplyPrgBanks();
        ApplyChrBanks();
    }

    void Mapper_MMC1::OnInstall()
    {
        shiftRegister = 0;
        writeCount = 0;
        control = 0x0C;
        chrBank0 = 0;
        chrBank1 = 0;
        prgBank = 0;
        ApplyMirroring();
        ApplyPrgBanks();
        ApplyChrBanks();
    }

    // FIXED (new design, not a C# port or a real-hardware behavior - a
    // deliberate, documented deviation from strict hardware fidelity; see
    // Interrupt::SuppressNMI()'s own comment for the full story): a save
    // state captured live from Chip and Dale reproduced a residual
    // recurrence of this exact game's already-documented shift-register
    // corruption bug (NES_Console::RenderFrame()'s comment) - an NMI landing
    // between two writes of the game's own $FF04 coroutine-yield routine's
    // 5-write MMC1 bank-switch burst, interleaving with either that burst or
    // the NMI handler's own MMC1 writes and corrupting shiftRegister/
    // writeCount, which committed a garbage register value and left the
    // game's cooperative task scheduler permanently stuck waiting on a flag
    // the corrupted write path never went on to clear. Guarding the burst
    // (deferring NMI delivery, never dropping it - see SuppressNMI()) closes
    // this without needing full per-cycle/mid-instruction CPU accuracy.
    void Mapper_MMC1::WriteRegister(uint16_t address, uint8_t value)
    {
        if (value & 0x80)
        {
            // Reset: also forces PRG mode back to 3 (fix last bank at
            // $C000) per nesdev - without this, a game mid-way through a
            // write sequence when it resets could end up "stuck" believing
            // $C000 is switchable when the CPU vectors it needs actually
            // live in a bank it never re-selects.
            shiftRegister = 0;
            writeCount = 0;
            control |= 0x0C;
            ApplyPrgBanks();
            Interrupt::SuppressNMI(false); // reset also ends any in-progress burst
            return;
        }

        if (writeCount == 0)
            Interrupt::SuppressNMI(true); // entering a fresh 5-write burst

        shiftRegister = static_cast<uint8_t>((shiftRegister >> 1) | ((value & 0x01) << 4));
        ++writeCount;
        if (writeCount < 5)
            return;

        uint8_t regValue = shiftRegister & 0x1F;
        switch ((address >> 13) & 0x03) // which $2000-sized quarter the 5th write landed in
        {
            case 0: WriteControl(regValue); break;
            case 1: WriteChrBank0(regValue); break;
            case 2: WriteChrBank1(regValue); break;
            case 3: WritePrgBank(regValue); break;
        }
        shiftRegister = 0;
        writeCount = 0;
        Interrupt::SuppressNMI(false); // burst complete, safe to deliver NMI again
    }

    void Mapper_MMC1::WriteControl(uint8_t value)
    {
        control = value;
        ApplyMirroring();
        ApplyPrgBanks();
        ApplyChrBanks();
    }

    void Mapper_MMC1::WriteChrBank0(uint8_t value)
    {
        chrBank0 = value;
        ApplyChrBanks();
    }

    void Mapper_MMC1::WriteChrBank1(uint8_t value)
    {
        chrBank1 = value;
        ApplyChrBanks();
    }

    void Mapper_MMC1::WritePrgBank(uint8_t value)
    {
        prgBank = value;
        ApplyPrgBanks();
    }

    // FIXED (was a preserved C# bug, now corrected - found live while
    // investigating Chip and Dale's background-doesn't-match-the-real-level
    // bug, and confirmed against real documentation before touching it, per
    // this project's own standing rule): per
    // https://www.nesdev.org/wiki/MMC1#Control_(internal,_$8000-$9FFF),
    // the control register's mirroring bits (control & 0x03) are "0:
    // one-screen, lower bank; 1: one-screen, upper bank; 2: vertical; 3:
    // horizontal" - value 2 is vertical, value 3 is horizontal. This had
    // them swapped (2 -> horisontal, 3/default -> vertical), so *every*
    // MMC1 game's real mirroring mode was inverted the moment it wrote its
    // own actual value here (almost immediately, at boot - MMC1's
    // OnInstall() only ever sets a placeholder `control = 0x0C` before the
    // game's own init code writes the real value). With the wrong
    // arrangement, NES_PPU_Memory::RewireNameTableMirroring() (called right
    // below) aliases the four logical nametable slots to the wrong physical
    // VRAM banks - exactly why the on-screen background could visually
    // desync from the level the game's own (correct) collision logic
    // tracks: RenderBackgroundScanline() was faithfully rendering *some*
    // valid nametable data, just not the physical bank the game actually
    // intended for that logical quadrant.
    void Mapper_MMC1::ApplyMirroring()
    {
        switch (control & 0x03)
        {
            case 0: INES::arrangement = INES::Mirror::single_screen_a; break;
            case 1: INES::arrangement = INES::Mirror::single_screen_b; break;
            case 2: INES::arrangement = INES::Mirror::vertical; break;
            default: INES::arrangement = INES::Mirror::horisontal; break;
        }
        NES_PPU_Memory::RewireNameTableMirroring();
    }

    void Mapper_MMC1::ApplyPrgBanks()
    {
        size_t numBanks16k = prg.size() / 16384;
        uint8_t bank = prgBank & 0x0F;
        int prgMode = (control >> 2) & 0x03;

        if (prgMode <= 1)
        {
            // 32KB mode: switch the whole $8000-$FFFF window at once,
            // ignoring the bank register's low bit.
            size_t numBanks32k = numBanks16k / 2 == 0 ? 1 : numBanks16k / 2;
            size_t bank32 = (bank >> 1) % numBanks32k;
            WritePrgWindow(0x8000, bank32 * 32768, 32768);
        }
        else if (prgMode == 2)
        {
            // Fix first bank at $8000, switch 16KB at $C000.
            WritePrgWindow(0x8000, 0, 16384);
            WritePrgWindow(0xC000, (static_cast<size_t>(bank) % numBanks16k) * 16384, 16384);
        }
        else
        {
            // Fix last bank at $C000, switch 16KB at $8000.
            WritePrgWindow(0x8000, (static_cast<size_t>(bank) % numBanks16k) * 16384, 16384);
            WritePrgWindow(0xC000, (numBanks16k - 1) * 16384, 16384);
        }
    }

    void Mapper_MMC1::ApplyChrBanks()
    {
        if (HasChrRam())
            return;
        if (control & 0x10)
        {
            // Two separate 4KB banks.
            size_t numBanks4k = chr.size() / 4096;
            WriteChrWindow(0x0000, (static_cast<size_t>(chrBank0) % numBanks4k) * 4096, 4096);
            WriteChrWindow(0x1000, (static_cast<size_t>(chrBank1) % numBanks4k) * 4096, 4096);
        }
        else
        {
            // 8KB at a time - low bit of chrBank0 ignored.
            size_t numBanks8k = chr.size() / 8192 == 0 ? 1 : chr.size() / 8192;
            size_t bank8 = (chrBank0 >> 1) % numBanks8k;
            WriteChrWindow(0x0000, bank8 * 8192, 8192);
        }
    }
}
