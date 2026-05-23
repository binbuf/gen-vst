#include <array>
#include <cmath>

#include <gtest/gtest.h>

#include "PartManager.h"
#include "PatchSystem.h"
#include "VoiceAllocator.h"

namespace
{
    // An audible patch: algorithm 7 (all four operators are carriers), instant
    // attack, no decay — a held note sustains at peak, so the render path can
    // be checked for non-silent output.
    Patch makePatch()
    {
        Patch p {};
        p.alg = 7;
        p.lr  = 3;                // both outputs enabled
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op] = 1;
            p.ar[op]  = 31;       // instant attack
            p.tl[op]  = 20;       // audible level
            p.sl[op]  = 0;        // sustain at peak
            p.rr[op]  = 15;       // fast release
        }
        return p;
    }
}

// --- Free-voice allocation ---------------------------------------------------

TEST (VoiceAllocator, AllocatesDistinctFreeVoices)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
        alloc.noteOn (0, 60 + i, 100, p);

    EXPECT_EQ (alloc.numActiveVoices(), VoiceAllocator::kNumVoices);
    EXPECT_EQ (alloc.numIdleVoices(), 0);
    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
        EXPECT_TRUE (alloc.isNoteActive (0, 60 + i));
}

// --- Voice stealing ----------------------------------------------------------

TEST (VoiceAllocator, SeventeenthNoteStealsAVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    for (int i = 0; i < 16; ++i)
        alloc.noteOn (0, 60 + i, 100, p);
    alloc.noteOn (0, 90, 100, p);   // 17th simultaneous note

    EXPECT_EQ (alloc.numActiveVoices(), 16);
    EXPECT_TRUE  (alloc.isNoteActive (0, 90));
    EXPECT_FALSE (alloc.isNoteActive (0, 60));   // the oldest note was stolen
}

TEST (VoiceAllocator, StealUsesGlobalLru)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    // 16 notes keyed on in order — note 60 is the oldest, note 75 the newest.
    for (int i = 0; i < 16; ++i)
        alloc.noteOn (0, 60 + i, 100, p);

    alloc.noteOn (0, 100, 100, p);   // steals the oldest -> note 60
    alloc.noteOn (0, 101, 100, p);   // steals the next   -> note 61

    EXPECT_FALSE (alloc.isNoteActive (0, 60));
    EXPECT_FALSE (alloc.isNoteActive (0, 61));
    EXPECT_TRUE  (alloc.isNoteActive (0, 62));
    EXPECT_TRUE  (alloc.isNoteActive (0, 100));
    EXPECT_TRUE  (alloc.isNoteActive (0, 101));
    EXPECT_EQ (alloc.numActiveVoices(), 16);
}

TEST (VoiceAllocator, StealPrefersReleasePhaseVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    for (int i = 0; i < 16; ++i)
        alloc.noteOn (0, 60 + i, 100, p);

    // Release the most-recently-played note; note 60 stays the oldest Active.
    alloc.noteOff (0, 75);
    EXPECT_EQ (alloc.numActiveVoices(), 15);
    EXPECT_EQ (alloc.numReleasingVoices(), 1);

    // The next note-on must reuse the release-phase voice rather than steal an
    // older sustaining voice — release-phase voices are preferred.
    alloc.noteOn (0, 90, 100, p);
    EXPECT_EQ (alloc.numActiveVoices(), 16);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_TRUE  (alloc.isNoteActive (0, 90));
    EXPECT_TRUE  (alloc.isNoteActive (0, 60));   // oldest Active left untouched
    EXPECT_FALSE (alloc.isNoteActive (0, 75));
}

// --- Note-off / panic --------------------------------------------------------

TEST (VoiceAllocator, NoteOffReleasesTheVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    alloc.noteOn (0, 64, 100, p);
    EXPECT_EQ (alloc.numActiveVoices(), 1);

    alloc.noteOff (0, 64);
    EXPECT_EQ (alloc.numActiveVoices(), 0);
    EXPECT_EQ (alloc.numReleasingVoices(), 1);
    EXPECT_FALSE (alloc.isNoteActive (0, 64));
}

TEST (VoiceAllocator, AllNotesOffReleasesEverySoundingVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    for (int i = 0; i < 5; ++i)
        alloc.noteOn (0, 60 + i, 100, p);

    alloc.allNotesOff();
    EXPECT_EQ (alloc.numActiveVoices(), 0);
    EXPECT_EQ (alloc.numReleasingVoices(), 5);
}

