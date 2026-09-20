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
///
/// Noise channel ($400C, $400E, $400F). New code (see NES_APU.h); built
/// against http://wiki.nesdev.com/w/index.php/APU_Noise.
#include "NES_APU.h"

namespace NES
{
    void NES_APU::Noise::WriteReg0(uint8_t v) // $400C: --LC VVVV
    {
        envelope.loop = (v & 0x20) != 0; // shared with the length-counter halt flag
        envelope.constantVolume = (v & 0x10) != 0;
        envelope.volumeParam = v & 0x0F;
    }

    void NES_APU::Noise::WriteReg2(uint8_t v) // $400E: M--- PPPP
    {
        mode = (v & 0x80) != 0;
        timerPeriod = kNoisePeriodTableNTSC[v & 0x0F];
    }

    void NES_APU::Noise::WriteReg3(uint8_t v) // $400F: LLLL L---
    {
        length.Load(static_cast<uint8_t>((v >> 3) & 0x1F));
        envelope.Restart();
    }

    // Clocked once per APU cycle, same cadence as the pulse channels.
    void NES_APU::Noise::ClockTimer()
    {
        if (timerCounter <= 0)
        {
            timerCounter = timerPeriod;
            int tapBit = mode ? 6 : 1;
            uint16_t feedback = (shiftRegister & 0x0001) ^ ((shiftRegister >> tapBit) & 0x0001);
            shiftRegister >>= 1;
            shiftRegister |= static_cast<uint16_t>(feedback << 14);
        }
        else
        {
            --timerCounter;
        }
    }

    uint8_t NES_APU::Noise::Output() const
    {
        if (length.Silenced() || (shiftRegister & 0x0001))
            return 0;
        return envelope.Output();
    }
}
