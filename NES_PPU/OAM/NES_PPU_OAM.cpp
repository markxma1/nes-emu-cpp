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
#include "NES_PPU_OAM.h"
#include "NES_Memory.h"
#include "NES_CPU.h"

namespace NES
{
    std::vector<std::shared_ptr<AddressSetup>> NES_PPU_OAM::Memory;
    std::vector<std::shared_ptr<AddressSetup>> NES_PPU_OAM::SpriteYc;
    std::vector<NES_PPU_OAM::Byte1> NES_PPU_OAM::SpriteTile;
    std::vector<NES_PPU_OAM::Byte2> NES_PPU_OAM::SpriteAttribute;
    std::vector<std::shared_ptr<AddressSetup>> NES_PPU_OAM::SpriteXc;

    NES_PPU_OAM::NES_PPU_OAM()
    {
        Memory.clear();
        for (int i = 0; i <= 0xFF; i++)
            Memory.push_back(std::make_shared<AddressSetup>(i));
        InitBytes();
    }

    void NES_PPU_OAM::InitBytes()
    {
        InitArrays();
        for (int i = 0; i < 0xff; i += 4)
        {
            SpriteYc.push_back(Memory[i]);
            InitSpriteTile(i);
            InitSpriteAttribute(i);
            SpriteXc.push_back(Memory[i + 3]);
        }
    }

    void NES_PPU_OAM::InitArrays()
    {
        SpriteYc.clear();
        SpriteTile.clear();
        SpriteAttribute.clear();
        SpriteXc.clear();
    }

    void NES_PPU_OAM::InitSpriteAttribute(int i)
    {
        Byte2 temp;
        temp.adress = Memory[i + 2];
        SpriteAttribute.push_back(temp);
    }

    void NES_PPU_OAM::InitSpriteTile(int i)
    {
        Byte1 temp;
        temp.adress = Memory[i + 1];
        SpriteTile.push_back(temp);
    }

    // FIXED (very likely *the* primary cause of sprites looking
    // scrambled/stale/wrong, including the "eine Halfte ist richtig, andere
    // falsch" report and newly-spawned objects like the player ship never
    // visibly appearing after "STAGE 1"): per
    // http://wiki.nesdev.com/w/index.php/PPU_registers ("OAM DMA"), writing
    // $4014 "instantly" copies 256 *bytes* from CPU page $XX00-$XXFF into
    // OAM - a value copy, physically separate PPU-side memory from that
    // point on, completely decoupled from whatever the CPU RAM page holds
    // afterward.
    //
    // This previously (`Memory[i] = NES_Memory.Memory[(XX << 8) + i];`, with
    // `AddressSetup` behind a `shared_ptr`, i.e. reference semantics)
    // reassigned each OAM slot to *alias the same object* as the
    // corresponding CPU RAM cell, rather than copying its value into OAM's
    // own permanently-owned cells. So "OAM" kept live-reflecting every
    // subsequent CPU write to that RAM page - normally the game's own
    // sprite-staging buffer, which it goes on writing/reusing for the
    // *next* frame (or other scratch work) immediately after the DMA call
    // - instead of holding a stable snapshot until the next OAMDMA. Given
    // the CPU thread runs far faster than the ~60Hz display loop that
    // actually reads OAM to render, by the time a frame gets drawn, OAM
    // could easily be "seeing" RAM contents from several CPU-frames later
    // than the one it was supposed to render - explaining sprites that look
    // right some of the time (by coincidence) and wrong/stale/uniform
    // (e.g. all sharing one leftover byte value) the rest of the time.
    //
    // Also fixes an off-by-one (`i < 0xFF`, 255 iterations, never copying
    // the 256th byte / OAM's last sprite's X coordinate) to `i <= 0xFF`.
    //
    // Fixed by copying the *value* into OAM's own already-allocated
    // AddressSetup cells (constructed once in the NES_PPU_OAM() constructor)
    // instead of replacing the shared_ptr itself.
    // FIXED (real bug, a long-documented explicit non-goal of this port's
    // whole scanline-accurate PPU redesign, now actually closed - found
    // directly relevant while root-causing a real CPU-state divergence
    // from a reference emulator for the same recorded input, first visible
    // in stack bytes by frame 13-16): per
    // http://wiki.nesdev.com/w/index.php/PPU_OAM ("DMA"), a $4014 write
    // doesn't just copy 256 bytes "instantly" from the CPU's own
    // perspective - it *stalls the CPU* for 513 cycles (514 if the write
    // happens to land on an odd CPU cycle), during which nothing else the
    // 6502 does can be observed. This port's cycle-atomic model already
    // needed a similar per-instruction "dynamic extra cycles" mechanism for
    // branch/page-crossing undercounting (see NES_CPU.cpp's own
    // kPageCrossSensitiveOpcode comment for that fix and the full story of
    // *why* this matters: undercounted cycles shift exactly when an
    // NMI/mapper IRQ fires relative to the CPU instruction stream) - reused
    // here via NES_CPU::pendingExtraCycles, added into whatever
    // kCycleTable-based cost the actual $4014-writing instruction (a 4-cycle
    // STA absolute, in every real game this project has tested) already
    // reports, using NES_CPU::totalCyclesEver's parity *as of just before
    // this instruction started* as the real hardware's odd/even-cycle rule
    // needs (see NES_CPU.h's own comment on totalCyclesEver for why "just
    // before" is a reasonable approximation of "at the exact bus cycle the
    // write lands on" for this port's instruction-atomic model - this
    // game's own $4014 write is always a 4-cycle STA, an even count, so the
    // two would agree regardless).
    void NES_PPU_OAM::OAMDMA(uint8_t XX)
    {
        for (int i = 0; i <= 0xFF; i++)
            Memory[static_cast<size_t>(i)]->value(NES_Memory::Memory[static_cast<size_t>((XX << 8) + i)]->Value());
        InitBytes();

        bool onOddCycle = (NES_CPU::totalCyclesEver.load(std::memory_order_relaxed) % 2) != 0;
        NES_CPU::pendingExtraCycles += onOddCycle ? 514 : 513;
    }
}
