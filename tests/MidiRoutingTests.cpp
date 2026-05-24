#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

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

TEST (MidiRouting, GapChannelsAreUnboundByDefault)
{
    // Channels 7-10 and 15 fall outside any default binding (FM, PSG, DAC).
    MidiRouter router;
    for (int ch : { 7, 8, 9, 10, 15 })
    {
        SCOPED_TRACE ("channel=" + std::to_string (ch));
        EXPECT_EQ (router.destinationFor (ch).kind, MidiRouter::Destination::Kind::None);
        EXPECT_FALSE (router.destinationFor (ch).isFmPart());
    }
}

TEST (MidiRouting, DefaultBindingMapsChannels11To13ToPsgTone)
{
    MidiRouter router;
    for (int slot = 0; slot < 3; ++slot)
    {
        const int ch = 11 + slot;
        SCOPED_TRACE ("channel=" + std::to_string (ch));
        const auto d = router.destinationFor (ch);
        EXPECT_EQ (d.kind, MidiRouter::Destination::Kind::PsgTone);
        EXPECT_EQ (d.index, slot);
        EXPECT_TRUE (d.isPsgTone());
    }
}

TEST (MidiRouting, DefaultBindingMapsChannel14ToPsgNoise)
{
    MidiRouter router;
    const auto d = router.destinationFor (14);
    EXPECT_EQ (d.kind, MidiRouter::Destination::Kind::PsgNoise);
    EXPECT_TRUE (d.isPsgNoise());
    EXPECT_FALSE (d.isFmPart());
}

TEST (MidiRouting, DefaultBindingMapsChannel16ToDac)
{
    MidiRouter router;
    const auto d = router.destinationFor (16);
    EXPECT_EQ (d.kind, MidiRouter::Destination::Kind::Dac);
    EXPECT_TRUE (d.isDac());
    EXPECT_FALSE (d.isFmPart());
}

TEST (MidiRouting, SetDestinationMovesDestinationToChannel)
{
    MidiRouter router;
    // Reassign channel 10 -> FM part 0. Destination-centric semantics
    // (Task 13): a destination has at most one MIDI channel, so FM Part 0
    // moves OFF channel 1 onto channel 10.
    router.setDestination (10, { MidiRouter::Destination::Kind::FmPart, 0 });

    EXPECT_TRUE (router.destinationFor (10).isFmPart());
    EXPECT_EQ   (router.destinationFor (10).index, 0);

    // Channel 1 no longer routes anywhere — FM Part 0 moved.
    EXPECT_EQ (router.destinationFor (1).kind, MidiRouter::Destination::Kind::None);
}

TEST (MidiRouting, SetDestinationChannelMovesDestination)
{
    MidiRouter router;
    using Kind = MidiRouter::Destination::Kind;
    const int fm0 = MidiRouter::destinationId ({ Kind::FmPart, 0 });

    router.setDestinationChannel (fm0, 7);
    EXPECT_EQ (router.destinationChannel (fm0), 7);

    EXPECT_EQ (router.destinationFor (7).kind, Kind::FmPart);
    EXPECT_EQ (router.destinationFor (7).index, 0);
    EXPECT_EQ (router.destinationFor (1).kind, Kind::None);

    // Setting to channel 0 means "Off" — no channel routes to FM Part 0.
    router.setDestinationChannel (fm0, 0);
    EXPECT_EQ (router.destinationChannel (fm0), 0);
    EXPECT_EQ (router.destinationFor (7).kind, Kind::None);
}

TEST (MidiRouting, ForEachDestinationCoversAllOnChannel)
{
    MidiRouter router;
    using Kind = MidiRouter::Destination::Kind;
    const int fm0   = MidiRouter::destinationId ({ Kind::FmPart, 0 });
    const int noise = MidiRouter::destinationId ({ Kind::PsgNoise, 0 });

    // Channel 7 has no default destination — pick it so the layering
    // assertion isn't contaminated by the default FM mapping.
    router.setDestinationChannel (fm0,   7);
    router.setDestinationChannel (noise, 7);

    std::vector<MidiRouter::Destination::Kind> seen;
    router.forEachDestination (7, [&] (MidiRouter::Destination d) { seen.push_back (d.kind); });

    EXPECT_EQ (seen.size(), 2u);
    EXPECT_NE (std::find (seen.begin(), seen.end(), Kind::FmPart),   seen.end());
    EXPECT_NE (std::find (seen.begin(), seen.end(), Kind::PsgNoise), seen.end());
}

