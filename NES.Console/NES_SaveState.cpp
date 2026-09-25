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
#include "NES_SaveState.h"
#include "Interrupt.h"
#include "NES_Memory.h"
#include "NES_PPU_Register.h"
#include "NES_ROM.h"
#include "NES_Register.h"
#include "../Mappers/Mapper.h"
#include "../NES_PPU/NES_PPU_Folder/NES_PPU.h"
#include "../NES_PPU/OAM/NES_PPU_OAM.h"
#include <cstring>
#include <fstream>

namespace NES
{
    namespace
    {
        // 8-byte magic + 1-byte format version - if this class's own layout
        // ever needs to change, bump the version and Load() rejects any
        // older file cleanly instead of misreading it as a different shape.
        constexpr char kMagic[8] = { 'N', 'E', 'S', 'S', 'A', 'V', 'E', '1' };

        void AppendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

        void AppendU16(std::vector<uint8_t>& out, uint16_t v)
        {
            out.push_back(static_cast<uint8_t>(v & 0xFF));
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }

        void AppendI32(std::vector<uint8_t>& out, int32_t v)
        {
            auto u = static_cast<uint32_t>(v);
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
        }

        void AppendU64(std::vector<uint8_t>& out, uint64_t v)
        {
            for (int i = 0; i < 8; i++)
                out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }

        void AppendVec(std::vector<uint8_t>& out, const AddrVec& addrs)
        {
            for (const auto& a : addrs)
                out.push_back(a->value());
        }

        /// Sequential cursor over a save file's bytes, used by Load() below -
        /// every Read*() bounds-checks against `end` and returns false
        /// (leaving `ok` false for the rest of the read) instead of reading
        /// past a truncated/corrupt file.
        struct Reader
        {
            const uint8_t* p;
            const uint8_t* end;
            bool ok = true;

            bool Need(size_t n)
            {
                if (!ok || static_cast<size_t>(end - p) < n)
                {
                    ok = false;
                    return false;
                }
                return true;
            }

            uint8_t ReadU8()
            {
                if (!Need(1)) return 0;
                return *p++;
            }

            uint16_t ReadU16()
            {
                if (!Need(2)) return 0;
                uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
                p += 2;
                return v;
            }

            int32_t ReadI32()
            {
                if (!Need(4)) return 0;
                uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                             (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
                p += 4;
                return static_cast<int32_t>(v);
            }

            uint64_t ReadU64()
            {
                if (!Need(8)) return 0;
                uint64_t v = 0;
                for (int i = 0; i < 8; i++)
                    v |= static_cast<uint64_t>(p[i]) << (i * 8);
                p += 8;
                return v;
            }

            void ReadIntoVec(AddrVec& addrs)
            {
                if (!Need(addrs.size())) return;
                for (auto& a : addrs)
                    a->value(*p++);
            }
        };
    }

    bool NES_SaveState::Save(const std::string& path)
    {
        std::vector<uint8_t> out;
        out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
        AppendU64(out, NES_ROM::RomHash());

        AppendU16(out, NES_Register::PC);
        AppendU8(out, NES_Register::S);
        AppendU8(out, NES_Register::A);
        AppendU8(out, NES_Register::X);
        AppendU8(out, NES_Register::Y);
        AppendU8(out, NES_Register::P.P);

        // Full 64KB CPU address space - already covers PPUCTRL/PPUMASK/
        // PPUSTATUS/OAMADDR/OAMDATA/PPUSCROLL/PPUADDR/PPUDATA/OAMDMA (all
        // just AddressSetup cells inside NES_Memory::Memory) and every
        // mapper's *currently visible* PRG-ROM window - see this class's
        // own header comment.
        for (const auto& a : NES_Memory::Memory)
            out.push_back(a->value());

        AppendVec(out, NES_PPU_Memory::BGPalette);
        AppendVec(out, NES_PPU_Memory::SpritePalette);
        for (auto& bank : NES_PPU_Memory::NameTablePhysicalBanks())
            AppendVec(out, bank);
        for (auto& bank : NES_PPU_Memory::AttributeTablePhysicalBanks())
            AppendVec(out, bank);
        AppendVec(out, NES_PPU_Memory::PatternTable); // CHR-RAM content; harmless no-op weight for CHR-ROM boards

        AppendVec(out, NES_PPU_OAM::Memory);

        AppendU16(out, NES_PPU_Register::PPUPCADDR);
        AppendU8(out, NES_PPU_Register::GetPpuAddrHighLatch());

        AppendI32(out, NES_PPU::xScroll);
        AppendI32(out, NES_PPU::yScroll);
        AppendU8(out, NES_PPU::ScrollXoY ? 1 : 0);

        AppendU8(out, Interrupt::NMI() ? 1 : 0);
        AppendU8(out, Interrupt::IRQ() ? 1 : 0);
        AppendU8(out, Interrupt::BRK() ? 1 : 0);

        std::vector<uint8_t> mapperState;
        if (Mapper* mapper = NES_ROM::CurrentMapper())
            mapper->SerializeState(mapperState);
        AppendU16(out, static_cast<uint16_t>(mapperState.size()));
        out.insert(out.end(), mapperState.begin(), mapperState.end());

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return static_cast<bool>(file);
    }

