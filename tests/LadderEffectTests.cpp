#include <gtest/gtest.h>

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LadderEffect.h"

// YM2612 ladder DAC stepwise nonlinearity, used in D mode only (FM mode
// dispatches between ym2612/ym3438 inside Voice::renderAdd). The curve
// mirrors ymfm's `dac_discontinuity`: non-negative codes get +4, negative
// codes get -3, normalised by 256. These tests pin the endpoint pinch
// points + monotonicity + bypass identity so the curve can't silently
// regress.

// --- Endpoint pinch points -------------------------------------------------

TEST (LadderEffect, PinchPointsMatchYmfmDiscontinuity)
{
    constexpr float kInv256 = 1.0f / 256.0f;

    // code -256 → (-256 - 3) / 256 = -259/256
    EXPECT_NEAR (LadderEffect::lookup (-1.0f),         -259.0f * kInv256, 1.0e-5f);
    // code -1   → (-1 - 3) / 256 = -4/256
    EXPECT_NEAR (LadderEffect::lookup (-kInv256),       -4.0f * kInv256, 1.0e-5f);
    // code  0   → ( 0 + 4) / 256 = +4/256
    EXPECT_NEAR (LadderEffect::lookup ( 0.0f),          +4.0f * kInv256, 1.0e-5f);
    // code +1   → (+1 + 4) / 256 = +5/256
    EXPECT_NEAR (LadderEffect::lookup ( kInv256),       +5.0f * kInv256, 1.0e-5f);
    // code +255 → (+255 + 4) / 256 = +259/256
    EXPECT_NEAR (LadderEffect::lookup ( 1.0f),         +259.0f * kInv256, 1.0e-5f);
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
// ymfm's +4/-3 bias gives a gap of 8 codes between code -1 (-4/256) and code
// 0 (+4/256), while the normal step between consecutive codes is 1. Expected
// ratio is exactly 8.0; bound to [7.5, 8.5] to accommodate float rounding.

TEST (LadderEffect, ZeroCrossingGapMatchesYmfmEightTimes)
{
    const float fA = LadderEffect::lookup (-2.0f / 256.0f);
    const float fB = LadderEffect::lookup (-1.0f / 256.0f);
    const float fC = LadderEffect::lookup ( 0.0f);

    const float gap_in   = fB - fA;
    const float gap_edge = fC - fB;

    EXPECT_GT (gap_in,   0.0f);
    EXPECT_GT (gap_edge, 0.0f);
    EXPECT_GE (gap_edge / gap_in, 7.5f);
    EXPECT_LE (gap_edge / gap_in, 8.5f);
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
    constexpr float kInv256 = 1.0f / 256.0f;
    EXPECT_NEAR (LadderEffect::lookup (-10.0f), -259.0f * kInv256, 1.0e-5f);
    EXPECT_NEAR (LadderEffect::lookup ( 10.0f), +259.0f * kInv256, 1.0e-5f);
}

// --- Process honours channel count -----------------------------------------

TEST (LadderEffect, ProcessAppliesToAllChannels)
{
    LadderEffect le;
    le.prepare (44100.0);

    constexpr float kInv256 = 1.0f / 256.0f;

    juce::AudioBuffer<float> buf (2, 8);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            buf.setSample (ch, i, ch == 0 ? -1.0f : 1.0f);

    le.process (buf, /*enabled*/ true);

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_NEAR (buf.getSample (0, i), -259.0f * kInv256, 1.0e-5f);
        EXPECT_NEAR (buf.getSample (1, i), +259.0f * kInv256, 1.0e-5f);
    }
}
