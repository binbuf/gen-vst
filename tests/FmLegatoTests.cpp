#include <gtest/gtest.h>

#include "PatchSystem.h"
#include "VoiceAllocator.h"

// LEGATO semantics (02-fm-synthesis.md *Voice handling — LEGATO and RETRIG*):
// when a re-keyed voice is in LEGATO mode it does **not** issue the
// key-off / key-on pair (0x28 = 0x00 then 0x28 = 0xF0). The frequency
// registers update, operator params dirty-diff, but the envelope rides
// through into the new note's pitch — there's no Released voice tail
// stacked up, no Idle voice spun up, and the active voice stays Active.
//
// The VoiceAllocator's Mono+monoLegato mode in noteOnMono() drives this
// path through `Voice::legatoTo`, which ultimately re-runs updateRegisters
// (writing only the registers that differ from the shadow — by construction
// not 0x28, since the helper skips it). These tests check the *observable*
// side effects: the voice state machine and the activated-note tracking.

namespace
{
    Patch makePatch()
    {
        Patch p {};
        p.alg = 7;
        p.lr  = 3;
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op] = 1;
            p.ar[op]  = 31;
            p.tl[op]  = 20;
            p.rr[op]  = 15;
        }
        return p;
    }

    VoiceAllocator::PartPolyMode monoLegato()
    {
        VoiceAllocator::PartPolyMode m {
            VoiceAllocator::PartPolyMode::Mode::Mono, /*monoLegato*/ true, 12.0 };
        return m;
    }

    VoiceAllocator::PartPolyMode monoRetrig()
    {
        VoiceAllocator::PartPolyMode m {
            VoiceAllocator::PartPolyMode::Mode::Mono, /*monoLegato*/ false, 12.0 };
        return m;
    }
}

// --- LEGATO suppresses key-off/key-on on re-keyed voices ---------------------

TEST (FmLegato, LegatoReKeyKeepsVoiceActiveWithoutReleasedTail)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoLegato());
    const Patch p = makePatch();

    // 1. First note: voice becomes Active.
    alloc.noteOn (0, 60, 100, 0.0, false, p);
    EXPECT_EQ (alloc.numActiveVoices(),    1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);

    // 2. Re-key with a different note: LEGATO must update pitch in place. The
    //    voice stays Active; no key-off was issued, so no Released tail
    //    appears, and the same physical voice now serves note 64.
    alloc.noteOn (0, 64, 100, 0.0, false, p);

    EXPECT_EQ (alloc.numActiveVoices(),    1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0)
        << "LEGATO re-key issued a key-off (got a Released tail) — must not";
    EXPECT_TRUE  (alloc.isNoteActive (0, 64));
    EXPECT_FALSE (alloc.isNoteActive (0, 60));
}

TEST (FmLegato, LegatoTransitionsAcrossSeveralNotesPreservesEnvelope)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoLegato());
    const Patch p = makePatch();

    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOn (0, 64, 100, 0.0, false, p);
    alloc.noteOn (0, 67, 100, 0.0, false, p);
    alloc.noteOn (0, 72, 100, 0.0, false, p);

    // After 4 LEGATO note-ons the part still owns exactly one Active voice and
    // zero Released voices — every transition was a legato hop, not a retrigger.
    EXPECT_EQ (alloc.numActiveVoices(),    1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0);
    EXPECT_TRUE (alloc.isNoteActive (0, 72));
}

// --- RETRIG (the contrast case) ----------------------------------------------

TEST (FmLegato, RetrigReKeyReplacesVoiceWithoutLeavingReleasedTail)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoRetrig());
    const Patch p = makePatch();

    // Retrigger mode reuses the same physical voice slot via Voice::noteOn —
    // the buildNoteOn() sequence opens with 0x28 = 0x00 (key-off) and closes
    // with 0x28 = 0xF0 (key-on), so the envelope re-attacks fully inside a
    // single write burst. The voice never enters the Released state.
    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOn (0, 64, 100, 0.0, false, p);

    EXPECT_EQ (alloc.numActiveVoices(),    1);
    EXPECT_EQ (alloc.numReleasingVoices(), 0)
        << "RETRIG in-place re-key should not leave a separate Released voice";
    EXPECT_TRUE  (alloc.isNoteActive (0, 64));
    EXPECT_FALSE (alloc.isNoteActive (0, 60));
}

// --- Edge: legato into a released voice tail ---------------------------------

TEST (FmLegato, LegatoIntoReleasedVoiceUpdatesPitchWithoutNewAllocation)
{
    VoiceAllocator alloc;
    alloc.prepare (44100.0, 512);
    alloc.setPartMode (0, monoLegato());
    const Patch p = makePatch();

    // First note-on; release it; the voice enters Released phase but is still
    // ringing. A LEGATO note-on while the voice is in release picks the
    // voice back up (the legato path matches both Active and Released voices
    // for the part in noteOnMono()'s "find existing" pass) — the envelope
    // continues from wherever its release tail had reached, the operator
    // registers / frequency dirty-diff to the new pitch. The voice's state
    // stays Released because we never issued a key-on; the released tail
    // simply now sounds at the new note's pitch until it decays.
    alloc.noteOn (0, 60, 100, 0.0, false, p);
    alloc.noteOff (0, 60, /*sustainHeld*/ false);
    EXPECT_EQ (alloc.numActiveVoices(),    0);
    EXPECT_EQ (alloc.numReleasingVoices(), 1);

    alloc.noteOn (0, 64, 100, 0.0, false, p);

    // After the LEGATO hop the same physical voice is now serving note 64.
    // No fresh Idle voice spun up (that would mean 1 Active + 1 Released = 2).
    EXPECT_EQ (alloc.numActiveVoices() + alloc.numReleasingVoices(), 1);

    // The Released voice now tracks note 64 — verify via voiceAt(...) so we
    // count regardless of Active/Released state (isNoteActive() checks Active
    // only, which would miss our resurrected Released voice).
    bool foundVoiceOnNew = false;
    for (int i = 0; i < VoiceAllocator::kNumVoices; ++i)
    {
        const auto& v = alloc.voiceAt (i);
        if (! v.isIdle() && v.part() == 0 && v.note() == 64)
            foundVoiceOnNew = true;
    }
    EXPECT_TRUE (foundVoiceOnNew) << "LEGATO hop must update the voice's note()";
}
