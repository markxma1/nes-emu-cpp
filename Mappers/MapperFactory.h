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
#include "Mapper.h"
#include <memory>

namespace NES
{
    /// @brief Creates the Mapper for an iNES header's mapper number
    /// (INES::Lmapper | (INES::Hmapper << 4) - see INES.h). New code, the
    /// dispatch NES_ROM.cpp never had (see its LEARNING NOTE this
    /// replaced). Throws std::runtime_error, with the mapper number in the
    /// message, for anything not one of the (currently 6) implemented
    /// boards - so an unsupported ROM fails with a clear message instead of
    /// silently mis-loading as the wrong board, matching how NES_ROM.cpp
    /// already reports other startup failures.
    std::unique_ptr<Mapper> CreateMapper(int mapperNumber);
}
