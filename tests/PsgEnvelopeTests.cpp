#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "SN76489Engine.h"

// Task 23 — software amplitude ADSR for PSG channels.
//
// The SN76489 has no envelope hardware; SN76489Engine synthesises one in
// software (`PsgEnvelope`) and multiplies its amplitude into the per-channel
// mix gain. These tests drive the engine through the public API and observe
// the envelope via channelAmplitude / channelStage so we don't depend on the
// chip's float output samples (which are 4-bit-quantised and noisy).

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr int    kBlockSize  = 256;

    // Run `numBlocks` consecutive renderAdd calls with a fresh zero-filled
    // L/R buffer each block. Pure side-effect: advances the envelope.
    void renderBlocks (SN76489Engine& eng, int numBlocks, int blockSize = kBlockSize)
    {
        std::vector<float> L ((std::size_t) blockSize, 0.0f);
        std::vector<float> R ((std::size_t) blockSize, 0.0f);
        for (int b = 0; b < numBlocks; ++b)
        {
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            eng.renderAdd (L.data(), R.data(), blockSize);
        }
    }

    // Time a stage of the engine's stageSamplesFromRate calibration so the
    // tests stay aligned with the implementation curve (rate/maxRate * 2s).
    // ATK uses maxRate = 31; RR uses maxRate = 15.
    int expectedStageSamples (int rate, int maxRate, double sampleRate = kSampleRate)
    {
        if (rate <= 0 || maxRate <= 0) return 0;
        return static_cast<int> (std::round (
            sampleRate * 2.0 * static_cast<double> (rate) / static_cast<double> (maxRate)));
    }
}

// --- Step response (defaults ATK=0, RR=0) -----------------------------------

TEST (PsgEnvelope, DefaultsGiveStepResponseOnNoteOnAndNoteOff)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    // Defaults: all rates 0 + sus = 0  -> amplitude jumps to peak immediately
    // on note-on and back to zero immediately on note-off. Velocity 127 at
    // default vel_sensitivity 1.0 -> peak = 1.0.
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Idle);
    EXPECT_NEAR (eng.channelAmplitude (0), 0.0f, 1.0e-6f);

    eng.noteOnTone (60, 127);

    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-6f);
    // ATK=0, DR1=0, SUS=0 -> cascade lands in Sustain hold (DR2=0 collapses
    // Decay2 into Sustain).
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Sustain);

    renderBlocks (eng, 4);

    // Sustained at peak — Sustain hold does not decay on its own.
    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-6f);

    eng.noteOffTone (60);

    // RR=0 -> instant silence; envelope returns to Idle on the same call.
    EXPECT_NEAR (eng.channelAmplitude (0), 0.0f, 1.0e-6f);
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Idle);
}

// --- Long ATK ramp ----------------------------------------------------------

TEST (PsgEnvelope, LongAttackRampsAmplitudeOverExpectedSampleCount)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    // ATK=8, DR1=0, SUS=0, DR2=0, RR=0 -> attack ramp over
    // 2s * (8/31) ~= 22760 samples, then sustain at peak.
    constexpr int atk = 8;
    eng.setEnvelopeRates (0, atk, 0, 0, 0, 0);

    eng.noteOnTone (60, 127);

    // After zero render the amplitude should still be 0 (we just kicked
    // Attack; render hasn't consumed any samples yet).
    EXPECT_NEAR (eng.channelAmplitude (0), 0.0f, 1.0e-6f);
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Attack);

    const int expectedSamples = expectedStageSamples (atk, 31);
    const int blocksToMidway   = (expectedSamples / 2) / kBlockSize;
    renderBlocks (eng, blocksToMidway);

    // After about half the attack time, amplitude should be roughly half-way
    // to the peak. Linear ramp -> ~0.5 with some tolerance for block-rounding.
    EXPECT_GT (eng.channelAmplitude (0), 0.30f);
    EXPECT_LT (eng.channelAmplitude (0), 0.70f);
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Attack);

    // Render the remainder plus an extra block so we definitely overshoot.
    const int blocksRemaining = (expectedSamples / kBlockSize) - blocksToMidway + 2;
    renderBlocks (eng, blocksRemaining);

    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-3f);
    // Cascaded into Sustain (DR1 = SUS = DR2 = 0).
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Sustain);
}

// --- Long RR decay ----------------------------------------------------------

