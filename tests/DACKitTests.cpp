#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

#include "DACKit.h"
#include "DACPlayer.h"

namespace
{
    // Spin up a DACPlayer bound to `kit` and pump one renderAdd block so any
    // pending message-thread swaps drain. Without this, a load-then-mutate
    // sequence on the same cell would hit the 1 s busy-wait in stageSwap
    // because no audio thread is consuming pendingSwap (tests run on the
    // message thread only). Production code drains via the real audio
    // thread; the helper makes message-only tests fast.
    void drainKitSwaps (DACKit& kit)
    {
        DACPlayer dac;
        dac.setKit (&kit);
        dac.prepare (44100.0, 256);
        constexpr int kBlock = 64;
        std::array<float, kBlock> L {}, R {};
        dac.renderAdd (L.data(), R.data(), kBlock);
    }
}

// --- Cell indexing -----------------------------------------------------------

TEST (DACKit, CellIndexForNoteMapsGridChromatically)
{
    // C-3 (note 48) maps to cell 0; G-4 (note 67) to cell 19. The 20 cells
    // are chromatic in between (no skip — every MIDI note in the range
    // owns its own cell, per Task 31 spec).
    EXPECT_EQ (DACKit::cellIndexForNote (48), 0);
    EXPECT_EQ (DACKit::cellIndexForNote (60), 12);   // C-4 (centre of grid)
    EXPECT_EQ (DACKit::cellIndexForNote (67), 19);
}

TEST (DACKit, CellIndexForNoteRejectsOutOfRange)
{
    EXPECT_EQ (DACKit::cellIndexForNote (47), -1);   // B-2, below C-3
    EXPECT_EQ (DACKit::cellIndexForNote (68), -1);   // G#-4, above G-4
    EXPECT_EQ (DACKit::cellIndexForNote (-1), -1);
    EXPECT_EQ (DACKit::cellIndexForNote (200), -1);
}

TEST (DACKit, NoteForCellIndexRoundTrips)
{
    for (int i = 0; i < DACKit::kNumCells; ++i)
        EXPECT_EQ (DACKit::cellIndexForNote (DACKit::noteForCellIndex (i)), i);
}

// --- Load / Clear -----------------------------------------------------------

TEST (DACKit, LoadCellRawPcmThenLookupReturnsCell)
{
    DACKit kit;

    std::vector<std::uint8_t> bytes (1024, 0xC0);
    kit.loadCellRawPcm (12 /* C-4 */, bytes.data(), bytes.size(), 22050, "snare.wav");

    EXPECT_TRUE (kit.hasCell (12));
    auto* c = kit.cellForNote (60);
    ASSERT_NE (c, nullptr);
    EXPECT_EQ (c->mtRate, 22050);
    EXPECT_EQ (c->name,   juce::String ("snare.wav"));
    EXPECT_EQ (c->mtPcm.size(), bytes.size());

    // Another note in-range with no loaded cell returns null.
    EXPECT_EQ (kit.cellForNote (61), nullptr);
}

TEST (DACKit, ClearCellEmptiesItAndKillsLookup)
{
    DACKit kit;

    std::vector<std::uint8_t> bytes (512, 0x40);
    kit.loadCellRawPcm (5, bytes.data(), bytes.size(), 11025, "kick.wav");
    ASSERT_TRUE (kit.hasCell (5));

    drainKitSwaps (kit);
    kit.clearCell (5);

    // Message-thread truth flips immediately.
    EXPECT_FALSE (kit.hasCell (5));

    // Audio-thread truth flips after the next swap drains the now-empty
    // stagingPcm into pcm. drainKitSwaps pumps one renderAdd block; after
    // that, cellForNote correctly reports nullptr.
    drainKitSwaps (kit);
    EXPECT_EQ (kit.cellForNote (DACKit::noteForCellIndex (5)), nullptr);
}

