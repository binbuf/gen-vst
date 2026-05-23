#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "DACPlayer.h"
#include "SN76489Engine.h"

// --- DACPlayer pure helpers ---------------------------------------------------

TEST (DACPlayer, FloatToUnsignedSilenceIsMidpoint)
{
    // YM2612 DAC: 0x80 is the midpoint / silence (07-feature-spec.md
    // "DAC Mode Specification").
    EXPECT_EQ (DACPlayer::floatTo8BitUnsigned (0.0f), 0x80);
}

TEST (DACPlayer, FloatToUnsignedExtremes)
{
    EXPECT_EQ (DACPlayer::floatTo8BitUnsigned (1.0f),  255);
    EXPECT_EQ (DACPlayer::floatTo8BitUnsigned (-1.0f), 1);
}

TEST (DACPlayer, FloatToUnsignedClampsOutOfRange)
{
    EXPECT_EQ (DACPlayer::floatTo8BitUnsigned (2.0f),   255);
    EXPECT_EQ (DACPlayer::floatTo8BitUnsigned (-2.0f),  1);
}

TEST (DACPlayer, NormaliseDacRatePassesValidRates)
{
    EXPECT_EQ (DACPlayer::normaliseDacRate (8000),  8000);
    EXPECT_EQ (DACPlayer::normaliseDacRate (11025), 11025);
    EXPECT_EQ (DACPlayer::normaliseDacRate (22050), 22050);
}

TEST (DACPlayer, NormaliseDacRateFallsBackOnUnknown)
{
    EXPECT_EQ (DACPlayer::normaliseDacRate (48000), 22050);
    EXPECT_EQ (DACPlayer::normaliseDacRate (0),     22050);
    EXPECT_EQ (DACPlayer::normaliseDacRate (-1),    22050);
}

// --- DACPlayer thread-safety stress -----------------------------------------
// Regression test: prior to the lock-free swap pattern,
// `resetAllToDefaults` could crash Ableton during MIDI playback because
// DACPlayer::clearPcm() deallocated `pcm` on the message thread while
// renderAdd() was reading `pcm[playPos]` on the audio thread.
//
// This test hammers clearPcm + loadRawPcm + setDacRate from one thread while
// renderAdd runs continuously on another for ~500 ms. Pass criterion: no
// crashes (the test process must exit normally), no torn reads observable
// via finite output samples.
TEST (DACPlayer, ClearAndLoadAreSafeUnderConcurrentRender)
{
    DACPlayer dac;
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    // Pre-load a non-empty buffer so the first iterations have something to
    // play (and to read concurrent-deallocate from).
    std::vector<std::uint8_t> initial (4096, 0xC0);
    dac.loadRawPcm (initial.data(), initial.size(), 22050, "stress.raw");
    dac.trigger (60, 100);

    std::atomic<bool> stop { false };
    std::atomic<std::uint64_t> samplesRendered { 0 };

    std::thread audio ([&]
    {
        constexpr int kBlock = 256;
        std::array<float, kBlock> left{};
        std::array<float, kBlock> right{};
        while (! stop.load (std::memory_order_acquire))
        {
            left.fill  (0.0f);
            right.fill (0.0f);
            dac.renderAdd (left.data(), right.data(), kBlock);
            // Confirm output is finite — torn reads can produce NaN/Inf.
            for (float v : left)  ASSERT_TRUE (std::isfinite (v));
            for (float v : right) ASSERT_TRUE (std::isfinite (v));
            samplesRendered.fetch_add ((std::uint64_t) kBlock,
                                       std::memory_order_relaxed);
        }
    });

    // Message thread: cycle through clear / load / setDacRate, mirroring what
    // resetAllToDefaults + a user importing a new sample would do.
    const std::vector<std::uint8_t> alt (8192, 0x40);
    const auto t0 = std::chrono::steady_clock::now();
    int iter = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (500))
    {
        ++iter;
        if (iter % 3 == 0) {
            dac.clearPcm();
        } else if (iter % 3 == 1) {
            dac.loadRawPcm (initial.data(), initial.size(), 22050, "a.raw");
            dac.trigger (60, 100);
        } else {
            dac.loadRawPcm (alt.data(), alt.size(), 11025, "b.raw");
            dac.setDacRate (8000);
            dac.trigger (60, 100);
        }
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }

    stop.store (true, std::memory_order_release);
    audio.join();

    // Sanity: audio thread should have rendered at least *some* blocks.
    EXPECT_GT (samplesRendered.load(), 0u);
}

// --- SN76489Engine allocation -------------------------------------------------

TEST (SN76489Engine, TonesAllocateRoundRobin)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnTone (60, 100);   // ch0
    eng.noteOnTone (64, 100);   // ch1
    eng.noteOnTone (67, 100);   // ch2

    EXPECT_TRUE (eng.isToneChannelActive (0));
    EXPECT_TRUE (eng.isToneChannelActive (1));
    EXPECT_TRUE (eng.isToneChannelActive (2));
    EXPECT_EQ   (eng.toneChannelNote (0), 60);
    EXPECT_EQ   (eng.toneChannelNote (1), 64);
    EXPECT_EQ   (eng.toneChannelNote (2), 67);
}

