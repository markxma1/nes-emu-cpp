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
#include "Address.h"
#include <functional>

namespace NES
{
    /// @brief One addressable byte in the NES's 64KB address space.
    ///
    /// C++ port of NES.Memory/AddressSetup.cs. Every cell in NES_Memory::Memory
    /// is one of these. The four hook functions (BeforGet/AfterGet/BeforSet/AfterSet)
    /// are how memory-mapped I/O works: e.g. the PPU registers or the controller
    /// port attach a hook here so that a plain CPU read/write to that address also
    /// triggers PPU/controller side effects - exactly like the real NES hardware
    /// decodes the address bus. See http://wiki.nesdev.com/w/index.php/CPU_memory_map
    class AddressSetup : public Address
    {
    public:
        using func = std::function<void()>;
        using funcOut = std::function<void(uint8_t)>;

        explicit AddressSetup(int id);
        AddressSetup(uint8_t value, int id);

        /// Pure value without additional functions (mirrors the C# `value` property).
        uint8_t value() const { return valueCore; }
        void value(uint8_t v) { valueCore = v; }

        /// Value with additional get/set hook functions (mirrors the C# `Value` property).
        uint8_t Value() const override;
        void Value(uint8_t v) override;

        int ID() const { return id; }
        void ID(int v) { id = v; }

        func BeforGet() const { return beforGet; }
        void BeforGet(func f) { beforGet = std::move(f); }

        func AfterGet() const { return afterGet; }
        void AfterGet(func f) { afterGet = std::move(f); }

        func BeforSet() const { return beforSet; }
        void BeforSet(func f) { beforSet = std::move(f); }

        funcOut AfterSet() const { return afterSet; }
        void AfterSet(funcOut f) { afterSet = std::move(f); }

        bool isNew() const override;
        void setAsOld() override;
        std::string ToString() const override;

    private:
        int id = 0;
        func beforGet = [] {};
        func afterGet = [] {};
        func beforSet = [] {};
        funcOut afterSet = [](uint8_t) {};
        uint8_t oldValue = 0;
        uint8_t valueCore = 0;
    };
}
