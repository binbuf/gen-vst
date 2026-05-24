// VgmLogger round-trip tests (Task 29 verification).
//
// Each test creates a VgmLogger, starts a capture, enqueues synthetic
// register writes through the audio-thread entry points, stops the capture,
// then re-parses the produced .vgm file via the existing extractFmPatches
// pipeline (Task 21) and asserts the writes round-trip back into a Patch
// matching what was logged.
//
// Coverage:
//   * Toggle lifecycle (start -> stop -> start) creates separate files.
//   * Single-patch round-trip via FmRegisterMap::buildNoteOn: every per-op
//     and per-channel register survives the ring -> file -> parser path.
//   * Multi-part remap: two voices on different parts end up on distinct
//     VGM channels in the captured file.
//
// The tests do NOT spin up a juce::MessageManager loop. flushPendingWritesForTest()
// drains the ring inline so the timer is never relied upon; stop() (the
// second toggle()) drains again and patches the header before closing.

#include <array>
#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include <juce_core/juce_core.h>

#include "FmRegisterMap.h"
#include "PatchSystem.h"
#include "VgmExtract.h"
#include "VgmLogger.h"

namespace fs = std::filesystem;

namespace
{
    // A Patch with distinct, non-default values in every per-op + per-part
    // slot so the round-trip check fails loudly on any field that doesn't
    // survive the ring -> file -> parser path.
    Patch distinctPatch()
    {
        Patch p {};
        p.alg = 4;
        p.fb  = 3;
        p.ams = 2;
        p.pms = 5;
        p.lr  = 3;
        p.lfo_enable = 1;
        p.lfo_rate   = 4;
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op]  = static_cast<std::uint8_t> ((op + 1) & 0x0F);
            p.dt[op]   = static_cast<std::uint8_t> ((op + 1) & 0x07);
            p.tl[op]   = static_cast<std::uint8_t> ((10 + op * 5) & 0x7F);
            p.ks[op]   = static_cast<std::uint8_t> (op & 0x03);
            p.ar[op]   = static_cast<std::uint8_t> ((20 + op) & 0x1F);
            p.dr[op]   = static_cast<std::uint8_t> ((18 + op) & 0x1F);
            p.sr[op]   = static_cast<std::uint8_t> ((15 + op) & 0x1F);
            p.rr[op]   = static_cast<std::uint8_t> ((7 + op) & 0x0F);
            p.sl[op]   = static_cast<std::uint8_t> ((8 + op) & 0x0F);
            p.ssg[op]  = 0;
            p.amon[op] = static_cast<std::uint8_t> (op & 1);
        }
        return p;
    }

    // Replay buildNoteOn's register sequence through the logger's audio-thread
    // entry point. Skips the 0x22 LFO write because the parser tracks LFO via
    // a separate global write — but buildNoteOn already emits it, so we send
    // the full list and let the parser pick what it cares about.
    void emitNoteOn (VgmLogger& logger, int partIndex, const Patch& p, int midiNote,
                     int velocity = 100, bool velToTl = true)
    {
        const FmRegisterMap::NoteParams np { velocity, velToTl, 0.0 };
        for (const auto& w : FmRegisterMap::buildNoteOn (p, midiNote, np))
            logger.recordYm2612VoiceWrite (partIndex, w.reg, w.value);
    }

    void deleteTempFile (const juce::String& path)
    {
        if (path.isNotEmpty())
            juce::File (path).deleteFile();
    }
}

// =============================================================================
// Toggle lifecycle: start writes a header file, stop closes + back-patches it,
// a second start opens a fresh file.
// =============================================================================

