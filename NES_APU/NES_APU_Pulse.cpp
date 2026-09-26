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
/// Pulse channel ($4000-$4003 / $4004-$4007). New code (see NES_APU.h);
/// built against http://wiki.nesdev.com/w/index.php/APU_Pulse and
/// http://wiki.nesdev.com/w/index.php/APU_Sweep.
#include "NES_APU.h"

namespace NES
{
    void NES_APU::Pulse::WriteReg0(uint8_t v) // $4000/$4004: DDLC VVVV
    {
        duty = (v >> 6) & 0x03;
        envelope.loop = (v & 0x20) != 0; // shared with the length-counter halt flag
        envelope.constantVolume = (v & 0x10) != 0;
        envelope.volumeParam = v & 0x0F;
    }

    void NES_APU::Pulse::WriteReg1(uint8_t v) // $4001/$4005: EPPP NSSS
    {
        sweepEnabled = (v & 0x80) != 0;
        sweepPeriod = (v >> 4) & 0x07;
        sweepNegate = (v & 0x08) != 0;
        sweepShift = v & 0x07;
        sweepReload = true;
    }

    void NES_APU::Pulse::WriteReg2(uint8_t v) // $4002/$4006: timer low 8 bits
    {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x0700) | v);
    }

    void NES_APU::Pulse::WriteReg3(uint8_t v) // $4003/$4007: LLLL LTTT
    {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x00FF) | ((v & 0x07) << 8));
        length.Load(static_cast<uint8_t>((v >> 3) & 0x1F));
        envelope.Restart();
        dutyStep = 0; // side effect of writing $4003/$4007: sequencer restarts
    }

    // Called once per *APU* cycle (every 2nd CPU cycle - see
    // NES_APU::ClockOneCpuCycle) - fpulse = fCPU/(16*(t+1)) means each of the
    // 8 duty steps lasts (t+1) APU cycles.
    void NES_APU::Pulse::ClockTimer()
    {
        if (timerCounter <= 0)
        {
            timerCounter = timerPeriod; // counts period..0 = period+1 ticks per duty step
            dutyStep = (dutyStep + 1) & 0x07;
        }
        else
        {
            --timerCounter;
        }
    }

    uint16_t NES_APU::Pulse::SweepTarget() const
    {
        int change = timerPeriod >> sweepShift;
        int delta;
        if (sweepNegate)
            // Pulse 1 subtracts one extra (ones'-complement negate) so it
            // mutes one period sooner than pulse 2's two's-complement
            // negate - a deliberate, documented hardware quirk, not a bug.
            delta = isChannel2 ? -change : -(change + 1);
        else
            delta = change;
        int target = static_cast<int>(timerPeriod) + delta;
        return target < 0 ? 0 : static_cast<uint16_t>(target);
    }

    bool NES_APU::Pulse::SweepMutes() const
    {
        // "the target period is computed continuously" - muting applies
        // even while the sweep unit itself is disabled.
        return timerPeriod < 8 || SweepTarget() > 0x7FF;
    }

    void NES_APU::Pulse::ClockSweep()
    {
        uint16_t target = SweepTarget();
        if (sweepDivider == 0 && sweepEnabled && sweepShift != 0 && !SweepMutes())
            timerPeriod = target;

        if (sweepDivider == 0 || sweepReload)
        {
            sweepDivider = sweepPeriod;
            sweepReload = false;
        }
        else
        {
            --sweepDivider;
        }
    }

    uint8_t NES_APU::Pulse::Output() const
    {
        if (length.Silenced() || SweepMutes())
            return 0;
        if (!kDutyTable[duty][dutyStep])
            return 0;
        return envelope.Output();
    }
}
