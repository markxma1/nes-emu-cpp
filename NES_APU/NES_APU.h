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
#include <cstdint>
#include <mutex>

namespace NES
{
    /// @brief NES Audio Processing Unit: 2 pulse channels, triangle, noise
    /// and DMC (delta-modulation sample playback), mixed to one mono signal.
    ///
    /// New code, not a port of anything - the C# original never implemented
    /// audio (no NES_APU.cs exists anywhere in the original solution). Built
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
    /// LEARNING NOTE on timing: real hardware clocks the APU from the same
    /// 1.789773 MHz (NTSC) clock as the CPU, one APU/CPU cycle at a time - a
    /// cycle-accurate APU normally hangs directly off the CPU's own cycle
    /// counter. This port's NES_CPU::Step() (CPU/CPU/NES_CPU.cpp) executes
    /// one *whole instruction* per call and has never counted or exposed
    /// individual CPU cycles, even before this file existed - Display()/NMI
    /// are likewise already paced by the UI's ~60Hz render loop rather than
    /// real per-scanline PPU/CPU interleaving (see NES_PPU.Display.cpp).
    /// Wiring the APU to a per-cycle counter that doesn't exist wasn't
    /// possible without a much larger CPU-timing rewrite, so this APU
    /// instead free-runs its own clock in real wall-clock time, at the same
    /// fixed 1.789773 MHz rate, driven from the SDL audio callback (see
    /// NES_Audio/NES_Audio.cpp): every channel's period/envelope/sweep/
    /// sequencer math is exactly what nesdev documents, just paced by
    /// elapsed audio time instead of literal 6502 cycles. Register *writes*
    /// still land the instant the CPU thread executes e.g. `STA $4000` (see
    /// NES_APU_Register.cpp) - only the free-running channel/frame-sequencer
    /// clock is time-based rather than cycle-counted. Since pitch/tempo/
    /// envelope speed are entirely defined by this fixed clock rate (not by
    /// how many instructions execute), this produces correct-sounding audio;
    /// it just means audio isn't cycle-locked to CPU/PPU state, the same
    /// looseness this port's PPU/CPU relationship already has.
    class NES_APU
    {
    public:
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

        /// Renders `numSamples` mono 16-bit samples at `sampleRateHz` into
        /// `buffer`, advancing the emulated APU clock by the equivalent
        /// number of real CPU cycles as it goes (see the class comment on
        /// why this - not a per-instruction hook - is what paces the APU in
        /// this port). Called from the SDL audio callback; takes the
        /// internal lock once for the whole batch rather than per sample.
        static void FillAudioBuffer(int16_t* buffer, int numSamples, double sampleRateHz);

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
        static int cpuCycleParity_;        // toggles every CPU cycle; pulse/noise/DMA... wait DMC uses its own

        static double cycleAccumulator_;
        static float dcPrevIn_;
        static float dcPrevOut_;
        static std::mutex mutex_;

        static void ClockOneCpuCycle();
        static void ClockQuarterFrame();
        static void ClockHalfFrame();
        static float MixCurrentOutput();

        static const uint8_t kLengthTable[32];
        static const uint16_t kNoisePeriodTableNTSC[16];
        static const uint16_t kDmcRateTableNTSC[16];
        static const uint8_t kTriangleSequence[32];
        static const uint8_t kDutyTable[4][8];
    };
}