TEST (DACKit, OutOfRangeNoteAlwaysReturnsNullEvenWhenCellsLoaded)
{
    DACKit kit;
    std::vector<std::uint8_t> bytes (256, 0x80);
    kit.loadCellRawPcm (0, bytes.data(), bytes.size(), 22050, "a.wav");
    kit.loadCellRawPcm (19, bytes.data(), bytes.size(), 22050, "b.wav");

    EXPECT_EQ (kit.cellForNote (47), nullptr);   // below C-3
    EXPECT_EQ (kit.cellForNote (68), nullptr);   // above G-4
}

TEST (DACKit, ClearAllWipesEveryCell)
{
    DACKit kit;
    std::vector<std::uint8_t> bytes (256, 0x55);
    for (int i = 0; i < DACKit::kNumCells; ++i)
        kit.loadCellRawPcm (i, bytes.data(), bytes.size(), 22050, "c.wav");

    drainKitSwaps (kit);
    kit.clearAll();

    for (int i = 0; i < DACKit::kNumCells; ++i)
        EXPECT_FALSE (kit.hasCell (i));
}

// --- Rate normalisation -----------------------------------------------------

TEST (DACKit, NormaliseDacRateAcceptsValidAndFallsBack)
{
    EXPECT_EQ (DACKit::normaliseDacRate (8000),  8000);
    EXPECT_EQ (DACKit::normaliseDacRate (11025), 11025);
    EXPECT_EQ (DACKit::normaliseDacRate (22050), 22050);

    EXPECT_EQ (DACKit::normaliseDacRate (48000), 22050);
    EXPECT_EQ (DACKit::normaliseDacRate (0),     22050);
}

TEST (DACKit, LoadCellRawPcmNormalisesRate)
{
    DACKit kit;
    std::vector<std::uint8_t> bytes (256, 0x80);
    kit.loadCellRawPcm (3, bytes.data(), bytes.size(), 48000 /* invalid */, "x.wav");
    auto* c = kit.cellPtr (3);
    ASSERT_NE (c, nullptr);
    EXPECT_EQ (c->mtRate, 22050);   // fell back to default
}

// --- Serialisation round-trip ----------------------------------------------

TEST (DACKit, SaveAndRestoreRoundTrip)
{
    DACKit src;

    // Populate three non-adjacent cells with different bytes / rates / names
    // so the per-cell distinctions are observable after restore.
    const std::vector<std::uint8_t> kickBytes  (512, 0xC0);
    const std::vector<std::uint8_t> snareBytes (1024, 0x40);
    const std::vector<std::uint8_t> hatBytes   (128, 0x80);

    src.loadCellRawPcm (0,  kickBytes.data(),  kickBytes.size(),  22050, "kick.wav");
    src.loadCellRawPcm (12, snareBytes.data(), snareBytes.size(), 11025, "snare.wav");
    src.loadCellRawPcm (19, hatBytes.data(),   hatBytes.size(),   8000,  "hat.wav");

    juce::XmlElement dacEl ("dac");
    src.saveToXml (dacEl);

    DACKit dst;
    dst.restoreFromXml (dacEl);
    drainKitSwaps (dst);   // drain so subsequent mtPcm reads are stable

    EXPECT_TRUE  (dst.hasCell (0));
    EXPECT_TRUE  (dst.hasCell (12));
    EXPECT_TRUE  (dst.hasCell (19));
    EXPECT_FALSE (dst.hasCell (1));
    EXPECT_FALSE (dst.hasCell (5));

    {
        auto* c = dst.cellPtr (0);
        ASSERT_NE (c, nullptr);
        EXPECT_EQ (c->mtRate, 22050);
        EXPECT_EQ (c->name,   juce::String ("kick.wav"));
        EXPECT_EQ (c->mtPcm.size(), kickBytes.size());
        EXPECT_EQ (c->mtPcm,  kickBytes);
    }
    {
        auto* c = dst.cellPtr (12);
        ASSERT_NE (c, nullptr);
        EXPECT_EQ (c->mtRate, 11025);
        EXPECT_EQ (c->name,   juce::String ("snare.wav"));
        EXPECT_EQ (c->mtPcm,  snareBytes);
    }
    {
        auto* c = dst.cellPtr (19);
        ASSERT_NE (c, nullptr);
        EXPECT_EQ (c->mtRate, 8000);
        EXPECT_EQ (c->name,   juce::String ("hat.wav"));
        EXPECT_EQ (c->mtPcm,  hatBytes);
    }
}