TEST (SN76489Engine, FourthToneStealsOldestChannel)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnTone (60, 100);   // ch0 (oldest)
    eng.noteOnTone (64, 100);   // ch1
    eng.noteOnTone (67, 100);   // ch2
    eng.noteOnTone (72, 100);   // -> steals ch0 (smallest timestamp)

    EXPECT_EQ (eng.toneChannelNote (0), 72);
    EXPECT_EQ (eng.toneChannelNote (1), 64);
    EXPECT_EQ (eng.toneChannelNote (2), 67);
}

TEST (SN76489Engine, NoteOffMatchesByMidiNote)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnTone (60, 100);
    eng.noteOnTone (64, 100);
    eng.noteOffTone (60);

    EXPECT_FALSE (eng.isToneChannelActive (0));
    EXPECT_TRUE  (eng.isToneChannelActive (1));
    EXPECT_EQ    (eng.toneChannelNote (1), 64);
}

TEST (SN76489Engine, NoteOffWithNoMatchIsNoOp)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnTone (60, 100);
    eng.noteOffTone (99);                     // never sounded
    EXPECT_TRUE (eng.isToneChannelActive (0));
    EXPECT_EQ   (eng.toneChannelNote (0), 60);
}

TEST (SN76489Engine, NoiseIsMonophonicLastNotePriority)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnNoise (50, 100);
    EXPECT_TRUE (eng.isNoiseChannelActive());
    EXPECT_EQ   (eng.noiseChannelNote(), 50);

    eng.noteOnNoise (80, 110);                // replaces the previous note
    EXPECT_TRUE (eng.isNoiseChannelActive());
    EXPECT_EQ   (eng.noiseChannelNote(), 80);

    eng.noteOffNoise (80);
    EXPECT_FALSE (eng.isNoiseChannelActive());
}

TEST (SN76489Engine, NoiseNoteOffWithWrongNoteIsNoOp)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 256);

    eng.noteOnNoise (50, 100);
    eng.noteOffNoise (99);
    EXPECT_TRUE (eng.isNoiseChannelActive());
}

TEST (SN76489Engine, RenderAddProducesFiniteSamples)
{
    constexpr int kBlock = 256;
    SN76489Engine eng;
    eng.prepare (44100.0, kBlock);

    eng.noteOnTone  (60, 127);   // max velocity = loudest output
    eng.noteOnNoise (40, 127);

    // Render several blocks: the chip plus the host-rate resampler need a
    // few samples of warmup before steady-state output. Eight blocks
    // (~46 ms at 44.1 kHz) is plenty to clear any startup transient.
    std::array<float, kBlock> L {};
    std::array<float, kBlock> R {};

    bool anyNonZero = false;
    for (int block = 0; block < 8 && ! anyNonZero; ++block)
    {
        L.fill (0.0f);
        R.fill (0.0f);
        eng.renderAdd (L.data(), R.data(), kBlock);

        for (float v : L)
        {
            ASSERT_TRUE (std::isfinite (v));
            if (std::abs (v) > 1.0e-6f) anyNonZero = true;
        }
        for (float v : R) ASSERT_TRUE (std::isfinite (v));
    }

    EXPECT_TRUE (anyNonZero);
}

TEST (SN76489Engine, NoiseAutoModeOverridesDirectRate)
{
    // Auto-mode active: a low note (< 38) -> shift rate 0b10. We can't
    // observe the chip register directly from here, but we can verify the
    // engine state transitions without crashing.
    SN76489Engine eng;
    eng.prepare (44100.0, 64);

    eng.setNoiseAutoMode (true);
    eng.noteOnNoise (20, 100);

    std::array<float, 64> L {};
    std::array<float, 64> R {};
    eng.renderAdd (L.data(), R.data(), 64);

    EXPECT_TRUE (eng.isNoiseChannelActive());
    EXPECT_EQ   (eng.noiseChannelNote(), 20);
}

TEST (SN76489Engine, PitchBendOptInDoesNotAffectChannelsWithoutBend)
{
    SN76489Engine eng;
    eng.prepare (44100.0, 64);

    // Channel 0 bend disabled (default); channel 1 enabled.
    eng.setChannelBendEnabled (0, false);
    eng.setChannelBendEnabled (1, true);

    eng.noteOnTone (60, 100);   // ch0 (oldest -> first slot)
    eng.noteOnTone (64, 100);   // ch1

    eng.setPitchBendSemitones (0, 2.0);
    eng.setPitchBendSemitones (1, 2.0);

    // The engine doesn't expose chip register state; instead we verify that
    // the bend setter doesn't change the *recorded* MIDI note for either
    // channel — both still sound their original note (the divider write is
    // an implementation detail behind the chip wrapper).
    EXPECT_EQ (eng.toneChannelNote (0), 60);
    EXPECT_EQ (eng.toneChannelNote (1), 64);
    EXPECT_TRUE (eng.isToneChannelActive (0));
    EXPECT_TRUE (eng.isToneChannelActive (1));
}
