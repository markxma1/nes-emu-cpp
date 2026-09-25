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
/// @file EnvFlag.h
/// @brief Cached environment variable lookup for debug switches.
///
/// `std::getenv` walks the whole environment on every call, which is far too slow
/// inside code that runs per instruction or per scanline. `NES_GETENV("NAME")` reads the
/// variable once per call site (the name must be a string literal) and remembers the
/// result, so it costs one load afterwards. The switches are read when the emulator
/// starts; changing them later has no effect.
#include <cstdlib>

#define NES_GETENV(name) ([]() -> const char* { static const char* const value = std::getenv(name); return value; }())
