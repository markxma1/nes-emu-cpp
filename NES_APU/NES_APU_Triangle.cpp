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
/// Triangle channel ($4008, $400A, $400B). New code (see NES_APU.h); built
/// against http://wiki.nesdev.com/w/index.php/APU_Triangle.
#include "NES_APU.h"

namespace NES
{
    void NES_APU::Triangle::WriteReg0(uint8_t v) // $4008: CRRR RRRR
    {
        control = (v & 0x80) != 0; // doubles as the length-counter halt flag
        linearReloadValue = v & 0x7F;
    }

    void NES_APU::Triangle::WriteReg2(uint8_t v) // $400A: timer low 8 bits
    {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x0700) | v);
    }

    void NES_APU::Triangle::WriteReg3(uint8_t v) // $400B: LLLL LTTT
    {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x00FF) | ((v & 0x07) << 8));
        length.Load(static_cast<uint8_t>((v >> 3) & 0x1F));
        linearReloadFlag = true;
    }

    // Clocked every CPU cycle (unlike pulse/noise's every-2nd-cycle) -
    // f = fCPU/(32*(t+1)), so each of the 32 sequence steps lasts (t+1) CPU
    // cycles.
    void NES_APU::Triangle::ClockTimer()
    {
        if (timerCounter <= 0)
        {
            timerCounter = timerPeriod; // counts period..0 = period+1 CPU cycles per step
            // Real hardware freezes the sequencer (rather than silencing the
            // output) when either counter is 0 - this is what keeps the
            // triangle from producing an audible "ultrasonic pop" click
            // every time a note stops, unlike the other channels.
            if (!length.Silenced() && linearCounter > 0)
                sequenceStep = (sequenceStep + 1) & 0x1F;
        }
        else
        {
            --timerCounter;
        }
    }

    void NES_APU::Triangle::ClockLinearCounter()
    {
        if (linearReloadFlag)
            linearCounter = linearReloadValue;
        else if (linearCounter > 0)
            --linearCounter;

        if (!control)
            linearReloadFlag = false;
    }

    uint8_t NES_APU::Triangle::Output() const
    {
        return kTriangleSequence[sequenceStep];
    }
}