TEST (VoiceAllocator, AllSoundOffFreesEveryVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    for (int i = 0; i < 5; ++i)
        alloc.noteOn (0, 60 + i, 100, p);

    alloc.allSoundOff();
    EXPECT_EQ (alloc.numActiveVoices(), 0);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_EQ (alloc.numIdleVoices(), VoiceAllocator::kNumVoices);
}

// --- Part association --------------------------------------------------------

TEST (VoiceAllocator, NoteOnRecordsTheServingPart)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    alloc.noteOn (3, 64, 100, p);
    EXPECT_TRUE  (alloc.isNoteActive (3, 64));
    EXPECT_FALSE (alloc.isNoteActive (0, 64));
}

TEST (VoiceAllocator, ChannelRoutesToCorrectPart)
{
    PartManager pm;
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    const Patch p = makePatch();

    // A note arriving on MIDI channel 2 must be served by part 1.
    const int part = pm.partForMidiChannel (2);
    ASSERT_EQ (part, 1);
    alloc.noteOn (part, 67, 100, p);

    EXPECT_TRUE  (alloc.isNoteActive (1, 67));
    EXPECT_FALSE (alloc.isNoteActive (0, 67));
}

// --- Dirty-diff parameter updates -------------------------------------------

TEST (VoiceAllocator, UpdateActiveVoicesAppliesParamEditsWithoutRetrigger)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.noteOn (0, 69, 100, makePatch());

    // A live edit on part 0: the dirty-diff must apply it without dropping or
    // retriggering the voice.
    std::array<Patch, VoiceAllocator::kNumParts> patches;
    patches.fill (makePatch());
    patches[0].tl[0] = 80;   // changed carrier level
    alloc.updateActiveVoices (patches);

    EXPECT_EQ (alloc.numActiveVoices(), 1);
    EXPECT_TRUE (alloc.isNoteActive (0, 69));
}

// --- Render path -------------------------------------------------------------

TEST (VoiceAllocator, RenderProducesFiniteAudibleOutput)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.noteOn (0, 69, 100, makePatch());

    std::array<float, 512> outL {};
    std::array<float, 512> outR {};
    bool allFinite  = true;
    bool anyNonZero = false;

    // ~93 ms — well past the instant attack, so output is sounding.
    for (int block = 0; block < 8; ++block)
    {
        alloc.render (outL.data(), outR.data(), 512);
        for (int i = 0; i < 512; ++i)
        {
            if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i]))
                allFinite = false;
            if (outL[i] != 0.0f)
                anyNonZero = true;
        }
    }

    EXPECT_TRUE (allFinite);
    EXPECT_TRUE (anyNonZero);
}

TEST (VoiceAllocator, RenderHandlesVaryingBlockSizes)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.noteOn (0, 69, 100, makePatch());

    std::array<float, 512> outL {};
    std::array<float, 512> outR {};
    const int sizes[] { 64, 512, 1, 200, 333, 512, 7 };

    bool allFinite = true;
    for (const int n : sizes)
    {
        alloc.render (outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i]))
                allFinite = false;
    }

    EXPECT_TRUE (allFinite);
}

// --- PartManager -------------------------------------------------------------

TEST (PartManager, DefaultChannelBindings)
{
    PartManager pm;

    for (int part = 0; part < PartManager::kNumParts; ++part)
        EXPECT_EQ (pm.midiChannelForPart (part), part + 1);

    for (int ch = 1; ch <= 6; ++ch)
        EXPECT_EQ (pm.partForMidiChannel (ch), ch - 1);

    // Channels 7-16 are unbound until PSG/DAC routing arrives.
    EXPECT_EQ (pm.partForMidiChannel (7), -1);
    EXPECT_EQ (pm.partForMidiChannel (16), -1);
}

TEST (PartManager, LoadPatchStoresPerPart)
{
    PartManager pm;
    Patch a {}; a.alg = 5;
    Patch b {}; b.alg = 2;

    pm.loadPatch (0, a);
    pm.loadPatch (1, b);

    EXPECT_EQ (static_cast<int> (pm.getPatch (0).alg), 5);
    EXPECT_EQ (static_cast<int> (pm.getPatch (1).alg), 2);
}
