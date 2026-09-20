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

    // FIXED (was a preserved C# bug, now corrected - very likely *the*
    // primary cause of sprites looking scrambled/stale/wrong, including the
    // "eine Halfte ist richtig, andere falsch" report and newly-spawned
    // objects like the player ship never visibly appearing after "STAGE 1"):
    // per http://wiki.nesdev.com/w/index.php/PPU_registers ("OAM DMA"),
    // writing $4014 "instantly" copies 256 *bytes* from CPU page $XX00-$XXFF
    // into OAM - a value copy, physically separate PPU-side memory from
    // that point on, completely decoupled from whatever the CPU RAM page
    // holds afterward.
    //
    // The C# original (`Memory[i] = NES_Memory.Memory[(XX << 8) + i];`,
    // AddressSetup being a C# `class` = reference type) and this port
    // (`AddressSetup` behind a `shared_ptr`, same reference semantics)
    // instead reassigned each OAM slot to *alias the same object* as the
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
    // Also fixes an off-by-one also present in the C# original
    // (`i < 0xFF`, 255 iterations, never copying the 256th byte / OAM's
    // last sprite's X coordinate) to `i <= 0xFF`.
    //
    // Fixed by copying the *value* into OAM's own already-allocated
    // AddressSetup cells (constructed once in the NES_PPU_OAM() constructor)
    // instead of replacing the shared_ptr itself.
    void NES_PPU_OAM::OAMDMA(uint8_t XX)
    {
        for (int i = 0; i <= 0xFF; i++)
            Memory[static_cast<size_t>(i)]->value(NES_Memory::Memory[static_cast<size_t>((XX << 8) + i)]->Value());
        InitBytes();
    }
}
