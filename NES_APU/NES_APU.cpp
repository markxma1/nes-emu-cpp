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
#include "EnvFlag.h"
#include "NES_APU.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

namespace NES
{
    NES_APU::Pulse NES_APU::pulse1_;
    NES_APU::Pulse NES_APU::pulse2_;
    NES_APU::Triangle NES_APU::triangle_;
    NES_APU::Noise NES_APU::noise_;
    NES_APU::DMC NES_APU::dmc_;

    bool NES_APU::frameIrqInhibit_ = false;
    bool NES_APU::frameIrqFlag_ = false;
    bool NES_APU::fiveStepMode_ = false;
    int NES_APU::frameSequencerCounter_ = 0;
    int NES_APU::cpuCycleParity_ = 0;

    double NES_APU::cycleAccumulator_ = 0.0;
    float NES_APU::dcPrevIn_ = 0.0f;
    float NES_APU::dcPrevOut_ = 0.0f;
    std::mutex NES_APU::mutex_;

    // http://wiki.nesdev.com/w/index.php/APU_Length_Counter
    const uint8_t NES_APU::kLengthTable[32] = {
        10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
        12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
    };

    // http://wiki.nesdev.com/w/index.php/APU_Noise, NTSC column.
    const uint16_t NES_APU::kNoisePeriodTableNTSC[16] = {
        4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
    };

    // http://wiki.nesdev.com/w/index.php/APU_DMC, NTSC rate table (CPU cycles).
    const uint16_t NES_APU::kDmcRateTableNTSC[16] = {
        428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
    };

    // http://wiki.nesdev.com/w/index.php/APU_Triangle - 32-step sequence.
    const uint8_t NES_APU::kTriangleSequence[32] = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    // http://wiki.nesdev.com/w/index.php/APU_Pulse - output waveform per duty
    // setting, indexed by sequencer step (0-7). The exact starting phase
    // doesn't matter for how it sounds (only the repeating shape does), so
    // this port always advances the step forward rather than reproducing
    // real hardware's specific bit-shift-right sequencer direction.
    const uint8_t NES_APU::kDutyTable[4][8] = {
        { 0, 1, 0, 0, 0, 0, 0, 0 }, // 12.5%
        { 0, 1, 1, 0, 0, 0, 0, 0 }, // 25%
        { 0, 1, 1, 1, 1, 0, 0, 0 }, // 50%
        { 1, 0, 0, 1, 1, 1, 1, 1 }, // 25% negated
    };

    void NES_APU::Envelope::Clock()
    {
        if (start)
        {
            start = false;
            decayLevel = 15;
            divider = volumeParam;
            return;
        }
        if (divider == 0)
        {
            divider = volumeParam;
            if (decayLevel > 0)
                --decayLevel;
            else if (loop)
                decayLevel = 15;
        }
        else
        {
            --divider;
        }
    }

    void NES_APU::LengthCounter::Load(uint8_t index)
    {
        if (enabled)
            value = kLengthTable[index & 0x1F];
    }

    void NES_APU::LengthCounter::Clock(bool halt)
    {
        if (!halt && value > 0)
            --value;
    }

    void NES_APU::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pulse1_ = Pulse{};
        pulse1_.isChannel2 = false;
        pulse2_ = Pulse{};
        pulse2_.isChannel2 = true;
        triangle_ = Triangle{};
        noise_ = Noise{};
        noise_.shiftRegister = 1;
        dmc_ = DMC{};

