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
#include <array>
#include <atomic>
#include <cstdint>

namespace NES
{
    /// @brief NES Audio Processing Unit: 2 pulse channels, triangle, noise
    /// and DMC (delta-modulation sample playback), mixed to one mono signal.
    ///
    /// Built
    /// from scratch against nesdev's APU documentation:
    /// - http://wiki.nesdev.com/w/index.php/APU (register map, clocking)
    /// - http://wiki.nesdev.com/w/index.php/APU_Pulse (duty tables, timer)
    /// - http://wiki.nesdev.com/w/index.php/APU_Sweep (sweep unit)
    /// - http://wiki.nesdev.com/w/index.php/APU_Envelope (envelope generator)
    /// - http://wiki.nesdev.com/w/index.php/APU_Triangle (sequence, linear counter)
    /// - http://wiki.nesdev.com/w/index.php/APU_Noise (LFSR, period table)
    /// - http://wiki.nesdev.com/w/index.php/APU_DMC (rate table, output unit)
    /// - http://wiki.nesdev.com/w/index.php/APU_Length_Counter (length table)
    /// - http://wiki.nesdev.com/w/index.php/APU_Frame_Counter (sequencer)
    /// - http://wiki.nesdev.com/w/index.php/APU_Mixer (non-linear mixing)
    ///
    /// LEARNING NOTE on timing: the APU runs on the same clock as the CPU (1.789773 MHz NTSC). After
    /// every CPU instruction NES_CPU::Run() calls Advance() with the cycles that instruction took, so the
    /// APU is clocked one CPU cycle at a time, in step with the emulated program (not with wall-clock
    /// time). Every output sample is the average of the ~40 mixer values of one sample period (box
    /// filter, avoids aliasing), followed by the console's own filters (high-pass 90 Hz and 440 Hz,
    /// low-pass 14 kHz). The samples go through a lock-free queue to the SDL audio thread
    /// (FillAudioBuffer()). Without a sound device the sample generation is skipped completely.
    class NES_APU
    {
    public:
        /// NTSC CPU clock in Hz, used to convert CPU cycles to audio samples.
        static constexpr double kCpuClockHzNTSC = 1789773.0;

        /// Clears all channel/register state - call on power-on and whenever
        /// a new ROM is loaded, so leftover notes/envelopes from the
        /// previous game don't bleed into the next one.
        static void Reset();

        /// CPU write to $4000-$4013/$4015/$4017. Hooked up in
        /// NES_APU_Register.cpp via AddressSetup::AfterSet, the same pattern
        /// NES_PPU_Register.cpp uses for PPU registers.
        static void WriteRegister(uint16_t address, uint8_t value);

        /// CPU read of $4015 (channel-active / IRQ-flag status). Clears the
        /// frame-IRQ flag as a read side effect, per nesdev.
        static uint8_t ReadStatus();

        /// Fills `buffer` with `numSamples` mono 16-bit samples that Advance() has produced (repeats the last
        /// sample when the emulation is behind). Called from the SDL audio callback.
        static void FillAudioBuffer(int16_t* buffer, int numSamples);

        /// Runs the APU for `cpuCycles` CPU cycles (called by the CPU loop after every instruction, so
        /// registers, length counters, envelopes and the frame counter follow emulated time exactly).
        /// When sound is wanted (see SetGenerateSamples()) it also produces audio samples.
        static void Advance(int cpuCycles);

        /// Sound output on/off. Off: only the parts a program can observe (length counters, frame
        /// counter, DMC) are run, which is much cheaper; on: every channel is clocked every cycle and
        /// samples are produced for FillAudioBuffer().
        static void SetGenerateSamples(bool on) { generateSamples_.store(on, std::memory_order_relaxed); }
        /// True while samples are produced.
        static bool GenerateSamples() { return generateSamples_.load(std::memory_order_relaxed); }
        /// Output sample rate in Hz (default 44100); set before sound starts.
        static void SetSampleRate(double hz) { sampleRateHz_.store(hz, std::memory_order_relaxed); }

        /// Mixes one set of channel outputs (pulse 0-15 each, triangle 0-15, noise 0-15, DMC 0-127) with the
        /// NES's non-linear mixer, result 0.0 - 1.0 (see https://www.nesdev.org/wiki/APU_Mixer).
        static float Mix(uint8_t pulse1, uint8_t pulse2, uint8_t triangle, uint8_t noise, uint8_t dmc);

        /// Current channel outputs (pulse1, pulse2, triangle, noise, dmc), for tests and debugging.
        static std::array<uint8_t, 5> ChannelOutputs();
        /// Remaining length counter of channel 0 = pulse1, 1 = pulse2, 2 = triangle, 3 = noise (for tests).
        static int LengthCounterValue(int channel);
        /// Noise channel shift register (for tests).
        static uint16_t NoiseShiftRegister() { return noise_.shiftRegister; }
        /// Writes everything produced so far to a 16-bit mono WAV file (also done automatically at exit
        /// when the environment variable `NES_AUDIO_WAV=<file.wav>` is set).
        static bool WriteWav(const char* path);

    private:
        struct Envelope
        {
            bool start = false;
            uint8_t divider = 0;
            uint8_t decayLevel = 0;
            bool loop = false;          // also the length-counter halt flag
            bool constantVolume = false;
            uint8_t volumeParam = 0;    // constant volume, or envelope divider period