TEST (VgmLogger, ToggleStartStopProducesAFile)
{
    VgmLogger logger;
    logger.prepare (44100.0);

    juce::String startPath;
    const bool started = logger.toggle (startPath);
    ASSERT_TRUE (started);
    EXPECT_TRUE (startPath.endsWith (".vgm"));

    // Push at least one wait so total_samples > 0 in the patched header.
    logger.recordWaitSamples (44100);
    logger.flushPendingWritesForTest();

    juce::String stopPath;
    const bool stopped = ! logger.toggle (stopPath);
    EXPECT_TRUE (stopped);
    EXPECT_EQ (stopPath, startPath);

    juce::File f (stopPath);
    EXPECT_TRUE (f.existsAsFile());
    // Minimum: 64-byte header + 3-byte wait (0x61 nn nn) + 1-byte EoF.
    EXPECT_GE (f.getSize(), static_cast<juce::int64> (0x40 + 3 + 1));

    deleteTempFile (stopPath);
}

// =============================================================================
// Round-trip: writes for one voice -> file -> extractFmPatches -> matching Patch.
// =============================================================================

TEST (VgmLogger, SinglePartRoundTripsThroughExtract)
{
    VgmLogger logger;
    logger.prepare (44100.0);

    juce::String path;
    ASSERT_TRUE (logger.toggle (path));

    // Wait then the full note-on register sequence for part 0 (VGM channel 1).
    logger.recordWaitSamples (44100);
    const Patch expected = distinctPatch();
    emitNoteOn (logger, /* partIndex */ 0, expected, /* note */ 60);
    logger.flushPendingWritesForTest();

    juce::String stopPath;
    logger.toggle (stopPath);
    ASSERT_EQ (path, stopPath);

    std::string error;
    const auto patches =
        extractFmPatches (fs::path (path.toRawUTF8()), error);
    deleteTempFile (path);

    ASSERT_GE (patches.size(), 1u) << "extract error: " << error;
    const Patch& got = patches[0];

    EXPECT_EQ (static_cast<int> (got.alg), static_cast<int> (expected.alg));
    EXPECT_EQ (static_cast<int> (got.fb),  static_cast<int> (expected.fb));
    EXPECT_EQ (static_cast<int> (got.ams), static_cast<int> (expected.ams));
    EXPECT_EQ (static_cast<int> (got.pms), static_cast<int> (expected.pms));
    EXPECT_EQ (static_cast<int> (got.lr),  static_cast<int> (expected.lr));
    EXPECT_EQ (static_cast<int> (got.lfo_enable),
               static_cast<int> (expected.lfo_enable));
    EXPECT_EQ (static_cast<int> (got.lfo_rate),
               static_cast<int> (expected.lfo_rate));

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE ("OP" + std::to_string (op + 1));
        EXPECT_EQ (static_cast<int> (got.mul[op]),  static_cast<int> (expected.mul[op]));
        EXPECT_EQ (static_cast<int> (got.dt[op]),   static_cast<int> (expected.dt[op]));
        // velToTl=true scales the carrier TL — modulators round-trip exactly.
        // Carriers shift by (127 - 100) / 2 = 13 (FmRegisterMap::scaleCarrierTl);
        // the per-alg carrier mask says alg=4 -> OP2 + OP4 are carriers.
        const bool isCarrier = (op == 1) || (op == 3);   // alg 4 mask = 0b1010
        const int  expectedTl = isCarrier
                                    ? juce::jlimit (0, 127, expected.tl[op] + 13)
                                    : expected.tl[op];
        EXPECT_EQ (static_cast<int> (got.tl[op]), expectedTl);
        EXPECT_EQ (static_cast<int> (got.ks[op]),   static_cast<int> (expected.ks[op]));
        EXPECT_EQ (static_cast<int> (got.ar[op]),   static_cast<int> (expected.ar[op]));
        EXPECT_EQ (static_cast<int> (got.dr[op]),   static_cast<int> (expected.dr[op]));
        EXPECT_EQ (static_cast<int> (got.sr[op]),   static_cast<int> (expected.sr[op]));
        EXPECT_EQ (static_cast<int> (got.rr[op]),   static_cast<int> (expected.rr[op]));
        EXPECT_EQ (static_cast<int> (got.sl[op]),   static_cast<int> (expected.sl[op]));
        EXPECT_EQ (static_cast<int> (got.amon[op]), static_cast<int> (expected.amon[op]));
    }
}

