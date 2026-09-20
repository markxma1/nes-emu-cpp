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

    uint8_t AddressSetup::Value() const
    {
        beforGet();
        uint8_t temp = valueCore;
        afterGet();
        return temp;
    }

    void AddressSetup::Value(uint8_t v)
    {
        beforSet();
        valueCore = v;
        afterSet(v);
    }

    bool AddressSetup::isNew() const
    {
        return !(oldValue == valueCore);
    }

    void AddressSetup::setAsOld()
    {
        oldValue = valueCore;
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
