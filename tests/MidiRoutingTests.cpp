#include <gtest/gtest.h>

#include "FmRegisterMap.h"
#include "MidiRouter.h"
#include "PartManager.h"
#include "PatchSystem.h"

// --- CC value scaling ---------------------------------------------------------
//
// 07-feature-spec.md "MIDI CC Map":
//   hardware_val = round(cc_val * max_val / 127.0f)
// Boundary values 0 and 127 must map exactly to 0 and max.

TEST (MidiRouting, ScaleCcZeroIsAlwaysZero)
{
    for (int max : { 1, 3, 7, 15, 31, 63, 127 })
    {
        SCOPED_TRACE ("max=" + std::to_string (max));
        EXPECT_EQ (MidiRouter::scaleCC (0, max), 0);
    }
}

TEST (MidiRouting, ScaleCc127IsAlwaysMax)
{
    for (int max : { 1, 3, 7, 15, 31, 63, 127 })
    {
        SCOPED_TRACE ("max=" + std::to_string (max));
        EXPECT_EQ (MidiRouter::scaleCC (127, max), max);
    }
}

TEST (MidiRouting, ScaleCcMidpointsMatchFormula)
{
    // round(64 * 7 / 127) = round(3.527) = 4
    EXPECT_EQ (MidiRouter::scaleCC (64,   7),   4);
    // round(64 * 127 / 127) = 64
    EXPECT_EQ (MidiRouter::scaleCC (64, 127),  64);
    // round(32 * 15 / 127) = round(3.78) = 4
    EXPECT_EQ (MidiRouter::scaleCC (32,  15),   4);
    // round(100 * 31 / 127) = round(24.41) = 24
    EXPECT_EQ (MidiRouter::scaleCC (100, 31),  24);
}

TEST (MidiRouting, ScaleCcClampsOutOfRangeInput)
{
    EXPECT_EQ (MidiRouter::scaleCC (-50,  7),   0);
    EXPECT_EQ (MidiRouter::scaleCC (1000, 7),   7);
}

// --- Pitch bend conversion ----------------------------------------------------
//
// 07-feature-spec.md "Pitch Bend":
//   semitone_offset = (bend_value / 8192.0f) * bend_range_semitones
// Centre (8192) is 0 semitones; extremes are exactly +/- range.

TEST (MidiRouting, PitchBendCentreIsZero)
{
    EXPECT_DOUBLE_EQ (MidiRouter::pitchBendToSemitones (8192, 2), 0.0);
    EXPECT_DOUBLE_EQ (MidiRouter::pitchBendToSemitones (8192, 12), 0.0);
}

TEST (MidiRouting, PitchBendExtremesMatchRange)
{
    // Note: 14-bit max is 16383; +8191 -> +(range * 8191/8192), almost +range.
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (16383, 2), 2.0,  0.001);
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (0,     2), -2.0, 0.001);
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (16383, 12), 12.0, 0.01);
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (0,     12), -12.0, 0.01);
}

TEST (MidiRouting, PitchBendIsLinearOverRange)
{
    // Half-way up from centre with range +/-12 = +6 semitones.
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (8192 + 4096, 12), 6.0, 0.001);
    EXPECT_NEAR (MidiRouter::pitchBendToSemitones (8192 - 4096, 12), -6.0, 0.001);
}

// --- CC -> apvts parameter ID -------------------------------------------------
//
// The CC dispatch turns a (CC number, part) pair into an apvts parameter ID
// that follows 01-architecture.md "Parameter System":
//   per-operator: "<name>_op<1-4>_part<1-6>"
//   per-part:     "<name>_part<1-6>"

TEST (MidiRouting, CcParamIdResolvesOperatorParams)
{
    // CC 16 -> TL OP1, CC 19 -> TL OP4
    EXPECT_EQ (MidiRouter::ccToParamId (16, 0).value_or (juce::String()), "tl_op1_part1");
    EXPECT_EQ (MidiRouter::ccToParamId (19, 2).value_or (juce::String()), "tl_op4_part3");

    // CC 28..31 -> AR OP1..OP4
    EXPECT_EQ (MidiRouter::ccToParamId (28, 5).value_or (juce::String()), "ar_op1_part6");
    EXPECT_EQ (MidiRouter::ccToParamId (31, 5).value_or (juce::String()), "ar_op4_part6");
}