TEST (PsgEnvelope, LongReleaseDecaysAmplitudeAfterNoteOff)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    constexpr int rr = 7;   // ~2s * (7/15) ~= 41160 samples
    eng.setEnvelopeRates (0, 0, 0, 0, 0, rr);

    eng.noteOnTone (60, 127);
    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-6f);

    eng.noteOffTone (60);

    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Release);

    const int expectedSamples = expectedStageSamples (rr, 15);
    const int blocksToMidway  = (expectedSamples / 2) / kBlockSize;
    renderBlocks (eng, blocksToMidway);

    // Halfway through release the amplitude should be roughly halfway to zero.
    EXPECT_GT (eng.channelAmplitude (0), 0.30f);
    EXPECT_LT (eng.channelAmplitude (0), 0.70f);
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Release);

    // Render the remainder plus a couple of extra blocks to reach silence.
    const int blocksRemaining = (expectedSamples / kBlockSize) - blocksToMidway + 4;
    renderBlocks (eng, blocksRemaining);

    EXPECT_NEAR (eng.channelAmplitude (0), 0.0f, 1.0e-3f);
    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Idle);
}

// --- Re-trigger during release ----------------------------------------------

TEST (PsgEnvelope, NoteOnDuringReleaseRetriggersFromCurrentAmplitude)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    // Long release so we can sit in the middle of it; short-but-non-zero
    // attack so the retrigger is observable as a ramp rather than an instant
    // jump.
    eng.setEnvelopeRates (0, 8, 0, 0, 0, 15);

    eng.noteOnTone (60, 127);

    // Skip the attack ramp by running enough blocks to land in sustain.
    const int attackSamples = expectedStageSamples (8, 31);
    renderBlocks (eng, attackSamples / kBlockSize + 2);
    ASSERT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-3f);

    // Release, then render partway through the release.
    eng.noteOffTone (60);
    const int releaseSamples = expectedStageSamples (15, 15);
    renderBlocks (eng, (releaseSamples / 4) / kBlockSize);

    const float midReleaseAmp = eng.channelAmplitude (0);
    ASSERT_GT (midReleaseAmp, 0.30f);
    ASSERT_LT (midReleaseAmp, 0.95f);
    ASSERT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Release);

    // Retrigger from the middle of the release — note-on should pick the same
    // (now idle) channel and enter Attack starting from the current amp.
    eng.noteOnTone (60, 127);

    EXPECT_EQ (eng.channelStage (0), SN76489Engine::PsgEnvelope::Stage::Attack);
    // No click: the attack must start from the amplitude we were already at,
    // not snap back to zero.
    EXPECT_NEAR (eng.channelAmplitude (0), midReleaseAmp, 1.0e-3f);

    // After a few attack blocks, the amplitude should rise toward peak —
    // confirms it's actually attacking, not stuck.
    renderBlocks (eng, attackSamples / kBlockSize + 2);
    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-3f);
}

// --- Velocity sensitivity scalar --------------------------------------------

TEST (PsgEnvelope, VelocitySensitivityScalesPeakAmplitude)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    // psg_vel = 1 (full velocity sensitivity), low velocity -> low peak.
    eng.setEnvelopeVel (0, 1.0f);
    eng.noteOnTone (60, 32);
    const float peakWithVelSensOn = eng.channelAmplitude (0);
    EXPECT_LT (peakWithVelSensOn, 0.50f);
    EXPECT_GT (peakWithVelSensOn, 0.20f);

    eng.noteOffTone (60);

    // psg_vel = 0 (no sensitivity), same low velocity -> peak still = 1.0.
    eng.setEnvelopeVel (0, 0.0f);
    eng.noteOnTone (60, 32);
    EXPECT_NEAR (eng.channelAmplitude (0), 1.0f, 1.0e-6f);
}

// --- Noise channel envelope -------------------------------------------------

TEST (PsgEnvelope, NoiseChannelUsesEnvelopeToo)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    eng.setEnvelopeRates (SN76489Engine::kNoiseCh, 0, 0, 0, 0, 0);
    eng.noteOnNoise (40, 100);

    EXPECT_GT (eng.channelAmplitude (SN76489Engine::kNoiseCh), 0.0f);
    EXPECT_EQ (eng.channelStage (SN76489Engine::kNoiseCh),
               SN76489Engine::PsgEnvelope::Stage::Sustain);

    eng.noteOffNoise (40);
    EXPECT_NEAR (eng.channelAmplitude (SN76489Engine::kNoiseCh), 0.0f, 1.0e-6f);
    EXPECT_EQ (eng.channelStage (SN76489Engine::kNoiseCh),
               SN76489Engine::PsgEnvelope::Stage::Idle);
}

