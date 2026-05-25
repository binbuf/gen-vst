#include <array>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);
    alloc.noteOn (0, 90, 100, 0.0, false, p);   // 17th simultaneous note

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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

    alloc.noteOn (0, 100, 100, 0.0, false, p);   // steals the oldest -> note 60
    alloc.noteOn (0, 101, 100, 0.0, false, p);   // steals the next   -> note 61

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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

    // Release the most-recently-played note; note 60 stays the oldest Active.
    alloc.noteOff (0, 75, false);
    EXPECT_EQ (alloc.numActiveVoices(), 15);
    EXPECT_EQ (alloc.numReleasingVoices(), 1);

    // The next note-on must reuse the release-phase voice rather than steal an
    // older sustaining voice — release-phase voices are preferred.
    alloc.noteOn (0, 90, 100, 0.0, false, p);
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

    alloc.noteOn (0, 64, 100, 0.0, false, p);
    EXPECT_EQ (alloc.numActiveVoices(), 1);

    alloc.noteOff (0, 64, false);
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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

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
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

    alloc.allSoundOff();
    EXPECT_EQ (alloc.numActiveVoices(), 0);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_EQ (alloc.numIdleVoices(), VoiceAllocator::kNumVoices);
}

// --- Dirty-diff parameter updates -------------------------------------------

TEST (VoiceAllocator, UpdateActiveVoicesAppliesParamEditsWithoutRetrigger)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.noteOn (0, 69, 100, 0.0, false, makePatch());

    // A live edit: the dirty-diff must apply it without dropping or
    // retriggering the voice.
    Patch p = makePatch();
    p.tl[0] = 80;   // changed carrier level
    alloc.updateActiveVoicesForPart (0, p, false);

    EXPECT_EQ (alloc.numActiveVoices(), 1);
    EXPECT_TRUE (alloc.isNoteActive (0, 69));
}

// --- Render path -------------------------------------------------------------

TEST (VoiceAllocator, RenderProducesFiniteAudibleOutput)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.noteOn (0, 69, 100, 0.0, false, makePatch());

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
    alloc.noteOn (0, 69, 100, 0.0, false, makePatch());

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

// --- Task 15 — Polyphony modes & voice count --------------------------------

namespace
{
    VoiceAllocator::PartPolyMode polyMode()
    {
        return { VoiceAllocator::PartPolyMode::Mode::Poly, false, 12.0 };
    }
    VoiceAllocator::PartPolyMode monoMode (bool legato)
    {
        return { VoiceAllocator::PartPolyMode::Mode::Mono, legato, 12.0 };
    }
    VoiceAllocator::PartPolyMode unisonMode (double spreadCents)
    {
        return { VoiceAllocator::PartPolyMode::Mode::Unison, false, spreadCents };
    }
}

TEST (VoiceAllocator, MonoModeLimitsPartToOneVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoMode (false));   // retrigger
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOn (0, 64, 100, 0.0, false, p);
    alloc.noteOn (0, 67, 100, 0.0, false, p);

    // After three Mono note-ons on the same part the part owns exactly one
    // active voice — Retrigger reuses the same slot, so no extra Released
    // voices stack up either.
    EXPECT_EQ (alloc.numActiveVoices(), 1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_TRUE (alloc.isNoteActive (0, 67));    // last note wins
    EXPECT_FALSE (alloc.isNoteActive (0, 60));
    EXPECT_FALSE (alloc.isNoteActive (0, 64));
}

TEST (VoiceAllocator, MonoLegatoKeepsVoiceWithoutRetrigger)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoMode (true));    // legato
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    EXPECT_EQ (alloc.numActiveVoices(), 1);

    // Legato to a new note: the voice should stay Active (no Released tail
    // would mean key-off was issued) and its note number tracks the new note.
    alloc.noteOn (0, 64, 100, 0.0, false, p);

    EXPECT_EQ (alloc.numActiveVoices(), 1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_TRUE  (alloc.isNoteActive (0, 64));
    EXPECT_FALSE (alloc.isNoteActive (0, 60));
}

TEST (VoiceAllocator, UnisonStackAllocatesAllVoices)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, unisonMode (12.0));
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);

    // One unison note-on fills the full pool (default voice count = 16).
    EXPECT_EQ (alloc.numActiveVoices(), VoiceAllocator::kNumVoices);
}

TEST (VoiceAllocator, UnisonDetuneOffsetsAreSymmetric)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, unisonMode (10.0));   // 10 cents = 0.1 semitone
    alloc.setVoiceCount (8);                    // 8-voice stack for clean math
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);

    // Collect each sounding voice's detune offset (cents-as-semitones).
    std::vector<double> detunes;
    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
    {
        const auto& v = alloc.voiceAt (i);
        if (v.isActive() && v.part() == 0 && v.note() == 60)
            detunes.push_back (v.voiceDetuneSemitones());
    }

    ASSERT_EQ ((int) detunes.size(), 8);

    // 8-voice symmetric fan-out at 10 cents: 0, +10, -10, +20, -20, +30, -30, +40
    // (cents -> semitones is /100). The order in which voices were filled is
    // 0, 1, 2, ... so detunes[i] = unisonVoiceDetuneSemitones(i, 10).
    constexpr double kExpected[] {
         0.0,
         0.10, -0.10,
         0.20, -0.20,
         0.30, -0.30,
         0.40,
    };
    for (int i = 0; i < 8; ++i)
        EXPECT_NEAR (detunes[(std::size_t) i], kExpected[i], 1e-9);
}