    bool NES_SaveState::Load(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        auto size = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data.data()), size))
            return false;

        if (data.size() < sizeof(kMagic) || std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0)
            return false; // not a save file this version understands

        Reader r{ data.data() + sizeof(kMagic), data.data() + data.size() };

        uint64_t savedHash = r.ReadU64();
        if (savedHash != NES_ROM::RomHash())
            return false; // save file belongs to a different ROM - see Load()'s own header comment

        uint16_t pc = r.ReadU16();
        uint8_t s = r.ReadU8();
        uint8_t a = r.ReadU8();
        uint8_t x = r.ReadU8();
        uint8_t y = r.ReadU8();
        uint8_t p = r.ReadU8();

        if (!r.Need(NES_Memory::Memory.size()))
            return false;
        std::vector<uint8_t> memSnapshot(NES_Memory::Memory.size());
        std::memcpy(memSnapshot.data(), r.p, memSnapshot.size());
        r.p += memSnapshot.size();

        if (!r.Need(NES_PPU_Memory::BGPalette.size() + NES_PPU_Memory::SpritePalette.size()))
            return false;
        std::vector<uint8_t> bgPal(r.p, r.p + NES_PPU_Memory::BGPalette.size());
        r.p += NES_PPU_Memory::BGPalette.size();
        std::vector<uint8_t> sprPal(r.p, r.p + NES_PPU_Memory::SpritePalette.size());
        r.p += NES_PPU_Memory::SpritePalette.size();

        std::vector<std::vector<uint8_t>> nameBanks;
        for (auto& bank : NES_PPU_Memory::NameTablePhysicalBanks())
        {
            if (!r.Need(bank.size())) return false;
            nameBanks.emplace_back(r.p, r.p + bank.size());
            r.p += bank.size();
        }
        std::vector<std::vector<uint8_t>> attrBanks;
        for (auto& bank : NES_PPU_Memory::AttributeTablePhysicalBanks())
        {
            if (!r.Need(bank.size())) return false;
            attrBanks.emplace_back(r.p, r.p + bank.size());
            r.p += bank.size();
        }
        if (!r.Need(NES_PPU_Memory::PatternTable.size())) return false;
        std::vector<uint8_t> patternTable(r.p, r.p + NES_PPU_Memory::PatternTable.size());
        r.p += NES_PPU_Memory::PatternTable.size();

        if (!r.Need(NES_PPU_OAM::Memory.size())) return false;
        std::vector<uint8_t> oam(r.p, r.p + NES_PPU_OAM::Memory.size());
        r.p += NES_PPU_OAM::Memory.size();

        uint16_t ppuPcAddr = r.ReadU16();
        uint8_t ppuAddrHighLatch = r.ReadU8();
        int32_t xScroll = r.ReadI32();
        int32_t yScroll = r.ReadI32();
        uint8_t scrollXoY = r.ReadU8();
        uint8_t nmi = r.ReadU8();
        uint8_t irq = r.ReadU8();
        uint8_t brk = r.ReadU8();
        uint16_t mapperStateLen = r.ReadU16();
        if (!r.Need(mapperStateLen)) return false;
        const uint8_t* mapperStateBegin = r.p;
        r.p += mapperStateLen;

        if (!r.ok)
            return false; // truncated file - nothing below has touched live state yet

        // Everything parsed and bounds-checked - now safe to actually apply
        // it to live state. Deliberately all-or-nothing: no live state is
        // written above this point, so a corrupt/truncated file leaves the
        // running game completely untouched instead of half-restored.
        NES_Register::PC = pc;
        NES_Register::S = s;
        NES_Register::A = a;
        NES_Register::X = x;
        NES_Register::Y = y;
        NES_Register::P.P = p;

        for (size_t i = 0; i < NES_Memory::Memory.size(); i++)
            NES_Memory::Memory[i]->value(memSnapshot[i]);

        for (size_t i = 0; i < NES_PPU_Memory::BGPalette.size(); i++)
            NES_PPU_Memory::BGPalette[i]->value(bgPal[i]);
        for (size_t i = 0; i < NES_PPU_Memory::SpritePalette.size(); i++)
            NES_PPU_Memory::SpritePalette[i]->value(sprPal[i]);

        auto& nameTablePhysical = NES_PPU_Memory::NameTablePhysicalBanks();
        for (size_t bank = 0; bank < nameBanks.size(); bank++)
            for (size_t i = 0; i < nameBanks[bank].size(); i++)
                nameTablePhysical[bank][i]->value(nameBanks[bank][i]);
        auto& attrTablePhysical = NES_PPU_Memory::AttributeTablePhysicalBanks();
        for (size_t bank = 0; bank < attrBanks.size(); bank++)
            for (size_t i = 0; i < attrBanks[bank].size(); i++)
                attrTablePhysical[bank][i]->value(attrBanks[bank][i]);

        for (size_t i = 0; i < NES_PPU_Memory::PatternTable.size(); i++)
            NES_PPU_Memory::PatternTable[i]->value(patternTable[i]);

        for (size_t i = 0; i < NES_PPU_OAM::Memory.size(); i++)
            NES_PPU_OAM::Memory[i]->value(oam[i]);

        NES_PPU_Register::PPUPCADDR = ppuPcAddr;
        NES_PPU_Register::SetPpuAddrHighLatch(ppuAddrHighLatch);

        NES_PPU::xScroll = xScroll;
        NES_PPU::yScroll = yScroll;
        NES_PPU::ScrollXoY = scrollXoY != 0;

        Interrupt::NMI(nmi != 0);
        Interrupt::IRQ(irq != 0);
        Interrupt::BRK(brk != 0);

        if (Mapper* mapper = NES_ROM::CurrentMapper())
        {
            const uint8_t* cursor = mapperStateBegin;
            mapper->InvalidateBankCache();
            mapper->DeserializeState(cursor, mapperStateBegin + mapperStateLen);
        }

        return true;
    }
}
