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
#include <memory>

namespace NES
{
    /// @brief One addressable byte in the NES's 64KB address space.
    ///
    /// Every cell in NES_Memory::Memory
    /// is one of these. The four hook functions (BeforGet/AfterGet/BeforSet/AfterSet)
    /// are how memory-mapped I/O works: e.g. the PPU registers or the controller
    /// port attach a hook here so that a plain CPU read/write to that address also
    /// triggers PPU/controller side effects - exactly like the real NES hardware
    /// decodes the address bus. See http://wiki.nesdev.com/w/index.php/CPU_memory_map
    class AddressSetup : public Address
    {
    public:
        /// Hook run without arguments (before a get, after a get, before a set).
        using func = std::function<void()>;
        /// Hook that receives the value just written (after a set).
        using funcOut = std::function<void(uint8_t)>;

        /// Creates a cell with value 0 for the given address `id`.
        explicit AddressSetup(int id);
        /// Copies value and hooks.
        AddressSetup(const AddressSetup& other);
        /// Copies value and hooks.
        AddressSetup& operator=(const AddressSetup& other);
        /// Creates a cell with an initial value for the given address `id`.
        AddressSetup(uint8_t value, int id);

        /// Pure value without additional functions.
        uint8_t value() const { return valueCore; }
        /// Sets the raw value without running any hooks.
        void value(uint8_t v) { valueCore = v; }

        /// Value with additional get/set hook functions.
        uint8_t Value() const override;
        /// Writes the value: runs the before-set hook, stores the value, then runs the after-set hook with it.
        void Value(uint8_t v) override;

        /// Address (0-0xFFFF) this cell stands for.
        int ID() const { return id; }
        /// Sets the address this cell stands for.
        void ID(int v) { id = v; }

        /// Hook run before the value is read.
        func BeforGet() const { return hooks && hooks->beforGet ? hooks->beforGet : func([] {}); }
        /// Sets the hook run before the value is read.
        void BeforGet(func f) { Hooks().beforGet = std::move(f); }

        /// Hook run after the value is read.
        func AfterGet() const { return hooks && hooks->afterGet ? hooks->afterGet : func([] {}); }
        /// Sets the hook run after the value is read.
        void AfterGet(func f) { Hooks().afterGet = std::move(f); }

        /// Hook run before the value is written.
        func BeforSet() const { return hooks && hooks->beforSet ? hooks->beforSet : func([] {}); }
        /// Sets the hook run before the value is written.
        void BeforSet(func f) { Hooks().beforSet = std::move(f); }

        /// Hook run after the value is written; receives the written value.
        funcOut AfterSet() const { return hooks && hooks->afterSet ? hooks->afterSet : funcOut([](uint8_t) {}); }
        /// Sets the hook run after the value is written.
        void AfterSet(funcOut f) { Hooks().afterSet = std::move(f); }

        /// True if the raw value changed since the last setAsOld() call.
        bool isNew() const override;
        /// Remembers the current raw value as the baseline for isNew().
        void setAsOld() override;
        /// Formats the cell as `0xVV: (AAAA)` (value, then address, in hex).
        std::string ToString() const override;

    private:
        /// The four hooks. Only allocated once a hook is set: most of the 64 KB of cells never have
        /// one, and skipping the empty std::function calls makes every plain memory access cheap.
        struct HookSet
        {
            func beforGet, afterGet, beforSet;
            funcOut afterSet;
        };
        HookSet& Hooks()
        {
            if (!hooks)
                hooks = std::make_unique<HookSet>();
            return *hooks;
        }

        int id = 0;
        std::unique_ptr<HookSet> hooks;
        uint8_t oldValue = 0;
        uint8_t valueCore = 0;
    };
}