TEST (MidiRouting, DestinationIdRoundTripsThroughDestination)
{
    using Kind = MidiRouter::Destination::Kind;
    const auto check = [] (MidiRouter::Destination d) {
        const int id = MidiRouter::destinationId (d);
        const auto back = MidiRouter::destinationFromId (id);
        EXPECT_EQ (back.kind, d.kind);
        if (d.kind == Kind::FmPart || d.kind == Kind::PsgTone)
            EXPECT_EQ (back.index, d.index);
    };
    for (int i = 0; i < 6; ++i) check ({ Kind::FmPart, i });
    for (int i = 0; i < 3; ++i) check ({ Kind::PsgTone, i });
    check ({ Kind::PsgNoise, 0 });
    check ({ Kind::Dac,      0 });
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

// --- Task 22 — Rack routing (range / transpose / detune) ---------------------

TEST (MidiRoutingRack, NoteInRangeIsInclusiveAtBothEnds)
{
    // The widget's RNG slider is inclusive at both ends; a note exactly equal
    // to lo or hi must still pass.
    EXPECT_TRUE (MidiRouter::noteInRange (60, 60, 72));
    EXPECT_TRUE (MidiRouter::noteInRange (72, 60, 72));
    EXPECT_TRUE (MidiRouter::noteInRange (66, 60, 72));
}

TEST (MidiRoutingRack, NoteOutOfRangeIsDropped)
{
    EXPECT_FALSE (MidiRouter::noteInRange (59, 60, 72));
    EXPECT_FALSE (MidiRouter::noteInRange (73, 60, 72));
    EXPECT_FALSE (MidiRouter::noteInRange (0,  60, 72));
    EXPECT_FALSE (MidiRouter::noteInRange (127, 60, 72));
}

TEST (MidiRoutingRack, NoteInRangeAcceptsFullKeyboard)
{
    for (int n : { 0, 1, 60, 64, 96, 127 })
        EXPECT_TRUE (MidiRouter::noteInRange (n, 0, 127));
}

TEST (MidiRoutingRack, NoteInRangeIsRobustToSwappedLoHi)
{
    // If somehow lo > hi reaches the dispatcher, the helper still works.
    EXPECT_TRUE  (MidiRouter::noteInRange (65, 72, 60));   // window 60..72
    EXPECT_FALSE (MidiRouter::noteInRange (50, 72, 60));
}

TEST (MidiRoutingRack, TransposeSemitoneAndOctaveStack)
{
    // Transpose +12 (one octave up) sends C4 (60) to C5 (72).
    EXPECT_EQ (MidiRouter::applyTranspose (60, 12, 0), 72);
    // Transpose +1 octave sends C4 (60) to C5 (72).
    EXPECT_EQ (MidiRouter::applyTranspose (60, 0,  1), 72);
    // Mixed: +1 semitone + +1 octave = +13 semitones.
    EXPECT_EQ (MidiRouter::applyTranspose (60, 1,  1), 73);
    // Negative transpose lowers.
    EXPECT_EQ (MidiRouter::applyTranspose (60, 0, -1), 48);
    EXPECT_EQ (MidiRouter::applyTranspose (60, -7, -1), 41);
}

TEST (MidiRoutingRack, TransposeIsIdentityAtZero)
{
    for (int n : { 0, 60, 127 })
        EXPECT_EQ (MidiRouter::applyTranspose (n, 0, 0), n);
}

TEST (MidiRoutingRack, DetuneCentsConvertsToFractionalSemitones)
{
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (0),     0.0);
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (50),    0.5);
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (-50),  -0.5);
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (100),   1.0);
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (-100), -1.0);
}

TEST (MidiRoutingRack, DetuneCentsClampsOutOfRangeInput)
{
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (200),   1.0);
    EXPECT_DOUBLE_EQ (MidiRouter::detuneCentsToSemitones (-9999),-1.0);
}

TEST (MidiRoutingRack, TransposePlusRangeFiltersCorrectly)
{
    // Compose: an incoming note transposed by +12, then range-filtered.
    // C4 (60) + 12 = C5 (72), and a window of C5..C6 (72..84) accepts it.
    const int transposed = MidiRouter::applyTranspose (60, 12, 0);
    EXPECT_EQ (transposed, 72);
    EXPECT_TRUE (MidiRouter::noteInRange (transposed, 72, 84));

    // C3 (48) transposed by +12 = C4 (60). A C5..C6 window now drops it.
    const int below = MidiRouter::applyTranspose (48, 12, 0);
    EXPECT_EQ (below, 60);
    EXPECT_FALSE (MidiRouter::noteInRange (below, 72, 84));
}