// =============================================================================
// Multi-part remap: two voices on different parts must end up as patches on
// distinct VGM channels in the captured file. Identical patches dedupe down to
// one entry by the extract parser's content hash, so we use two distinct
// patches here and check both come back.
// =============================================================================

TEST (VgmLogger, TwoPartsEmitTwoPatches)
{
    VgmLogger logger;
    logger.prepare (44100.0);

    juce::String path;
    ASSERT_TRUE (logger.toggle (path));

    Patch a = distinctPatch();
    Patch b = distinctPatch();
    b.fb = 7;        // distinguish
    b.alg = 6;

    logger.recordWaitSamples (44100);
    emitNoteOn (logger, /* part 0 — VGM ch1 */ 0, a, /* note */ 60);
    emitNoteOn (logger, /* part 3 — VGM ch4 (port 1) */ 3, b, /* note */ 64);
    logger.flushPendingWritesForTest();

    juce::String stopPath;
    logger.toggle (stopPath);

    std::string error;
    const auto patches =
        extractFmPatches (fs::path (path.toRawUTF8()), error);
    deleteTempFile (path);

    ASSERT_EQ (patches.size(), 2u) << "extract error: " << error;

    // Order is observation order — ch1 keyed first, then ch4.
    EXPECT_EQ (static_cast<int> (patches[0].alg), static_cast<int> (a.alg));
    EXPECT_EQ (static_cast<int> (patches[0].fb),  static_cast<int> (a.fb));
    EXPECT_EQ (static_cast<int> (patches[1].alg), static_cast<int> (b.alg));
    EXPECT_EQ (static_cast<int> (patches[1].fb),  static_cast<int> (b.fb));
}

// =============================================================================
// PSG writes survive: a single SN76489 protocol byte enqueued via recordPsgWrite
// shows up as a 0x50 dd command in the file. Verifies the audio-thread entry
// point + the flush emits the right opcode (the parser handles 0x50 by skipping
// past it, so we read the raw bytes here).
// =============================================================================

TEST (VgmLogger, PsgWriteEmitsCorrectOpcode)
{
    VgmLogger logger;
    logger.prepare (44100.0);

    juce::String path;
    ASSERT_TRUE (logger.toggle (path));

    logger.recordWaitSamples (1024);
    logger.recordPsgWrite (0x9F);                 // ch0 attenuation = 15 (silent)
    logger.recordPsgWrite (static_cast<std::uint8_t> (0x80 | (0 << 5) | (0 << 4) | 0x05));  // ch0 freq low
    logger.flushPendingWritesForTest();

    juce::String stopPath;
    logger.toggle (stopPath);

    juce::File f (path);
    ASSERT_TRUE (f.existsAsFile());
    juce::MemoryBlock raw;
    f.loadFileAsData (raw);
    deleteTempFile (path);

    // Find the first 0x50 in the data stream (skip the 64-byte header). The
    // wait command 0x61 nn nn comes first; the two 0x50 dd PSG writes follow.
    const auto* bytes = static_cast<const std::uint8_t*> (raw.getData());
    bool foundFirstPsg = false, foundSecondPsg = false;
    for (std::size_t i = 0x40; i + 1 < raw.getSize(); ++i)
    {
        if (bytes[i] == 0x50 && bytes[i + 1] == 0x9F)
        {
            foundFirstPsg = true;
            // The next PSG write should immediately follow.
            if (i + 3 < raw.getSize()
                && bytes[i + 2] == 0x50
                && bytes[i + 3] == static_cast<std::uint8_t> (0x80 | 0x05))
                foundSecondPsg = true;
            break;
        }
    }
    EXPECT_TRUE (foundFirstPsg);
    EXPECT_TRUE (foundSecondPsg);
}
