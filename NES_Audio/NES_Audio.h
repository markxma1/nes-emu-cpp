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
    /// @brief SDL2 audio output glue for NES_APU. UI-shell code (same
    /// category as InputSource.h/main.cpp). Deliberately thin: SDL owns a background
    /// thread that periodically calls back asking for N samples, and this
    /// class's callback just calls NES_APU::FillAudioBuffer() to fetch the
    /// samples the emulated CPU thread produced (NES_APU::Advance()) - all the actual
    /// emulation logic lives in NES_APU, not here.
    class NES_Audio
    {
    public:
        /// Opens the default SDL audio output device at 44100 Hz mono
        /// 16-bit and starts playback. Returns false (logging why) if SDL
        /// audio isn't available - the emulator still runs fine without
        /// sound in that case, this is not a fatal error.
        static bool Start();

        /// Stops playback and closes the device. Safe to call even if
        /// Start() was never called or failed.
        static void Stop();
    };
}