TEST (MidiRoutingRack, DeferredChannelWriteRebuildsMaskInOneShot)
{
    MidiRouter router;
    using Kind = MidiRouter::Destination::Kind;
    const int fm0 = MidiRouter::destinationId ({ Kind::FmPart, 0 });
    const int fm1 = MidiRouter::destinationId ({ Kind::FmPart, 1 });

    router.setDestinationChannelDeferred (fm0, 7);
    router.setDestinationChannelDeferred (fm1, 7);
    // Until rebuild, the old mask is still in effect (FM0 still on ch1).
    EXPECT_EQ (router.destinationFor (1).kind, Kind::FmPart);
    EXPECT_EQ (router.destinationFor (1).index, 0);

    router.rebuildChannelMaskAfterDeferredWrites();
    // Now both share channel 7 — the layering test mirror of the existing
    // forEachDestination coverage, exercised via the deferred path.
    std::vector<MidiRouter::Destination::Kind> seen;
    router.forEachDestination (7, [&] (MidiRouter::Destination d) { seen.push_back (d.kind); });
    EXPECT_EQ (seen.size(), 2u);
}

// --- PartManager — Task 22 rack slot pool ------------------------------------

TEST (PartManagerRack, DefaultStateHasOnlyFmSlotZeroActive)
{
    PartManager pm;
    EXPECT_TRUE  (pm.isSlotActive ({ PartManager::InstrumentType::FM, 0 }));
    for (int i = 1; i < PartManager::kNumRackFmSlots; ++i)
        EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::FM, i }));
    for (int i = 0; i < PartManager::kNumRackSqSlots; ++i)
        EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::SQ, i }));
    EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::D, 0 }));
}

TEST (PartManagerRack, GetFreeSlotReturnsLowestIndexAvailable)
{
    PartManager pm;
    // Default: FM slot 0 active -> next free FM slot is index 1.
    EXPECT_EQ (pm.getFreeSlot (PartManager::InstrumentType::FM)->index, 1);
    // SQ + D pools start empty -> index 0 is free.
    EXPECT_EQ (pm.getFreeSlot (PartManager::InstrumentType::SQ)->index, 0);
    EXPECT_EQ (pm.getFreeSlot (PartManager::InstrumentType::D)->index,  0);
}

TEST (PartManagerRack, GetFreeSlotReturnsNulloptWhenPoolFull)
{
    PartManager pm;
    for (int i = 0; i < PartManager::kNumRackFmSlots; ++i)
        pm.setSlotActive ({ PartManager::InstrumentType::FM, i }, true);
    EXPECT_FALSE (pm.getFreeSlot (PartManager::InstrumentType::FM).has_value());

    pm.setSlotActive ({ PartManager::InstrumentType::D, 0 }, true);
    EXPECT_FALSE (pm.getFreeSlot (PartManager::InstrumentType::D).has_value());
}

TEST (PartManagerRack, SetSlotActiveTogglesAreIdempotent)
{
    // The state change itself is straightforward to test; the
    // ChangeBroadcaster dispatch is JUCE message-thread infrastructure and
    // exercised by the running plugin, so we restrict the unit test to the
    // observable PartManager state.
    PartManager pm;
    EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::SQ, 0 }));

    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    EXPECT_TRUE  (pm.isSlotActive ({ PartManager::InstrumentType::SQ, 0 }));

    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);   // idempotent
    EXPECT_TRUE  (pm.isSlotActive ({ PartManager::InstrumentType::SQ, 0 }));

    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, false);
    EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::SQ, 0 }));
}

TEST (PartManagerRack, SetSlotActiveRejectsOutOfRangeIndex)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::FM, -1 }, true);   // no-op
    pm.setSlotActive ({ PartManager::InstrumentType::FM, 99 }, true);   // no-op
    EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::FM, -1 }));
    EXPECT_FALSE (pm.isSlotActive ({ PartManager::InstrumentType::FM, 99 }));
}

TEST (PartManagerRack, SlotPoolSizesMatchSpec)
{
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::FM),
               PartManager::kNumRackFmSlots);
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::SQ),
               PartManager::kNumRackSqSlots);
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::D),
               PartManager::kNumRackDSlots);
    // The pool sizes from the task spec: 5 FM + 4 SQ + 1 D.
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::FM), 5);
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::SQ), 4);
    EXPECT_EQ (PartManager::slotPoolSize (PartManager::InstrumentType::D),  1);
}