TEST (MidiRouting, CcParamIdResolvesPartParams)
{
    EXPECT_EQ (MidiRouter::ccToParamId (14, 0).value_or (juce::String()), "alg_part1");
    EXPECT_EQ (MidiRouter::ccToParamId (15, 1).value_or (juce::String()), "fb_part2");
    EXPECT_EQ (MidiRouter::ccToParamId (1,  3).value_or (juce::String()), "pms_part4");  // mod wheel -> PMS
    EXPECT_EQ (MidiRouter::ccToParamId (72, 4).value_or (juce::String()), "ams_part5");
    EXPECT_EQ (MidiRouter::ccToParamId (73, 5).value_or (juce::String()), "pms_part6");
}

TEST (MidiRouting, CcParamIdReturnsNulloptForUnmappedAndSpecialCcs)
{
    // Sustain pedal, panic CCs, and unmapped CCs return nullopt.
    EXPECT_FALSE (MidiRouter::ccToParamId (64, 0).has_value());    // sustain
    EXPECT_FALSE (MidiRouter::ccToParamId (120, 0).has_value());   // all sound off
    EXPECT_FALSE (MidiRouter::ccToParamId (121, 0).has_value());   // reset all
    EXPECT_FALSE (MidiRouter::ccToParamId (123, 0).has_value());   // all notes off
    EXPECT_FALSE (MidiRouter::ccToParamId (7,   0).has_value());   // master volume
    EXPECT_FALSE (MidiRouter::ccToParamId (10,  0).has_value());   // pan - special-cased
    EXPECT_FALSE (MidiRouter::ccToParamId (60,  0).has_value());   // unmapped
}

TEST (MidiRouting, CcParamIdRejectsInvalidPartAndCc)
{
    EXPECT_FALSE (MidiRouter::ccToParamId (16, -1).has_value());
    EXPECT_FALSE (MidiRouter::ccToParamId (16,  6).has_value());   // 6 = past last part
    EXPECT_FALSE (MidiRouter::ccToParamId (-1,  0).has_value());
    EXPECT_FALSE (MidiRouter::ccToParamId (128, 0).has_value());
}

TEST (MidiRouting, CcMaxValueMatchesSpec)
{
    EXPECT_EQ (MidiRouter::ccMaxValue (16),  127);   // TL
    EXPECT_EQ (MidiRouter::ccMaxValue (14),    7);   // ALG
    EXPECT_EQ (MidiRouter::ccMaxValue (1),     7);   // mod wheel -> PMS
    EXPECT_EQ (MidiRouter::ccMaxValue (28),   31);   // AR
    EXPECT_EQ (MidiRouter::ccMaxValue (40),   15);   // RR
    EXPECT_EQ (MidiRouter::ccMaxValue (48),    3);   // KS
    EXPECT_EQ (MidiRouter::ccMaxValue (80),    1);   // AMON
    EXPECT_EQ (MidiRouter::ccMaxValue (64),   -1);   // sustain - not a param target
    EXPECT_EQ (MidiRouter::ccMaxValue (200),  -1);
}

// --- Routing table ------------------------------------------------------------

TEST (MidiRouting, DefaultBindingMapsChannels1To6ToFmParts)
{
    MidiRouter router;

    for (int ch = 1; ch <= 6; ++ch)
    {
        SCOPED_TRACE ("channel=" + std::to_string (ch));
        const auto d = router.destinationFor (ch);
        EXPECT_EQ (d.kind, MidiRouter::Destination::Kind::FmPart);
        EXPECT_EQ (d.index, ch - 1);
        EXPECT_TRUE (d.isFmPart());
    }
}

TEST (MidiRouting, ChannelsAbove6AreUnboundByDefault)
{
    MidiRouter router;
    for (int ch : { 7, 9, 16 })
    {
        SCOPED_TRACE ("channel=" + std::to_string (ch));
        EXPECT_EQ (router.destinationFor (ch).kind, MidiRouter::Destination::Kind::None);
        EXPECT_FALSE (router.destinationFor (ch).isFmPart());
    }
}

