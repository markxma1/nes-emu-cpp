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
#include "AddressSetup.h"
#include <sstream>
#include <iomanip>

namespace NES
{
    AddressSetup::AddressSetup(int id) : id(id) {}

    AddressSetup::AddressSetup(uint8_t value, int id) : id(id), valueCore(value) {}

    AddressSetup::AddressSetup(const AddressSetup& other)
        : Address(other), id(other.id), hooks(other.hooks ? std::make_unique<HookSet>(*other.hooks) : nullptr),
          romSlot(other.romSlot), romIndex(other.romIndex), oldValue(other.oldValue), valueCore(other.valueCore)
    {
    }

    AddressSetup& AddressSetup::operator=(const AddressSetup& other)
    {
        if (this != &other)
        {
            id = other.id;
            hooks = other.hooks ? std::make_unique<HookSet>(*other.hooks) : nullptr;
            romSlot = other.romSlot;
            romIndex = other.romIndex;
            oldValue = other.oldValue;
            valueCore = other.valueCore;
        }
        return *this;
    }

    uint8_t AddressSetup::Value() const
    {
        if (!hooks)
            return value();
        if (hooks->beforGet)
            hooks->beforGet();
        uint8_t temp = value();
        if (hooks->afterGet)
            hooks->afterGet();
        return temp;
    }

    void AddressSetup::Value(uint8_t v)
    {
        if (!hooks)
        {
            value(v);
            return;
        }
        if (hooks->beforSet)
            hooks->beforSet();
        value(v);
        if (hooks->afterSet)
            hooks->afterSet(v);
    }

    bool AddressSetup::isNew() const
    {
        return !(oldValue == value());
    }

    void AddressSetup::setAsOld()
    {
        oldValue = value();
    }

    std::string AddressSetup::ToString() const
    {
        std::ostringstream hexValue;
        hexValue << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(valueCore);

        std::ostringstream hexId;
        hexId << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << id;

        return hexValue.str() + ": (" + hexId.str() + ")";
    }
}
