#include <gtest/gtest.h>

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LadderEffect.h"

// Task 03 — YM2612 ladder DAC stepwise nonlinearity. The curve in
// LadderEffect.cpp is piecewise linear over both halves of the DAC code
// space, with the negative branch shifted to realise the ~8× gap at the
// zero crossing per jsgroth's hardware measurements. These tests pin the
// four endpoint pinch points the design doc names + monotonicity +
// bypass identity so the calibrated shape can't silently regress.

// --- Endpoint pinch points -------------------------------------------------

TEST (LadderEffect, PinchPointsMatchPublishedCurveEndpoints)
{
    constexpr float oneOver256 = 1.0f / 256.0f;

    EXPECT_FLOAT_EQ (LadderEffect::lookup (-1.0f),         -1.0f);
    EXPECT_NEAR     (LadderEffect::lookup (-oneOver256),   -8.0f * oneOver256, 1.0e-5f);
    EXPECT_FLOAT_EQ (LadderEffect::lookup ( 0.0f),          0.0f);
    EXPECT_NEAR     (LadderEffect::lookup ( oneOver256),    oneOver256, 1.0e-5f);
    EXPECT_FLOAT_EQ (LadderEffect::lookup ( 1.0f),          1.0f);
}

// --- Curve is monotonic non-decreasing across the table --------------------
//
// A future calibration pass may make the zero-crossing non-uniform but the
// curve must remain monotonic — otherwise the lookup would distort the
// signal in audibly wrong ways (sign-inversions on small samples).

TEST (LadderEffect, TableIsMonotonicNonDecreasing)
{
    const auto& t = LadderEffect::table();
    ASSERT_EQ (t.size(), (std::size_t) LadderEffect::kTableSize);
    for (std::size_t i = 1; i < t.size(); ++i)
        EXPECT_GE (t[i], t[i - 1]) << "non-monotone at index " << i;
}

// --- Boundary gap relationship (calibrated) --------------------------------
//
// The design doc cites an "8x gap exactly at the -1 -> 0 boundary" measured
// in jsgroth's article. With the negative branch shifted so DAC code -1
// sits at -8/256, the realised ratio lands around 8.2× (integer rounding
// inside the lookup nudges it slightly off a pure 8.0). Bound the ratio
// to [6, 10] so a regression in either direction (drifting back toward 1
// or over-amplifying past hardware) trips the test.

TEST (LadderEffect, ZeroCrossingGapIsMonotonicAndPositive)
{
    const float fA = LadderEffect::lookup (-2.0f / 256.0f);
    const float fB = LadderEffect::lookup (-1.0f / 256.0f);
    const float fC = LadderEffect::lookup ( 0.0f);

    const float gap_in   = fB - fA;
    const float gap_edge = fC - fB;

    EXPECT_GT (gap_in,   0.0f);
    EXPECT_GT (gap_edge, 0.0f);
    // Calibrated ~8.2× gap at the zero crossing.
    EXPECT_GE (gap_edge / gap_in,  6.0f);
    EXPECT_LE (gap_edge / gap_in, 10.0f);
}

// --- Bypass produces identity ----------------------------------------------

TEST (LadderEffect, BypassProducesIdentityOutput)
{
    LadderEffect le;
    le.prepare (44100.0);

    juce::AudioBuffer<float> buf (1, 128);
    for (int i = 0; i < 128; ++i)
        buf.setSample (0, i, -1.0f + (2.0f * i) / 127.0f);

    juce::AudioBuffer<float> expected (1, 128);
    expected.makeCopyOf (buf);

    le.process (buf, /*enabled*/ false);

    for (int i = 0; i < 128; ++i)
        EXPECT_FLOAT_EQ (buf.getSample (0, i), expected.getSample (0, i)) << "i=" << i;
}

// --- Out-of-range inputs are clamped ---------------------------------------

TEST (LadderEffect, OutOfRangeInputsAreClampedToTableEdges)
{
    EXPECT_FLOAT_EQ (LadderEffect::lookup (-10.0f), -1.0f);
    EXPECT_FLOAT_EQ (LadderEffect::lookup ( 10.0f),  1.0f);
}

// --- Process honours channel count -----------------------------------------

TEST (LadderEffect, ProcessAppliesToAllChannels)
{
    LadderEffect le;
    le.prepare (44100.0);

    juce::AudioBuffer<float> buf (2, 8);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            buf.setSample (ch, i, ch == 0 ? -1.0f : 1.0f);

    le.process (buf, /*enabled*/ true);

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_FLOAT_EQ (buf.getSample (0, i), -1.0f);
        EXPECT_FLOAT_EQ (buf.getSample (1, i),  1.0f);
    }
}
