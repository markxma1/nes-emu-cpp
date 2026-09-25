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
#include <stdexcept>

namespace NES
{
    /// @brief Thrown for an unimplemented/illegal opcode.
    /// Note: the class name contains the "NoAssemby" typo.
    class NoAssemby : public std::runtime_error
    {
    public:
        NoAssemby() : std::runtime_error("NoAssemby") {}
        /// Creates the exception with a custom message.
        explicit NoAssemby(const std::string& message) : std::runtime_error(message) {}
    };
}
