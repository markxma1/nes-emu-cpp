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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
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

    std::atomic<bool> NES_APU::generateSamples_{false};
    std::atomic<double> NES_APU::sampleRateHz_{44100.0};
    std::atomic<int> NES_APU::volume_{100};
    double NES_APU::cyclesPerSampleFor_ = 44100.0;
    double NES_APU::cyclesPerSample_ = NES_APU::kCpuClockHzNTSC / 44100.0;
    double NES_APU::cycleAccumulator_ = 0.0;
    double NES_APU::sampleSum_ = 0.0;
    int NES_APU::sampleCount_ = 0;
    float NES_APU::hp1PrevIn_ = 0.0f, NES_APU::hp1PrevOut_ = 0.0f;
    float NES_APU::hp2PrevIn_ = 0.0f, NES_APU::hp2PrevOut_ = 0.0f;
    float NES_APU::lpPrev_ = 0.0f;
    int16_t NES_APU::ring_[NES_APU::kRingSize];
    std::atomic<uint32_t> NES_APU::ringHead_{0};
    std::atomic<uint32_t> NES_APU::ringTail_{0};
    int16_t NES_APU::lastSample_ = 0;
    bool NES_APU::filtersPrimed_ = false;

    namespace
    {
        std::vector<int16_t>& WavSamples() { static std::vector<int16_t> v; return v; }
        std::string& WavPath() { static std::string p; return p; }
    }

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
        sampleSum_ = 0.0;
        sampleCount_ = 0;
        hp1PrevIn_ = hp1PrevOut_ = hp2PrevIn_ = hp2PrevOut_ = lpPrev_ = 0.0f;
        filtersPrimed_ = false;
        cyclesPerSampleFor_ = sampleRateHz_.load();
        cyclesPerSample_ = kCpuClockHzNTSC / cyclesPerSampleFor_;
        ringTail_.store(ringHead_.load()); // drop queued sound of the previous game

        // NES_AUDIO_WAV=<file.wav>: record the sound into a file (works without a sound device, too).
        static bool wavChecked = false;
        if (!wavChecked)
        {
            wavChecked = true;
            if (const char* path = NES_GETENV("NES_AUDIO_WAV"))
            {
                WavPath() = path;
                WavSamples().reserve(1 << 20);
                generateSamples_.store(true);
                std::atexit([] { WriteWav(WavPath().c_str()); });
            }
        }
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
    //   4-step: quarter at 3728, quarter+half at 7456, quarter at 11185, quarter+half (+IRQ) at 14914
    //   5-step: quarter at 3728, quarter+half at 7456, quarter at 11185, nothing at 14914, quarter+half at 18640
    namespace
    {
        constexpr int kStep1 = 3728, kStep2 = 7456, kStep3 = 11185, kStep4 = 14914, kStep5 = 18640;
    }

    void NES_APU::ClockOneCpuCycle(bool sound)
    {
        // Triangle's timer and the DMC's timer clock every CPU cycle (DMC's rate table is in CPU cycles).
        // Without sound only what a program can observe is run: the DMC matters while it is playing
        // (reads memory, raises its IRQ flag); the tone timers matter for nothing but the output.
        if (sound)
            triangle_.ClockTimer();
        if (sound || dmc_.bytesRemaining > 0 || dmc_.sampleBufferFilled || !dmc_.silence)
            dmc_.ClockTimer();

        cpuCycleParity_ ^= 1;
        if (cpuCycleParity_ != 0)
            return; // pulse/noise/frame-sequencer clock every *2nd* CPU cycle

        if (sound)
        {
            pulse1_.ClockTimer();
            pulse2_.ClockTimer();
            noise_.ClockTimer();
        }

        ++frameSequencerCounter_;
        const int n = frameSequencerCounter_;
        bool isQuarter = n == kStep1 || n == kStep2 || n == kStep3 || (!fiveStepMode_ && n == kStep4) || (fiveStepMode_ && n == kStep5);
        bool isHalf = n == kStep2 || (!fiveStepMode_ && n == kStep4) || (fiveStepMode_ && n == kStep5);

        if (isQuarter)
            ClockQuarterFrame();
        if (isHalf)
            ClockHalfFrame();

        if (!fiveStepMode_ && n == kStep4 && !frameIrqInhibit_)
            frameIrqFlag_ = true;

        if (n >= (fiveStepMode_ ? kStep5 : kStep4))
            frameSequencerCounter_ = 0;
    }

    // http://wiki.nesdev.com/w/index.php/APU_Mixer - the "lookup table" approximation of the non-linear
    // mixer: pulse_table[n] = 95.52 / (8128/n + 100), tnd_table[n] = 163.67 / (24329/n + 100).
    float NES_APU::Mix(uint8_t pulse1, uint8_t pulse2, uint8_t triangle, uint8_t noise, uint8_t dmc)
    {
        static const struct Tables
        {
            float pulse[31];
            float tnd[203];
            Tables()
            {
                pulse[0] = 0.0f;
                for (int n = 1; n < 31; ++n)
                    pulse[n] = 95.52f / (8128.0f / static_cast<float>(n) + 100.0f);
                tnd[0] = 0.0f;
                for (int n = 1; n < 203; ++n)
                    tnd[n] = 163.67f / (24329.0f / static_cast<float>(n) + 100.0f);
            }
        } tables;
        return tables.pulse[pulse1 + pulse2] + tables.tnd[3 * triangle + 2 * noise + dmc];
    }

    std::array<uint8_t, 5> NES_APU::ChannelOutputs()
    {
        return { pulse1_.Output(), pulse2_.Output(), triangle_.Output(), noise_.Output(), dmc_.Output() };
    }

    int NES_APU::LengthCounterValue(int channel)
    {
        switch (channel)
        {
            case 0: return pulse1_.length.value;
            case 1: return pulse2_.length.value;
            case 2: return triangle_.length.value;
            default: return noise_.length.value;
        }
    }

    // One finished output sample (average of the mixer over one sample period) -> console filters -> queue.
    void NES_APU::PushSample(float mixed)
    {
        // First-order filters as on the console: y = a*(y + x - xPrev) for high-pass, y += b*(x - y) for low-pass.
        const float dt = static_cast<float>(1.0 / cyclesPerSampleFor_);
        auto highPass = [dt](float cutoffHz, float x, float& prevIn, float& prevOut) {
            const float rc = 1.0f / (2.0f * 3.14159265f * cutoffHz);
            const float a = rc / (rc + dt);
            prevOut = a * (prevOut + x - prevIn);
            prevIn = x;
            return prevOut;
        };
        if (!filtersPrimed_)
        {
            // start from the current level instead of from 0: no loud pop when sound begins
            filtersPrimed_ = true;
            hp1PrevIn_ = mixed;
        }
        float y = highPass(90.0f, mixed, hp1PrevIn_, hp1PrevOut_);
        y = highPass(440.0f, y, hp2PrevIn_, hp2PrevOut_);
        const float rcLow = 1.0f / (2.0f * 3.14159265f * 14000.0f);
        lpPrev_ += (dt / (rcLow + dt)) * (y - lpPrev_);

        int sample = static_cast<int>(std::lround(lpPrev_ * 40000.0f * static_cast<float>(volume_.load(std::memory_order_relaxed)) / 100.0f));
        sample = std::clamp(sample, -32768, 32767);
        const int16_t s = static_cast<int16_t>(sample);

        if (!WavPath().empty())
            WavSamples().push_back(s);

        const uint32_t head = ringHead_.load(std::memory_order_relaxed);
        const uint32_t tail = ringTail_.load(std::memory_order_acquire);
        if (head - tail >= kMaxQueuedSamples)
            return; // audio thread is behind (or emulation runs faster than real time): drop, keep latency low
        ring_[head & (kRingSize - 1)] = s;
        ringHead_.store(head + 1, std::memory_order_release);
    }

    void NES_APU::Advance(int cpuCycles)
    {
        if (!generateSamples_.load(std::memory_order_relaxed))
        {
            for (int i = 0; i < cpuCycles; ++i)
                ClockOneCpuCycle(false);
            return;
        }
        if (cyclesPerSampleFor_ != sampleRateHz_.load(std::memory_order_relaxed))
        {
            // The sound device opened (or changed rate) while the emulation was already running.
            cyclesPerSampleFor_ = sampleRateHz_.load(std::memory_order_relaxed);
            cyclesPerSample_ = kCpuClockHzNTSC / cyclesPerSampleFor_;
        }
        for (int i = 0; i < cpuCycles; ++i)
        {
            ClockOneCpuCycle(true);
            sampleSum_ += Mix(pulse1_.Output(), pulse2_.Output(), triangle_.Output(), noise_.Output(), dmc_.Output());
            ++sampleCount_;
            cycleAccumulator_ += 1.0;
            if (cycleAccumulator_ >= cyclesPerSample_)
            {
                cycleAccumulator_ -= cyclesPerSample_;
                PushSample(static_cast<float>(sampleSum_ / sampleCount_));
                sampleSum_ = 0.0;
                sampleCount_ = 0;
            }
        }
    }

    void NES_APU::FillAudioBuffer(int16_t* buffer, int numSamples)
    {
        uint32_t tail = ringTail_.load(std::memory_order_relaxed);
        const uint32_t head = ringHead_.load(std::memory_order_acquire);
        for (int i = 0; i < numSamples; ++i)
        {
            if (tail != head)
                lastSample_ = ring_[tail++ & (kRingSize - 1)];
            buffer[i] = lastSample_; // queue empty: hold the last value (no click)
        }
        ringTail_.store(tail, std::memory_order_release);
    }

    bool NES_APU::WriteWav(const char* path)
    {
        const std::vector<int16_t>& v = WavSamples();
        FILE* f = std::fopen(path, "wb");
        if (!f)
            return false;
        auto w32 = [f](uint32_t x) { std::fwrite(&x, 4, 1, f); };
        auto w16 = [f](uint16_t x) { std::fwrite(&x, 2, 1, f); };
        const uint32_t dataBytes = static_cast<uint32_t>(v.size() * 2);
        std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
        w32(16); w16(1); w16(1); w32(static_cast<uint32_t>(sampleRateHz_.load())); w32(static_cast<uint32_t>(sampleRateHz_.load()) * 2); w16(2); w16(16);
        std::fwrite("data", 1, 4, f); w32(dataBytes);
        std::fwrite(v.data(), 2, v.size(), f);
        std::fclose(f);
        return true;
    }
}