// --- PartManager — Task 27 rack ordering -------------------------------------

TEST (PartManagerRack, RackOrderStartsWithDefaultFmSlot)
{
    PartManager pm;
    const auto& order = pm.getRackOrder();
    ASSERT_EQ (order.size(), 1u);
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::FM);
    EXPECT_EQ (order[0].index, 0);
}

TEST (PartManagerRack, ActivatingSlotAppendsToRackOrder)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 2 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    const auto& order = pm.getRackOrder();
    ASSERT_EQ (order.size(), 3u);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[1].index, 2);
    EXPECT_EQ (order[2].type,  PartManager::InstrumentType::D);
    EXPECT_EQ (order[2].index, 0);
}

TEST (PartManagerRack, DeactivatingSlotRemovesFromRackOrder)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, false);

    const auto& order = pm.getRackOrder();
    ASSERT_EQ (order.size(), 2u);
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::FM);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::D);
}

TEST (PartManagerRack, ReorderSlotMovesEntryDown)
{
    PartManager pm;
    // Build [FM 0, SQ 0, SQ 1, D 0]
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 1 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    ASSERT_EQ (pm.getRackOrder().size(), 4u);

    // Move row 0 to row 2 -> [SQ 0, SQ 1, FM 0, D 0]
    pm.reorderSlot (0, 2);
    const auto& order = pm.getRackOrder();
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[0].index, 0);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[1].index, 1);
    EXPECT_EQ (order[2].type,  PartManager::InstrumentType::FM);
    EXPECT_EQ (order[2].index, 0);
    EXPECT_EQ (order[3].type,  PartManager::InstrumentType::D);
    EXPECT_EQ (order[3].index, 0);
}

TEST (PartManagerRack, ReorderSlotMovesEntryUp)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 1 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    // [FM 0, SQ 0, SQ 1, D 0] -> move row 3 (D) to row 1 -> [FM 0, D 0, SQ 0, SQ 1]
    pm.reorderSlot (3, 1);
    const auto& order = pm.getRackOrder();
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::FM);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::D);
    EXPECT_EQ (order[2].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[2].index, 0);
    EXPECT_EQ (order[3].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[3].index, 1);
}

TEST (PartManagerRack, ReorderSlotRejectsOutOfRangeIndices)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    const auto snapshot = pm.getRackOrder();

    pm.reorderSlot (-1, 0);    // no-op
    pm.reorderSlot (0,  -1);   // no-op
    pm.reorderSlot (0,  99);   // no-op
    pm.reorderSlot (99, 0);    // no-op
    pm.reorderSlot (1,  1);    // identical -> no-op

    EXPECT_EQ (pm.getRackOrder(), snapshot);
}

TEST (PartManagerRack, ReorderSurvivesRoundTripWithDeactivation)
{
    // A more realistic flow: user adds three rows, drags one of them, then
    // removes the dragged row. Ordering must remain consistent.
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 0 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    // [FM 0, SQ 0, D 0]
    pm.reorderSlot (0, 2);
    // [SQ 0, D 0, FM 0]
    pm.setSlotActive ({ PartManager::InstrumentType::D, 0 }, false);
    // [SQ 0, FM 0]
    const auto& order = pm.getRackOrder();
    ASSERT_EQ (order.size(), 2u);
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::FM);
}

TEST (PartManagerRack, SetRackOrderReplacesWholeVector)
{
    PartManager pm;
    pm.setSlotActive ({ PartManager::InstrumentType::SQ, 1 }, true);
    pm.setSlotActive ({ PartManager::InstrumentType::D,  0 }, true);
    pm.setRackOrder ({
        { PartManager::InstrumentType::D,  0 },
        { PartManager::InstrumentType::FM, 0 },
        { PartManager::InstrumentType::SQ, 1 },
    });
    const auto& order = pm.getRackOrder();
    ASSERT_EQ (order.size(), 3u);
    EXPECT_EQ (order[0].type,  PartManager::InstrumentType::D);
    EXPECT_EQ (order[1].type,  PartManager::InstrumentType::FM);
    EXPECT_EQ (order[2].type,  PartManager::InstrumentType::SQ);
    EXPECT_EQ (order[2].index, 1);
}