TEST (DACKit, RestoreFromXmlClearsExistingCells)
{
    DACKit kit;
    std::vector<std::uint8_t> bytes (256, 0xAA);
    kit.loadCellRawPcm (5, bytes.data(), bytes.size(), 22050, "stale.wav");
    ASSERT_TRUE (kit.hasCell (5));
    drainKitSwaps (kit);

    // Empty <dac/> -> clearAll then no children to restore.
    juce::XmlElement emptyDac ("dac");
    kit.restoreFromXml (emptyDac);

    EXPECT_FALSE (kit.hasCell (5));
}

// --- DACPlayer dispatch via kit --------------------------------------------

namespace
{
    // Pump renderAdd repeatedly until the audio thread picks up the pending
    // swap (one block is enough in normal operation; the helper just makes
    // tests deterministic by always running at least one block).
    void renderOneBlock (DACPlayer& dac, int numSamples = 128)
    {
        std::vector<float> L (numSamples, 0.0f);
        std::vector<float> R (numSamples, 0.0f);
        dac.renderAdd (L.data(), R.data(), numSamples);
    }
}

TEST (DACPlayer, TriggerOnUnboundKitIsSilentlyIgnored)
{
    DACPlayer dac;
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    // No setKit -> trigger is a no-op (no crash, no playback).
    dac.trigger (60, 100);
    renderOneBlock (dac);

    EXPECT_FALSE (dac.isPlaying());
    EXPECT_EQ    (dac.activeCellIndex(), -1);
}

TEST (DACPlayer, TriggerWithKitDispatchesToCorrectCell)
{
    DACKit    kit;
    DACPlayer dac;
    dac.setKit (&kit);
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    // Load distinct bytes into two cells so we can verify dispatch picks the
    // right one. The bytes themselves aren't observable through the public
    // API after the trigger, but DACPlayer::activeCellIndex() exposes the
    // resolved cell for the test.
    std::vector<std::uint8_t> kickBytes  (512, 0xC0);
    std::vector<std::uint8_t> snareBytes (512, 0x40);
    kit.loadCellRawPcm (0,  kickBytes.data(),  kickBytes.size(),  22050, "kick.wav");
    kit.loadCellRawPcm (12, snareBytes.data(), snareBytes.size(), 22050, "snare.wav");

    // C-3 (48) -> cell 0; one renderAdd block to consume the arming flag.
    dac.trigger (48, 100);
    renderOneBlock (dac);
    EXPECT_EQ (dac.activeCellIndex(), 0);
    EXPECT_TRUE (dac.isPlaying());

    // C-4 (60) -> cell 12; the active cell flips on the next render block.
    dac.trigger (60, 100);
    renderOneBlock (dac);
    EXPECT_EQ (dac.activeCellIndex(), 12);
}

TEST (DACPlayer, TriggerOutOfGridRangeIsSilent)
{
    DACKit    kit;
    DACPlayer dac;
    dac.setKit (&kit);
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    std::vector<std::uint8_t> bytes (256, 0xC0);
    kit.loadCellRawPcm (0, bytes.data(), bytes.size(), 22050, "kick.wav");

    // C-2 (36) is below the grid range — trigger must not arm playback.
    dac.trigger (36, 100);
    renderOneBlock (dac);
    EXPECT_FALSE (dac.isPlaying());
    EXPECT_EQ    (dac.activeCellIndex(), -1);

    // G#-4 (68) is above the grid range — same outcome.
    dac.trigger (68, 100);
    renderOneBlock (dac);
    EXPECT_FALSE (dac.isPlaying());
}

