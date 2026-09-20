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
/// DMC (delta modulation / sample-playback) channel: $4010-$4013. New code
/// (see NES_APU.h); built against http://wiki.nesdev.com/w/index.php/APU_DMC.
#include "NES_APU.h"
#include "NES_Memory.h"

namespace NES
{
    void NES_APU::DMC::WriteReg0(uint8_t v) // $4010: IL-- RRRR
    {
        irqEnabled = (v & 0x80) != 0;
        loop = (v & 0x40) != 0;
        rate = kDmcRateTableNTSC[v & 0x0F];
        if (!irqEnabled)
            interruptFlag = false;
    }

    void NES_APU::DMC::WriteReg1(uint8_t v) // $4011: -DDD DDDD (direct load)
    {
        outputLevel = v & 0x7F;
    }

    void NES_APU::DMC::WriteReg2(uint8_t v) // $4012: sample address = %11AAAAAA.AA000000
    {
        sampleAddress = static_cast<uint16_t>(0xC000 + (static_cast<int>(v) * 64));
    }

    void NES_APU::DMC::WriteReg3(uint8_t v) // $4013: sample length = %LLLL.LLLL0001
    {
        sampleLength = static_cast<uint16_t>((static_cast<int>(v) * 16) + 1);
    }

    void NES_APU::DMC::Restart()
    {
        currentAddress = sampleAddress;
        bytesRemaining = sampleLength;
    }

    // Note: real hardware stalls the CPU for 1-4 cycles while this happens
    // (the DMA reader briefly takes over the bus) - not modeled, since this
    // port's CPU has no per-cycle stepping to stall in the first place (see
    // the timing note in NES_APU.h). Reading via AddressSetup::value()
    // (not Value()) deliberately bypasses any read-side-effect hooks - PRG-
    // ROM in the $C000-$FFFF sample range has none anyway, but this keeps
    // the DMC's background sample fetch from ever accidentally triggering
    // one if a mapper ever adds any there.
    void NES_APU::DMC::FillSampleBufferIfNeeded()
    {
        if (sampleBufferFilled || bytesRemaining == 0)
            return;

        sampleBuffer = NES_Memory::Memory[currentAddress]->value();
        sampleBufferFilled = true;
        currentAddress = (currentAddress == 0xFFFF) ? 0x8000 : static_cast<uint16_t>(currentAddress + 1);
        --bytesRemaining;

        if (bytesRemaining == 0)
        {
            if (loop)
                Restart();
            else if (irqEnabled)
                interruptFlag = true;
        }
    }

    // Clocked every CPU cycle - the rate table is already in CPU-cycle units.
    void NES_APU::DMC::ClockTimer()
    {
        FillSampleBufferIfNeeded();

        if (timerCounter > 0)
        {
            --timerCounter;
            return;
        }
        timerCounter = rate;

        if (!silence)
        {
            if (shiftRegister & 0x01)
            {
                if (outputLevel <= 125)
                    outputLevel = static_cast<uint8_t>(outputLevel + 2);
            }
            else
            {
                if (outputLevel >= 2)
                    outputLevel = static_cast<uint8_t>(outputLevel - 2);
            }
        }
        shiftRegister >>= 1;

        if (bitsRemaining > 0)
            --bitsRemaining;
        if (bitsRemaining == 0)
        {
            bitsRemaining = 8;
            if (sampleBufferFilled)
            {
                silence = false;
                shiftRegister = sampleBuffer;
                sampleBufferFilled = false;
            }
            else
            {
                silence = true;
            }
        }
    }
}