TEST (MidiRouting, SetDestinationRoundtrips)
{
    MidiRouter router;
    // Reassign channel 10 -> FM part 0 (a deliberate cross-binding).
    router.setDestination (10, { MidiRouter::Destination::Kind::FmPart, 0 });

    EXPECT_TRUE (router.destinationFor (10).isFmPart());
    EXPECT_EQ   (router.destinationFor (10).index, 0);

    // The default binding of channel 1 still resolves to part 0 — multiple
    // channels can target the same destination.
    EXPECT_EQ (router.destinationFor (1).index, 0);
}

TEST (MidiRouting, SetDestinationIgnoresInvalidChannel)
{
    MidiRouter router;
    router.setDestination (0,  { MidiRouter::Destination::Kind::FmPart, 0 });
    router.setDestination (17, { MidiRouter::Destination::Kind::FmPart, 0 });
    EXPECT_EQ (router.destinationFor (0).kind,  MidiRouter::Destination::Kind::None);
    EXPECT_EQ (router.destinationFor (17).kind, MidiRouter::Destination::Kind::None);
}

TEST (MidiRouting, ResetRoutingRestoresDefaults)
{
    MidiRouter router;
    router.setDestination (1, {});
    router.setDestination (3, {});
    router.resetRouting();

    for (int part = 0; part < PartManager::kNumParts; ++part)
    {
        SCOPED_TRACE ("part=" + std::to_string (part));
        EXPECT_EQ (router.destinationFor (part + 1).index, part);
    }
}

// --- Per-part live state ------------------------------------------------------

TEST (MidiRouting, PitchBendStateIsPerPart)
{
    MidiRouter router;
    router.setPitchBendSemitones (0,  2.0);
    router.setPitchBendSemitones (3, -1.5);

    EXPECT_DOUBLE_EQ (router.pitchBendSemitones (0),  2.0);
    EXPECT_DOUBLE_EQ (router.pitchBendSemitones (1),  0.0);   // untouched
    EXPECT_DOUBLE_EQ (router.pitchBendSemitones (3), -1.5);
}

TEST (MidiRouting, SustainPedalStateIsPerPart)
{
    MidiRouter router;
    router.setSustainPedalHeld (2, true);

    EXPECT_FALSE (router.sustainPedalHeld (0));
    EXPECT_TRUE  (router.sustainPedalHeld (2));
}

TEST (MidiRouting, ResetControllersClearsBendAndSustain)
{
    MidiRouter router;
    router.setPitchBendSemitones (1, 4.0);
    router.setSustainPedalHeld   (1, true);
    router.resetControllers (1);

    EXPECT_DOUBLE_EQ (router.pitchBendSemitones (1), 0.0);
    EXPECT_FALSE     (router.sustainPedalHeld   (1));
}

// --- Velocity -> carrier TL ---------------------------------------------------
//
// 07-feature-spec.md velocity -> TL: scale only carriers, leave modulators
// alone. The carrier mask depends on the algorithm.

TEST (FmRegisterMap, VelocityScalingIsNoopWhenDisabled)
{
    EXPECT_EQ (FmRegisterMap::scaleCarrierTl (40, 7, 3, 64, false), 40);
    EXPECT_EQ (FmRegisterMap::scaleCarrierTl (40, 0, 3, 1,  false), 40);
}

TEST (FmRegisterMap, VelocityScalingLeavesModulatorsUnchanged)
{
    // Algorithm 0: only OP4 is a carrier. OP1/2/3 are modulators and must
    // pass through even with vel < 127.
    for (int op : { 0, 1, 2 })
    {
        SCOPED_TRACE ("op=" + std::to_string (op));
        EXPECT_EQ (FmRegisterMap::scaleCarrierTl (20, 0, op, 64, true), 20);
    }
    // OP4 IS the carrier on alg 0 -> gets attenuated.
    EXPECT_GT (FmRegisterMap::scaleCarrierTl (20, 0, 3, 64, true), 20);
}

TEST (FmRegisterMap, VelocityScalingFullVelocityIsNoop)
{
    // vel == 127 -> +0 attenuation, all carriers unchanged.
    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE ("op=" + std::to_string (op));
        EXPECT_EQ (FmRegisterMap::scaleCarrierTl (30, 7, op, 127, true), 30);
    }
}