TEST (VoiceAllocator, UnisonNoteOffReleasesEveryStackVoice)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, unisonMode (12.0));
    alloc.setVoiceCount (8);
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    EXPECT_EQ (alloc.numActiveVoices(), 8);

    alloc.noteOff (0, 60, false);

    // All 8 voices for (part 0, note 60) move to Released.
    EXPECT_EQ (alloc.numActiveVoices(), 0);
    EXPECT_EQ (alloc.numReleasingVoices(), 8);
}

TEST (VoiceAllocator, VoiceCountCapLimitsAllocation)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setVoiceCount (8);
    const Patch p = makePatch();

    // Fill the cap, then try a ninth note — the cap-restricted LRU steal
    // must kick in even though slots 8..15 are still Idle.
    for (int i = 0; i < 8; ++i)
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);
    EXPECT_EQ (alloc.numActiveVoices(), 8);

    alloc.noteOn (0, 90, 100, 0.0, false, p);   // steals the oldest in [0,7]

    EXPECT_EQ (alloc.numActiveVoices(), 8);     // not 9
    EXPECT_FALSE (alloc.isNoteActive (0, 60));  // stolen
    EXPECT_TRUE  (alloc.isNoteActive (0, 90));
}

TEST (VoiceAllocator, VoiceCountResetToFullPoolAllowsAllVoices)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setVoiceCount (16);
    const Patch p = makePatch();

    for (int i = 0; i < 16; ++i)
        alloc.noteOn (0, 60 + i, 100, 0.0, false, p);

    EXPECT_EQ (alloc.numActiveVoices(), 16);
}

// --- Task 28 — Mono+Legato portamento (glide time) --------------------------

namespace
{
    // Mono+Legato with an explicit glide time (ms). Wraps the existing
    // monoMode() helper which defaults glideTimeMs to 0.
    VoiceAllocator::PartPolyMode monoLegatoWithGlide (double glideMs)
    {
        VoiceAllocator::PartPolyMode m {
            VoiceAllocator::PartPolyMode::Mode::Mono, true, 12.0 };
        m.glideTimeMs = glideMs;
        return m;
    }

    // Native render-rate VoiceAllocator runs at — derived from the voice's
    // chip sample rate via `voices[0].nativeSampleRate()` at prepare. The
    // value here mirrors the one in Voice.cpp (NTSC YM2612 / 144).
    constexpr double kNativeRate = 53267.0;
}

TEST (VoiceAllocator, GlideTimeZeroIsImmediate)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoLegatoWithGlide (0.0));   // glide off
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    // Legato to a new note with glide_time = 0 -> voice should not be gliding.
    alloc.noteOn (0, 72, 100, 0.0, false, p);

    bool found = false;
    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
    {
        const auto& v = alloc.voiceAt (i);
        if (v.isActive() && v.part() == 0 && v.note() == 72)
        {
            EXPECT_FALSE (v.isGliding());
            found = true;
        }
    }
    EXPECT_TRUE (found);
}

TEST (VoiceAllocator, GlideTimeReachesTargetWithinExpectedBlocks)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoLegatoWithGlide (100.0));   // 100 ms glide
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOn (0, 72, 100, 0.0, false, p);   // legato hop, glide starts

    // 100 ms at the chip's native rate is the glide duration; one extra
    // block of slack covers the int-vs-double rounding in advanceGlide.
    const int   glideSamples = static_cast<int> (100.0 * 0.001 * kNativeRate);
    const int   blockSize    = 512;
    const int   maxBlocks    = (glideSamples / blockSize) + 4;
    const auto findGlidingVoice = [&]() -> const Voice*
    {
        for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
        {
            const auto& v = alloc.voiceAt (i);
            if (v.isActive() && v.part() == 0 && v.note() == 72)
                return &v;
        }
        return nullptr;
    };

    const Voice* v = findGlidingVoice();
    ASSERT_NE (v, nullptr);
    EXPECT_TRUE (v->isGliding());

    std::array<float, 512> outL {}, outR {};
    bool reachedTarget = false;
    for (int b = 0; b < maxBlocks; ++b)
    {
        alloc.render (outL.data(), outR.data(), blockSize);
        if (! v->isGliding()) { reachedTarget = true; break; }
    }
    EXPECT_TRUE (reachedTarget) << "glide did not converge within "
                                << maxBlocks << " blocks";
}

TEST (VoiceAllocator, GlideOnlyAppliesInMonoLegato)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    // Poly mode with a non-zero glide_time — must NOT glide. Each new note
    // takes a fresh voice (LRU stealing irrelevant here), so the new voice's
    // glide tracker is fresh (current == target).
    VoiceAllocator::PartPolyMode polyWithGlide;
    polyWithGlide.mode        = VoiceAllocator::PartPolyMode::Mode::Poly;
    polyWithGlide.glideTimeMs = 500.0;
    alloc.setPartMode (0, polyWithGlide);
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOn (0, 72, 100, 0.0, false, p);

    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
    {
        const auto& v = alloc.voiceAt (i);
        if (v.isActive() && v.part() == 0)
            EXPECT_FALSE (v.isGliding());
    }
}

TEST (VoiceAllocator, UnisonPitchBendKeepsStackCoherent)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, unisonMode (12.0));
    alloc.setVoiceCount (4);
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.setPitchBend (0, 1.0, p, false);   // +1 semitone bend

    // Every active voice for part 0 must reflect the bend (pitchBend() == 1.0)
    // while keeping its own detune offset.
    int activeCount = 0;
    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
    {
        const auto& v = alloc.voiceAt (i);
        if (v.isActive() && v.part() == 0)
        {
            EXPECT_NEAR (v.pitchBend(), 1.0, 1e-9);
            ++activeCount;
        }
    }
    EXPECT_EQ (activeCount, 4);
}
