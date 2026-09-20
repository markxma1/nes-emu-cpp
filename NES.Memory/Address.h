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
#include <cstdint>
#include <string>

namespace NES
{
    /// @brief Interface for one addressable byte cell.
    ///
    /// C++ port of the C# `Address` interface (NES.Memory/Address.cs).
    /// C# properties don't exist in C++, so the `Value` get/set pair from the
    /// original interface becomes an overloaded `Value()` accessor pair here
    /// (no-arg getter, one-arg setter) - kept as one name, exactly like the
    /// C# property was one name with two accessors.
    class Address
    {
    public:
        virtual ~Address() = default;

        /// Value with additional get/set hook functions (mirrors the C# `Value` property).
        virtual uint8_t Value() const = 0;
        virtual void Value(uint8_t v) = 0;

        virtual bool isNew() const = 0;
        virtual void setAsOld() = 0;
        virtual std::string ToString() const = 0;
    };
}