TEST (FmRegisterMap, VelocityScalingAddsHalfRangeAttenuation)
{
    // (127 - 1) / 2 = 63 -> tl 20 + 63 = 83.
    EXPECT_EQ (FmRegisterMap::scaleCarrierTl (20, 7, 0, 1, true), 83);
    // Clamp at 127 even when the formula overshoots.
    EXPECT_EQ (FmRegisterMap::scaleCarrierTl (100, 7, 0, 0, true), 127);
}

TEST (FmRegisterMap, VelocityScalingCarrierMaskMatchesAlgorithms)
{
    using namespace FmRegisterMap;
    // Quick spot-checks of the carrier mask:
    //   alg 4 -> OP2 + OP4 carriers
    EXPECT_EQ (kCarrierMaskByAlg[4], 0b1010);
    //   alg 5/6 -> OP2 + OP3 + OP4 carriers
    EXPECT_EQ (kCarrierMaskByAlg[5], 0b1110);
    EXPECT_EQ (kCarrierMaskByAlg[6], 0b1110);
    //   alg 7 -> all carriers
    EXPECT_EQ (kCarrierMaskByAlg[7], 0b1111);
}

// --- Pitch bend in buildNoteOn -----------------------------------------------

TEST (FmRegisterMap, PitchBendShiftsFrequency)
{
    Patch p {};
    p.alg = 7;
    p.lr  = 3;
    for (int op = 0; op < 4; ++op)
        p.mul[op] = 1;

    const auto centred = FmRegisterMap::buildNoteOn (p, 69);
    const auto bentUp  = FmRegisterMap::buildNoteOn (p, 69,
                            FmRegisterMap::NoteParams { 127, false, 2.0 });
    const auto bentDn  = FmRegisterMap::buildNoteOn (p, 69,
                            FmRegisterMap::NoteParams { 127, false, -2.0 });

    // The frequency low byte is written second-to-last (index kNoteOnWriteCount-2).
    // A +2 semitone bend must raise it; -2 must lower it; centre is unchanged.
    const auto freqLowReg = [] (const auto& writes) {
        return writes[FmRegisterMap::kNoteOnWriteCount - 2];
    };

    EXPECT_EQ (freqLowReg (centred).reg, 0xA0);
    EXPECT_EQ (freqLowReg (bentUp).reg,  0xA0);
    EXPECT_EQ (freqLowReg (bentDn).reg,  0xA0);

    // The note can be reconstructed from the freq HIGH+LOW pair (index
    // kNoteOnWriteCount-3 is 0xA4). Compare against centred.
    const int hiCentred = centred[FmRegisterMap::kNoteOnWriteCount - 3].value;
    const int loCentred = centred[FmRegisterMap::kNoteOnWriteCount - 2].value;
    const int hiUp      = bentUp[FmRegisterMap::kNoteOnWriteCount - 3].value;
    const int loUp      = bentUp[FmRegisterMap::kNoteOnWriteCount - 2].value;
    const int hiDn      = bentDn[FmRegisterMap::kNoteOnWriteCount - 3].value;
    const int loDn      = bentDn[FmRegisterMap::kNoteOnWriteCount - 2].value;

    EXPECT_NE (std::make_pair (hiUp, loUp),  std::make_pair (hiCentred, loCentred));
    EXPECT_NE (std::make_pair (hiDn, loDn),  std::make_pair (hiCentred, loCentred));
}

TEST (FmRegisterMap, MidiNoteToFreqAcceptsFractionalNotes)
{
    // A +12 semitone offset must double the implied frequency, just like
    // jumping a whole MIDI octave.
    const auto a4 = FmRegisterMap::midiNoteToFreq (69.0);
    const auto a5 = FmRegisterMap::midiNoteToFreq (69.0 + 12.0);
    const double hzA4 = a4.fnum * 53267.0 / static_cast<double> (1 << (21 - a4.blk));
    const double hzA5 = a5.fnum * 53267.0 / static_cast<double> (1 << (21 - a5.blk));
    EXPECT_NEAR (hzA5 / hzA4, 2.0, 0.01);
}
