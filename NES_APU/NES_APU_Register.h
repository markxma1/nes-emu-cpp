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

namespace NES
{
    /// @brief Wires $4000-$4013/$4015/$4017 to NES_APU via
    /// AddressSetup::AfterSet/BeforGet - the same memory-mapped-I/O hook
    /// pattern NES_PPU_Register.cpp uses for the PPU's registers (see
    /// NES.Memory/AddressSetup.h). Constructed once from NES_Console::INIT(), same as
    /// NES_PPU_Register.
    class NES_APU_Register
    {
    public:
        NES_APU_Register();
    };
}