TEST (DACPlayer, TriggerOnEmptyCellIsSilent)
{
    DACKit    kit;
    DACPlayer dac;
    dac.setKit (&kit);
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    // Cell 5 is in-range but empty (no PCM loaded). Trigger silently ignores.
    dac.trigger (DACKit::noteForCellIndex (5), 100);
    renderOneBlock (dac);

    EXPECT_FALSE (dac.isPlaying());
    EXPECT_EQ    (dac.activeCellIndex(), -1);
}

TEST (DACPlayer, RenderAddProducesFiniteSamplesWhilePlaying)
{
    DACKit    kit;
    DACPlayer dac;
    dac.setKit (&kit);
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);
    dac.setLevel  (1.0f);

    // 0x40..0xC0 step — non-silence, so any finite output beyond ymfm warmup
    // signals the chip actually streamed something.
    std::vector<std::uint8_t> bytes (4096);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = (std::uint8_t) (0x40 + (i % 0x80));
    kit.loadCellRawPcm (12, bytes.data(), bytes.size(), 22050, "noise.wav");

    dac.trigger (60, 100);

    bool anyNonZero = false;
    for (int blk = 0; blk < 8 && ! anyNonZero; ++blk)
    {
        std::array<float, 256> L {}, R {};
        dac.renderAdd (L.data(), R.data(), 256);
        for (float v : L)
        {
            ASSERT_TRUE (std::isfinite (v));
            if (std::abs (v) > 1.0e-6f) anyNonZero = true;
        }
        for (float v : R) ASSERT_TRUE (std::isfinite (v));
    }
    EXPECT_TRUE (anyNonZero);
}

// --- Concurrent stress -----------------------------------------------------
// Regression test mirroring PsgDacTests' ClearAndLoadAreSafeUnderConcurrentRender,
// but with kit-mediated dispatch: the message thread hammers loadCellRawPcm /
// clearCell while the audio thread renders.
TEST (DACPlayer, KitMutationsAreSafeUnderConcurrentRender)
{
    DACKit    kit;
    DACPlayer dac;
    dac.setKit (&kit);
    dac.prepare (44100.0, 256);
    dac.setEnabled (true);

    std::vector<std::uint8_t> initial (4096, 0xC0);
    kit.loadCellRawPcm (12, initial.data(), initial.size(), 22050, "init.wav");
    dac.trigger (60, 100);

    std::atomic<bool> stop { false };
    std::atomic<std::uint64_t> samplesRendered { 0 };

    std::thread audio ([&]
    {
        constexpr int kBlock = 256;
        std::array<float, kBlock> L {}, R {};
        while (! stop.load (std::memory_order_acquire))
        {
            L.fill (0.0f); R.fill (0.0f);
            dac.renderAdd (L.data(), R.data(), kBlock);
            for (float v : L) ASSERT_TRUE (std::isfinite (v));
            for (float v : R) ASSERT_TRUE (std::isfinite (v));
            samplesRendered.fetch_add ((std::uint64_t) kBlock,
                                       std::memory_order_relaxed);
        }
    });

    std::vector<std::uint8_t> alt (8192, 0x40);
    const auto t0 = std::chrono::steady_clock::now();
    int iter = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (300))
    {
        ++iter;
        if      (iter % 4 == 0) kit.clearCell (12);
        else if (iter % 4 == 1) kit.loadCellRawPcm (12, initial.data(), initial.size(), 22050, "a.wav");
        else if (iter % 4 == 2) kit.loadCellRawPcm (12, alt.data(),     alt.size(),     11025, "b.wav");
        else                    kit.loadCellRawPcm (0,  alt.data(),     alt.size(),     8000,  "c.wav");
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }

    stop.store (true, std::memory_order_release);
    audio.join();

    EXPECT_GT (samplesRendered.load(), 0u);
}