        frameIrqInhibit_ = false;
        frameIrqFlag_ = false;
        fiveStepMode_ = false;
        frameSequencerCounter_ = 0;
        cpuCycleParity_ = 0;
        cycleAccumulator_ = 0.0;
        dcPrevIn_ = 0.0f;
        dcPrevOut_ = 0.0f;
    }

    void NES_APU::WriteRegister(uint16_t address, uint8_t value)
    {
        if (NES_GETENV("NES_TRACE_APU_RATE"))
        {
            static auto lastReportTime = std::chrono::steady_clock::now();
            static uint64_t writeCount = 0;
            ++writeCount;
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastReportTime).count() >= 1.0)
            {
                std::cerr << "[NES_TRACE_APU_RATE] register writes/sec=" << writeCount << std::endl;
                lastReportTime = now;
                writeCount = 0;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        switch (address)
        {
            case 0x4000: pulse1_.WriteReg0(value); break;
            case 0x4001: pulse1_.WriteReg1(value); break;
            case 0x4002: pulse1_.WriteReg2(value); break;
            case 0x4003: pulse1_.WriteReg3(value); break;
            case 0x4004: pulse2_.WriteReg0(value); break;
            case 0x4005: pulse2_.WriteReg1(value); break;
            case 0x4006: pulse2_.WriteReg2(value); break;
            case 0x4007: pulse2_.WriteReg3(value); break;
            case 0x4008: triangle_.WriteReg0(value); break;
            case 0x400A: triangle_.WriteReg2(value); break;
            case 0x400B: triangle_.WriteReg3(value); break;
            case 0x400C: noise_.WriteReg0(value); break;
            case 0x400E: noise_.WriteReg2(value); break;
            case 0x400F: noise_.WriteReg3(value); break;
            case 0x4010: dmc_.WriteReg0(value); break;
            case 0x4011: dmc_.WriteReg1(value); break;
            case 0x4012: dmc_.WriteReg2(value); break;
            case 0x4013: dmc_.WriteReg3(value); break;

            // http://wiki.nesdev.com/w/index.php/APU#Status_.28.244015.29
            case 0x4015:
            {
                pulse1_.length.enabled = (value & 0x01) != 0;
                pulse2_.length.enabled = (value & 0x02) != 0;
                triangle_.length.enabled = (value & 0x04) != 0;
                noise_.length.enabled = (value & 0x08) != 0;
                if (!pulse1_.length.enabled) pulse1_.length.value = 0;
                if (!pulse2_.length.enabled) pulse2_.length.value = 0;
                if (!triangle_.length.enabled) triangle_.length.value = 0;
                if (!noise_.length.enabled) noise_.length.value = 0;

                dmc_.enabled = (value & 0x10) != 0;
                if (!dmc_.enabled)
                    dmc_.bytesRemaining = 0;
                else if (dmc_.bytesRemaining == 0)
                    dmc_.Restart();
                dmc_.interruptFlag = false; // writing $4015 always clears the DMC IRQ flag
                break;
            }

            // http://wiki.nesdev.com/w/index.php/APU_Frame_Counter
            case 0x4017:
            {
                fiveStepMode_ = (value & 0x80) != 0;
                frameIrqInhibit_ = (value & 0x40) != 0;
                if (frameIrqInhibit_)
                    frameIrqFlag_ = false;
                frameSequencerCounter_ = 0;
                // NOTE: real hardware resets the divider/sequencer 3-4 CPU
                // cycles after this write (timing depends on write
                // alignment), not instantly - not meaningfully audible
                // given this port's time-based (not cycle-exact) clocking
                // (see the class comment in NES_APU.h), so simplified to an
                // immediate reset.
                if (fiveStepMode_)
                {
                    ClockQuarterFrame();
                    ClockHalfFrame();
                }
                break;
            }
            default:
                break;
        }
    }

    uint8_t NES_APU::ReadStatus()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint8_t status = 0;
        if (!pulse1_.length.Silenced()) status |= 0x01;
        if (!pulse2_.length.Silenced()) status |= 0x02;
        if (!triangle_.length.Silenced()) status |= 0x04;
        if (!noise_.length.Silenced()) status |= 0x08;
        if (dmc_.bytesRemaining > 0) status |= 0x10;
        if (frameIrqFlag_) status |= 0x40;
        if (dmc_.interruptFlag) status |= 0x80;
        frameIrqFlag_ = false; // reading $4015 clears the frame-IRQ flag (not the DMC one)
        return status;
    }

    void NES_APU::ClockQuarterFrame()
    {
        pulse1_.envelope.Clock();
        pulse2_.envelope.Clock();
        noise_.envelope.Clock();
        triangle_.ClockLinearCounter();
    }

    void NES_APU::ClockHalfFrame()
    {
        pulse1_.length.Clock(pulse1_.envelope.loop);
        pulse2_.length.Clock(pulse2_.envelope.loop);
        noise_.length.Clock(noise_.envelope.loop);
        triangle_.length.Clock(triangle_.control);
        pulse1_.ClockSweep();
        pulse2_.ClockSweep();
    }

    // http://wiki.nesdev.com/w/index.php/APU_Frame_Counter - NTSC step
    // numbers, in APU cycles (1 APU cycle = 2 CPU cycles, the same cadence
    // pulse/noise timers clock at).
    namespace
    {
        constexpr int kQuarterFrameSteps[4] = { 3728, 7456, 11185, 14914 };
        constexpr int kFifthStep = 18640;
    }

    void NES_APU::ClockOneCpuCycle()
    {
        // Triangle's timer and the DMC's own timer both clock every CPU
        // cycle (http://wiki.nesdev.com/w/index.php/APU#Triangle_.28.244008.2C_.24400A.2C_.24400B.29,
        // DMC's rate table is documented directly in CPU cycles).
        triangle_.ClockTimer();
        dmc_.ClockTimer();

        cpuCycleParity_ ^= 1;
        if (cpuCycleParity_ != 0)
            return; // pulse/noise/frame-sequencer clock every *2nd* CPU cycle

        pulse1_.ClockTimer();
        pulse2_.ClockTimer();
        noise_.ClockTimer();

        ++frameSequencerCounter_;
        bool isQuarter = frameSequencerCounter_ == kQuarterFrameSteps[0] ||
                          frameSequencerCounter_ == kQuarterFrameSteps[1] ||
                          frameSequencerCounter_ == kQuarterFrameSteps[2] ||
                          frameSequencerCounter_ == kQuarterFrameSteps[3] ||
                          (fiveStepMode_ && frameSequencerCounter_ == kFifthStep);
        bool isHalf = frameSequencerCounter_ == kQuarterFrameSteps[1] ||
                      frameSequencerCounter_ == kQuarterFrameSteps[3] ||
                      (fiveStepMode_ && frameSequencerCounter_ == kFifthStep);

        if (isQuarter)
            ClockQuarterFrame();
        if (isHalf)
            ClockHalfFrame();

        if (!fiveStepMode_ && frameSequencerCounter_ == kQuarterFrameSteps[3] && !frameIrqInhibit_)
            frameIrqFlag_ = true;

        int lastStep = fiveStepMode_ ? kFifthStep : kQuarterFrameSteps[3];
        if (frameSequencerCounter_ >= lastStep)
            frameSequencerCounter_ = 0;
    }

    // http://wiki.nesdev.com/w/index.php/APU_Mixer - lookup-table-equivalent
    // formula approach (computed directly rather than via a precomputed
    // table, since this isn't a hot enough path to need it: one call per
    // output audio sample, not per CPU cycle).
    float NES_APU::MixCurrentOutput()
    {
        uint8_t p1 = pulse1_.Output();
        uint8_t p2 = pulse2_.Output();
        uint8_t tr = triangle_.Output();
        uint8_t ns = noise_.Output();
        uint8_t dm = dmc_.Output();

        float pulseOut = (p1 == 0 && p2 == 0)
            ? 0.0f
            : 95.88f / (8128.0f / static_cast<float>(p1 + p2) + 100.0f);

        float tndSum = static_cast<float>(tr) / 8227.0f
                     + static_cast<float>(ns) / 12241.0f
                     + static_cast<float>(dm) / 22638.0f;
        float tndOut = (tr == 0 && ns == 0 && dm == 0) ? 0.0f : 159.79f / (tndSum + 100.0f);

        float mixed = pulseOut + tndOut; // always >= 0 - has a large DC offset

        // Real hardware's output stage includes RC high-pass filtering that
        // removes this DC bias before the signal reaches the audio jack
        // (see APU_Mixer's "Emulation" section) - approximated here with a
        // simple one-pole DC blocker so samples end up centered around 0
        // instead of clicking/wasting headroom sitting at a constant offset.
        constexpr float kDcAlpha = 0.996f;
        float filtered = mixed - dcPrevIn_ + kDcAlpha * dcPrevOut_;
        dcPrevIn_ = mixed;
        dcPrevOut_ = filtered;
        return filtered;
    }

    void NES_APU::FillAudioBuffer(int16_t* buffer, int numSamples, double sampleRateHz)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        double cyclesPerSample = kCpuClockHzNTSC / sampleRateHz;
        for (int i = 0; i < numSamples; ++i)
        {
            cycleAccumulator_ += cyclesPerSample;
            int cyclesThisSample = static_cast<int>(cycleAccumulator_);
            cycleAccumulator_ -= cyclesThisSample;
            for (int c = 0; c < cyclesThisSample; ++c)
                ClockOneCpuCycle();

            float mixed = MixCurrentOutput();
            int sample = static_cast<int>(mixed * 32767.0f);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            buffer[i] = static_cast<int16_t>(sample);
        }
    }
}
