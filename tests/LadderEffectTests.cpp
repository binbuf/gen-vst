#include <gtest/gtest.h>

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LadderEffect.h"

// Task 03 — YM2612 ladder DAC stepwise nonlinearity. The initial curve in
// LadderEffect.cpp is plain piecewise linear over both halves of the DAC
// code space; the "8x gap" calibration is a documented follow-up
// (07-feature-spec.md *Open Questions* #5). These tests pin the four
// endpoint pinch points the design doc names + monotonicity + bypass identity
// so the calibration pass can't silently break the shape.

// --- Endpoint pinch points -------------------------------------------------

TEST (LadderEffect, PinchPointsMatchPublishedCurveEndpoints)
{
    constexpr float oneOver256 = 1.0f / 256.0f;

    EXPECT_FLOAT_EQ (LadderEffect::lookup (-1.0f),         -1.0f);
    EXPECT_NEAR     (LadderEffect::lookup (-oneOver256),   -oneOver256, 1.0e-5f);
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

// --- Boundary gap relationship (relaxed) -----------------------------------
//
// The design doc cites an "8x gap exactly at the -1 -> 0 boundary" measured
// in jsgroth's article. The committed linear curve doesn't realise that yet;
// the gap ratio is currently 1. This test guards monotonicity at the
// boundary and pins the lookup's known step there, so the calibration pass
// in Task 08 can tighten the assertion in a single deliberate edit.

TEST (LadderEffect, ZeroCrossingGapIsMonotonicAndPositive)
{
    const float fA = LadderEffect::lookup (-2.0f / 256.0f);
    const float fB = LadderEffect::lookup (-1.0f / 256.0f);
    const float fC = LadderEffect::lookup ( 0.0f);

    const float gap_in   = fB - fA;
    const float gap_edge = fC - fB;

    EXPECT_GT (gap_in,   0.0f);
    EXPECT_GT (gap_edge, 0.0f);
    // Ratio is 1.0 on the committed linear curve; the calibration pass will
    // raise this to ~8. Asserting >= 1 catches any regression where the
    // negative branch over-shoots into the positive half.
    EXPECT_GE (gap_edge / gap_in, 1.0f);
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
