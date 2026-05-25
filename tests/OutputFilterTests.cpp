#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "OutputFilter.h"

// Task 03 — Model-1 RC low-pass + light high-shelf for the mix bus.
//
// The filter coefficients are fixed at calibration values (see OutputFilter.cpp
// header notes). These tests pin the audible behaviour — passband leak, stop-
// band attenuation, and bypass identity — so a later coefficient retune can't
// regress them silently.

namespace
{
    constexpr double kSampleRate = 44100.0;

    // Render N blocks of a steady sine, then measure the RMS of the last
    // block as a steady-state level. Pre-rolls the filter so transients
    // settle. Returns linear RMS (not dB).
    float steadyStateRms (OutputFilter& f, float freqHz, float amplitude,
                          double sampleRate, bool enabled, int blockSize = 1024,
                          int prerollBlocks = 4, int measureBlocks = 1)
    {
        juce::AudioBuffer<float> buf (1, blockSize);
        const double phaseInc = 2.0 * juce::MathConstants<double>::pi
                                    * freqHz / sampleRate;
        double phase = 0.0;

        for (int b = 0; b < prerollBlocks + measureBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                buf.setSample (0, i, amplitude * static_cast<float> (std::sin (phase)));
                phase += phaseInc;
                if (phase > 2.0 * juce::MathConstants<double>::pi)
                    phase -= 2.0 * juce::MathConstants<double>::pi;
            }
            f.process (buf, enabled);
        }

        double sum = 0.0;
        for (int i = 0; i < blockSize; ++i)
        {
            const double s = buf.getSample (0, i);
            sum += s * s;
        }
        return static_cast<float> (std::sqrt (sum / static_cast<double> (blockSize)));
    }
}

// --- Passband (200 Hz) - filter on - within ~0.5 dB of input level ---------

TEST (OutputFilter, PassbandLeavesLowFrequencyMostlyIntact)
{
    OutputFilter f;
    f.prepare (kSampleRate);

    const float inputAmp     = 0.5f;
    const float expectedRms  = inputAmp / static_cast<float> (std::sqrt (2.0));
    const float outRms       = steadyStateRms (f, 200.0f, inputAmp, kSampleRate, true);
    const float gainDb       = 20.0f * std::log10 (outRms / expectedRms);

    EXPECT_GT (gainDb, -0.5f) << "200 Hz attenuated more than 0.5 dB";
    EXPECT_LT (gainDb,  1.0f) << "200 Hz boosted unexpectedly (shelf is +0.5 dB, well above 200 Hz)";
}

// --- Stopband (10 kHz) - filter on - at least -6 dB attenuation -----------

TEST (OutputFilter, StopbandAttenuates10kHzSubstantially)
{
    OutputFilter f;
    f.prepare (kSampleRate);

    const float inputAmp     = 0.5f;
    const float expectedRms  = inputAmp / static_cast<float> (std::sqrt (2.0));
    const float outRms       = steadyStateRms (f, 10000.0f, inputAmp, kSampleRate, true);
    const float gainDb       = 20.0f * std::log10 (outRms / expectedRms);

    EXPECT_LT (gainDb, -6.0f) << "10 kHz attenuated by less than 6 dB";
}

// --- Bypass (enabled = false) returns identity for any input ---------------

TEST (OutputFilter, BypassProducesIdentityOutput)
{
    OutputFilter f;
    f.prepare (kSampleRate);

    juce::AudioBuffer<float> buf (1, 256);
    for (int i = 0; i < 256; ++i)
        buf.setSample (0, i, 0.7f * std::sin (i * 0.05f));

    juce::AudioBuffer<float> expected (1, 256);
    expected.makeCopyOf (buf);

    f.process (buf, /*enabled*/ false);

    for (int i = 0; i < 256; ++i)
        EXPECT_FLOAT_EQ (buf.getSample (0, i), expected.getSample (0, i)) << "i=" << i;
}

// --- Bypass adds no measurable cost beyond the early-return ----------------

TEST (OutputFilter, BypassDoesNotMutateBufferEvenWithNonZeroState)
{
    OutputFilter f;
    f.prepare (kSampleRate);

    // First run filter ON to put non-zero state in the LPF / shelf registers.
    juce::AudioBuffer<float> warm (1, 512);
    for (int i = 0; i < 512; ++i)
        warm.setSample (0, i, std::sin (i * 0.1f));
    f.process (warm, true);

    // Now run with bypass — input should be passed through verbatim despite
    // the filter holding state from the previous block.
    juce::AudioBuffer<float> buf (1, 16);
    for (int i = 0; i < 16; ++i)
        buf.setSample (0, i, 0.5f);

    f.process (buf, false);

    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ (buf.getSample (0, i), 0.5f);
}

// --- reset() clears filter state -------------------------------------------

TEST (OutputFilter, ResetClearsFilterState)
{
    OutputFilter f;
    f.prepare (kSampleRate);

    // Excite with a loud transient, leaving the LPF and shelf states ringing.
    juce::AudioBuffer<float> buf (1, 64);
    buf.setSample (0, 0, 1.0f);
    f.process (buf, true);

    f.reset();

    // After reset, feeding zeros yields silence (no decay tail).
    juce::AudioBuffer<float> zeros (1, 64);
    zeros.clear();
    f.process (zeros, true);

    for (int i = 0; i < 64; ++i)
        EXPECT_NEAR (zeros.getSample (0, i), 0.0f, 1.0e-6f);
}