// --- Stereo pan: hard-left isolates output to the L buffer ------------------
// Regression test for the post-mvp2 "L/R audio static" report: confirms the
// engine's per-channel soft-pan path is wired correctly end-to-end. The
// processor's renderSqBlock pushes apvts → engine each block; this test
// stands in for the engine half of that contract.
TEST (PsgEnvelope, HardLeftPanSilencesRightBufferForToneChannel)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    // Hard-left pan on channel 0; volume max, envelope = step.
    eng.setChannelPan    (0, -1.0f);
    eng.setChannelVolume (0,  1.0f);
    eng.setEnvelopeRates (0, 0, 0, 0, 0, 0);
    eng.setEnvelopeVel   (0, 1.0f);

    eng.noteOnTone (60, 127);

    std::vector<float> L ((std::size_t) kBlockSize, 0.0f);
    std::vector<float> R ((std::size_t) kBlockSize, 0.0f);
    // A few blocks of warmup so the chip's LFSR / divider settle into a
    // non-trivial output. SN76489's 4-bit PCM has stair-step output so a
    // single sample may sit at zero by chance.
    for (int b = 0; b < 16; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        eng.renderAdd (L.data(), R.data(), kBlockSize);
    }

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < kBlockSize; ++i)
    {
        peakL = std::max (peakL, std::abs (L[(std::size_t) i]));
        peakR = std::max (peakR, std::abs (R[(std::size_t) i]));
    }

    EXPECT_GT (peakL, 0.01f) << "hard-left pan should leave the L buffer audible";
    EXPECT_NEAR (peakR, 0.0f, 1.0e-6f) << "hard-left pan must silence the R buffer";
}

TEST (PsgEnvelope, HardRightPanSilencesLeftBufferForToneChannel)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    eng.setChannelPan    (0, +1.0f);
    eng.setChannelVolume (0,  1.0f);
    eng.setEnvelopeRates (0, 0, 0, 0, 0, 0);
    eng.setEnvelopeVel   (0, 1.0f);

    eng.noteOnTone (60, 127);

    std::vector<float> L ((std::size_t) kBlockSize, 0.0f);
    std::vector<float> R ((std::size_t) kBlockSize, 0.0f);
    for (int b = 0; b < 16; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        eng.renderAdd (L.data(), R.data(), kBlockSize);
    }

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < kBlockSize; ++i)
    {
        peakL = std::max (peakL, std::abs (L[(std::size_t) i]));
        peakR = std::max (peakR, std::abs (R[(std::size_t) i]));
    }

    EXPECT_NEAR (peakL, 0.0f, 1.0e-6f) << "hard-right pan must silence the L buffer";
    EXPECT_GT (peakR, 0.01f) << "hard-right pan should leave the R buffer audible";
}

// --- Channel volume scales the chip output proportionally ------------------
// Regression test for the post-mvp2 "presets change sound but UI doesn't
// follow" report: confirms channel volume reaches the mix. Combined with the
// processor's new renderSqBlock snapshot push, SQ vol sliders + .psg vol
// fields now take audible effect.
TEST (PsgEnvelope, ChannelVolumeScalesOutputAmplitude)
{
    SN76489Engine eng;
    eng.prepare (kSampleRate, kBlockSize);

    eng.setChannelPan    (0, 0.0f);
    eng.setEnvelopeRates (0, 0, 0, 0, 0, 0);
    eng.setEnvelopeVel   (0, 1.0f);

    eng.setChannelVolume (0, 1.0f);
    eng.noteOnTone (60, 127);

    std::vector<float> L ((std::size_t) kBlockSize, 0.0f);
    std::vector<float> R ((std::size_t) kBlockSize, 0.0f);
    float peakHigh = 0.0f;
    for (int b = 0; b < 16; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        eng.renderAdd (L.data(), R.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
            peakHigh = std::max (peakHigh, std::abs (L[(std::size_t) i]));
    }
    eng.noteOffTone (60);

    eng.setChannelVolume (0, 0.25f);
    eng.noteOnTone (60, 127);
    float peakLow = 0.0f;
    for (int b = 0; b < 16; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        eng.renderAdd (L.data(), R.data(), kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
            peakLow = std::max (peakLow, std::abs (L[(std::size_t) i]));
    }

    EXPECT_GT (peakHigh, 0.01f);
    EXPECT_GT (peakLow,  0.0f);
    EXPECT_LT (peakLow,  peakHigh * 0.5f) << "channel volume 0.25 should be substantially quieter than volume 1.0";
}