            void Restart() { start = true; }
            void Clock();
            uint8_t Output() const { return constantVolume ? volumeParam : decayLevel; }
        };

        struct LengthCounter
        {
            bool enabled = false; // this channel's bit in $4015
            uint8_t value = 0;

            void Load(uint8_t index);
            void Clock(bool halt);
            bool Silenced() const { return value == 0; }
        };

        struct Pulse
        {
            bool isChannel2 = false; // ones'- vs two's-complement sweep negate
            Envelope envelope;
            LengthCounter length;

            uint8_t duty = 0;
            uint16_t timerPeriod = 0; // 11-bit raw period as written by the CPU
            int timerCounter = 0;     // counts down in CPU cycles
            uint8_t dutyStep = 0;

            bool sweepEnabled = false;
            uint8_t sweepPeriod = 0;
            bool sweepNegate = false;
            uint8_t sweepShift = 0;
            bool sweepReload = false;
            uint8_t sweepDivider = 0;

            void WriteReg0(uint8_t v);
            void WriteReg1(uint8_t v);
            void WriteReg2(uint8_t v);
            void WriteReg3(uint8_t v);
            void ClockTimer();
            void ClockSweep();
            uint16_t SweepTarget() const;
            bool SweepMutes() const;
            uint8_t Output() const;
        };

        struct Triangle
        {
            LengthCounter length;

            uint16_t timerPeriod = 0;
            int timerCounter = 0;
            uint8_t sequenceStep = 0;

            uint8_t linearReloadValue = 0;
            uint8_t linearCounter = 0;
            bool linearReloadFlag = false;
            bool control = false; // shared with length.halt

            void WriteReg0(uint8_t v);
            void WriteReg2(uint8_t v);
            void WriteReg3(uint8_t v);
            void ClockTimer();
            void ClockLinearCounter();
            uint8_t Output() const;
        };

        struct Noise
        {
            Envelope envelope;
            LengthCounter length;

            bool mode = false;
            uint16_t timerPeriod = 4;
            int timerCounter = 0;
            uint16_t shiftRegister = 1;

            void WriteReg0(uint8_t v);
            void WriteReg2(uint8_t v);
            void WriteReg3(uint8_t v);
            void ClockTimer();
            uint8_t Output() const;
        };

        struct DMC
        {
            bool irqEnabled = false;
            bool loop = false;
            uint16_t rate = 428;
            int timerCounter = 0;

            uint16_t sampleAddress = 0xC000;
            uint16_t sampleLength = 1;
            uint16_t currentAddress = 0;
            uint16_t bytesRemaining = 0;

            uint8_t outputLevel = 64;

            bool sampleBufferFilled = false;
            uint8_t sampleBuffer = 0;

            uint8_t shiftRegister = 0;
            uint8_t bitsRemaining = 8;
            bool silence = true;

            bool enabled = false;
            bool interruptFlag = false;

            void WriteReg0(uint8_t v);
            void WriteReg1(uint8_t v);
            void WriteReg2(uint8_t v);
            void WriteReg3(uint8_t v);
            void Restart();
            void FillSampleBufferIfNeeded();
            void ClockTimer();
            uint8_t Output() const { return outputLevel; }
        };

        static Pulse pulse1_;
        static Pulse pulse2_;
        static Triangle triangle_;
        static Noise noise_;
        static DMC dmc_;

        static bool frameIrqInhibit_;
        static bool frameIrqFlag_;
        static bool fiveStepMode_;
        static int frameSequencerCounter_; // in APU cycles (1 APU cycle = 2 CPU cycles)
        static int cpuCycleParity_;        // toggles every CPU cycle; pulse and noise run on every 2nd one

        // Set by the main thread when the sound device opens, read by the CPU thread.
        static std::atomic<bool> generateSamples_;
        static std::atomic<double> sampleRateHz_;
        static double cycleAccumulator_;      // CPU cycles since the last output sample
        static double sampleSum_;             // sum of the mixer output over those cycles (box filter)
        static int sampleCount_;
        // NES output filters: high-pass 90 Hz, high-pass 440 Hz, low-pass 14 kHz (first order each)
        static float hp1PrevIn_, hp1PrevOut_, hp2PrevIn_, hp2PrevOut_, lpPrev_;
        static void PushSample(float mixed);
        // Sample queue between the CPU thread (writer) and the audio thread (reader).
        static constexpr uint32_t kRingSize = 1u << 15;
        static constexpr uint32_t kMaxQueuedSamples = 4096; // ~93 ms: keeps sound latency low
        static int16_t ring_[kRingSize];
        static std::atomic<uint32_t> ringHead_; // next write index (CPU thread)
        static std::atomic<uint32_t> ringTail_; // next read index (audio thread)
        static int16_t lastSample_;
        static bool filtersPrimed_;

        static void ClockOneCpuCycle(bool sound);
        static double cyclesPerSample_;         // CPU cycles per output sample (CPU thread only)
        static double cyclesPerSampleFor_;      // sample rate that value was computed for
        static void ClockQuarterFrame();
        static void ClockHalfFrame();

        static const uint8_t kLengthTable[32];
        static const uint16_t kNoisePeriodTableNTSC[16];
        static const uint16_t kDmcRateTableNTSC[16];
        static const uint8_t kTriangleSequence[32];
        static const uint8_t kDutyTable[4][8];
    };
}
