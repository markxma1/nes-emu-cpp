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
#include "MapperFactory.h"
#include "Mapper_NROM.h"
#include "Mapper_MMC1.h"
#include "Mapper_UxROM.h"
#include "Mapper_CNROM.h"
#include "Mapper_MMC3.h"
#include "Mapper_AxROM.h"
#include <stdexcept>

namespace NES
{
    std::unique_ptr<Mapper> CreateMapper(int mapperNumber)
    {
        switch (mapperNumber)
        {
            case 0: return std::make_unique<Mapper_NROM>();
            case 1: return std::make_unique<Mapper_MMC1>();
            case 2: return std::make_unique<Mapper_UxROM>();
            case 3: return std::make_unique<Mapper_CNROM>();
            case 4: return std::make_unique<Mapper_MMC3>();
            case 7: return std::make_unique<Mapper_AxROM>();
            default:
                throw std::runtime_error("CreateMapper: mapper " + std::to_string(mapperNumber) +
                    " is not implemented yet (supported: 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM)");
        }
    }
}
