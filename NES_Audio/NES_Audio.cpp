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
#include "NES_Audio.h"
#include "NES_APU.h"

#include <SDL.h>
#include <cstdint>
#include <iostream>

namespace NES
{
    namespace
    {
        SDL_AudioDeviceID device = 0;
        constexpr int kSampleRate = 44100;

        void AudioCallback(void* /*userdata*/, Uint8* stream, int len)
        {
            // len is in bytes; format is mono 16-bit, so 2 bytes/sample.
            int numSamples = len / 2;
            NES_APU::FillAudioBuffer(reinterpret_cast<int16_t*>(stream), numSamples);
        }
    }

    bool NES_Audio::Start()
    {
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        {
            std::cerr << "Warning: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " << SDL_GetError()
                       << " - running without sound." << std::endl;
            return false;
        }

        SDL_AudioSpec want{};
        want.freq = kSampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1; // NES audio is a single mixed mono signal
        want.samples = 1024;
        want.callback = AudioCallback;

        SDL_AudioSpec have{};
        device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0 /* require exact format, no SDL resampling */);
        if (device == 0)
        {
            std::cerr << "Warning: SDL_OpenAudioDevice failed: " << SDL_GetError()
                       << " - running without sound." << std::endl;
            return false;
        }

        NES_APU::SetSampleRate(have.freq);
        NES_APU::SetGenerateSamples(true); // the APU only produces samples while somebody listens
        SDL_PauseAudioDevice(device, 0); // start playback
        return true;
    }

    void NES_Audio::Stop()
    {
        if (device != 0)
        {
            SDL_CloseAudioDevice(device);
            device = 0;
        }
    }
}
