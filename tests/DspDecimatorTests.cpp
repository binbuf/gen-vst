#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "DspDecimator.h"

// Task 03 — sample-and-hold + 8-bit quantiser for D mode's PRESCALER knob
// (also feeds FM's `fm_dac_prescaler` once Task 05 wires it).

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr int    kBlockSize  = 256;

    // Fill a buffer with a ramp from -1 to +1 so the per-sample identity check
    // exercises both halves of the 8-bit quantiser.
    void fillRamp (juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = -1.0f + 2.0f * static_cast<float> (i) / static_cast<float> (n - 1);
        }
    }

    // Measure the average hold-run length on channel 0 by counting how many
    // consecutive samples share the same value. Returns the mean run length
    // across `n - 1` transitions.
    double measureHoldRun (const juce::AudioBuffer<float>& buf, int ch = 0)
    {
        const int n = buf.getNumSamples();
        const float* d = buf.getReadPointer (ch);
        int runs = 0, runLen = 0, totalLen = 0;
        float prev = d[0];
        runLen = 1;
        for (int i = 1; i < n; ++i)
        {
            if (d[i] == prev)
            {
                ++runLen;
            }
            else
            {
                totalLen += runLen;
                ++runs;
                runLen = 1;
                prev = d[i];
            }
        }
        totalLen += runLen;
        ++runs;
        return static_cast<double> (totalLen) / static_cast<double> (runs);
    }
}

// --- holdSamples mapping ---------------------------------------------------

TEST (DspDecimator, HoldSamplesMappingEndpoints)
{
    EXPECT_EQ (DspDecimator::holdSamples (0.0f), 1);
    EXPECT_EQ (DspDecimator::holdSamples (1.0f), 16);
    EXPECT_EQ (DspDecimator::holdSamples (0.5f), 9);   // round(1 + 15*0.5) = round(8.5) = 9
}

TEST (DspDecimator, HoldSamplesClampsOutOfRangeInput)
{
    EXPECT_EQ (DspDecimator::holdSamples (-1.0f), 1);
    EXPECT_EQ (DspDecimator::holdSamples ( 2.0f), 16);
}

// --- prescaler = 0 keeps every sample (identity, modulo 8-bit quantise) ----

TEST (DspDecimator, PrescalerZeroIsIdentityForQuantisedRamp)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf (1, kBlockSize);
    fillRamp (buf);

    // Bake the same 8-bit quantisation the decimator applies so the "identity"
    // claim is meaningful: even at prescaler=0, each sample is re-grabbed and
    // re-quantised — sub-1/128 deltas vanish, but the run length stays 1.
    juce::AudioBuffer<float> expected (1, kBlockSize);
    for (int i = 0; i < kBlockSize; ++i)
    {
        const float raw     = buf.getSample (0, i);
        const float bucket  = std::clamp (std::round (raw * 128.0f), -128.0f, 127.0f);
        expected.setSample (0, i, bucket / 128.0f);
    }

    dec.process (buf, 0.0f);

    // Every output sample matches the quantised expected value exactly.
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_NEAR (buf.getSample (0, i), expected.getSample (0, i), 1.0e-6f) << "i=" << i;

    // Hold-run length 1 at prescaler 0 (no held repeats beyond accidental
    // ties from the ramp quantising into the same bucket — the input ramp
    // crosses every 8-bit bucket so accidental ties are minimal).
    const double meanRun = measureHoldRun (buf);
    EXPECT_LT (meanRun, 3.0);
}

// --- prescaler = 0.5 -> ~9-sample hold (round(1 + 15*0.5) = 9) -------------

TEST (DspDecimator, PrescalerMidHoldsForExpectedRun)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf (1, kBlockSize);
    fillRamp (buf);

    dec.process (buf, 0.5f);

    const double meanRun = measureHoldRun (buf);
    // The decimator samples then holds for holdSamples(0.5) = 9 samples; the
    // first run is partial. Tolerate ±1 around the target.
    EXPECT_GE (meanRun, 7.0);
    EXPECT_LE (meanRun, 11.0);
}

// --- prescaler = 1 -> ~16-sample hold (max crush) --------------------------

TEST (DspDecimator, PrescalerMaxHoldsForSixteenSamples)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf (1, kBlockSize);
    fillRamp (buf);

    dec.process (buf, 1.0f);

    const double meanRun = measureHoldRun (buf);
    EXPECT_GE (meanRun, 12.0);
    EXPECT_LE (meanRun, 18.0);
}

// --- 8-bit quantisation collapses sub-1/128 differences --------------------

TEST (DspDecimator, EightBitQuantisationCollapsesSubBucketDeltas)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    // Four values inside the same 8-bit bucket (39/128 = [0.30078..0.30859]):
    // should all quantise to 39/128 = 0.3046875.
    juce::AudioBuffer<float> buf (1, 4);
    buf.setSample (0, 0, 0.301f);
    buf.setSample (0, 1, 0.303f);
    buf.setSample (0, 2, 0.305f);
    buf.setSample (0, 3, 0.307f);

    dec.process (buf, 0.0f);

    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR (buf.getSample (0, i), 39.0f / 128.0f, 1.0e-6f) << "i=" << i;
}

// --- per-channel state is independent --------------------------------------

TEST (DspDecimator, PerChannelStateIndependent)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf (2, kBlockSize);
    // Left = ramp -1..+1, right = ramp +1..-1 (opposite direction).
    for (int i = 0; i < kBlockSize; ++i)
    {
        const float u = static_cast<float> (i) / static_cast<float> (kBlockSize - 1);
        buf.setSample (0, i, -1.0f + 2.0f * u);
        buf.setSample (1, i,  1.0f - 2.0f * u);
    }

    dec.process (buf, 1.0f);

    // Hold pattern (~16 samples) on each channel, independently.
    EXPECT_NEAR (measureHoldRun (buf, 0), 16.0, 4.0);
    EXPECT_NEAR (measureHoldRun (buf, 1), 16.0, 4.0);

    // Channels differ in value: L starts very negative, R starts very positive.
    EXPECT_LT (buf.getSample (0, 0), 0.0f);
    EXPECT_GT (buf.getSample (1, 0), 0.0f);
}

// --- reset() clears state --------------------------------------------------

TEST (DspDecimator, ResetClearsHeldSample)
{
    DspDecimator dec;
    dec.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buf (1, kBlockSize);
    // Fill with a constant non-zero value so the held sample is non-zero.
    for (int i = 0; i < kBlockSize; ++i)
        buf.setSample (0, i, 0.5f);
    dec.process (buf, 1.0f);

    dec.reset();

    // After reset, the counter is 0, so the first sample of the next block
    // is freshly captured rather than the stale 0.5.
    for (int i = 0; i < kBlockSize; ++i)
        buf.setSample (0, i, -0.5f);
    dec.process (buf, 1.0f);

    EXPECT_LT (buf.getSample (0, 0), 0.0f);   // new capture, negative
}
