#pragma once

#include <array>
#include <optional>

#include <juce_core/juce_core.h>

#include "PartManager.h"

// Maps MIDI channels to destinations and tracks per-part live MIDI state
// (pitch bend, sustain pedal). Only FM destinations are populated for Task 06
// — Task 07 extends Destination::Kind with PSG slots and the DAC.
//
// The class is *state-only*: it owns no callbacks into the audio engine. The
// PluginProcessor owns the dispatch — it asks the router for the channel's
// destination, the per-part bend / sustain state, and uses the static
// helpers to scale CC values and resolve CC -> apvts parameter IDs.
//
// This split lets unit tests exercise the pure scaling / lookup logic without
// constructing a JUCE plugin host (01-architecture.md "MIDI Pipeline").
class MidiRouter
{
public:
    struct Destination
    {
        // Task 07 adds PsgSlot and Dac alongside FmPart.
        enum class Kind { None, FmPart };

        Kind kind  = Kind::None;
        int  index = -1;   // FM part 0..5 when kind == FmPart

        bool isFmPart() const noexcept { return kind == Kind::FmPart && index >= 0; }
    };

    MidiRouter();

    // --- Pure helpers (unit-tested) ------------------------------------------

    // The CC value scaling formula from 07-feature-spec.md MIDI CC Map:
    //   hardware_val = round(cc_val * max_val / 127.0f)
    // Boundary values 0 and 127 map exactly to 0 and max.
    static int scaleCC (int ccValue, int maxValue) noexcept;

    // 14-bit signed pitch wheel value (0..16383, 8192=centre) -> semitone
    // offset, given a positive ± range in semitones.
    static double pitchBendToSemitones (int bend14bit, int rangeSemitones) noexcept;

    // CC number -> apvts parameter ID for the given part (1-indexed in the ID).
    // Returns std::nullopt for unmapped or non-parameter CCs (sustain, panic,
    // and CCs whose targets do not exist yet, e.g. CC 7 master volume).
    static std::optional<juce::String> ccToParamId (int ccNumber, int part);

    // Hardware-range max for a CC, matching the 07-feature-spec.md table. Used
    // alongside scaleCC for raw atomic stores into the apvts parameter.
    // Returns -1 for CCs with no parameter target.
    static int ccMaxValue (int ccNumber) noexcept;

    // --- Routing table -------------------------------------------------------

    // Default binding: MIDI channel i+1 -> FM part i for i in 0..5; channels
    // 7..16 unbound (Destination::Kind::None) until PSG/DAC arrive (Task 07).
    void resetRouting() noexcept;

    void        setDestination (int midiChannel /* 1-16 */, Destination dest) noexcept;
    Destination destinationFor (int midiChannel /* 1-16 */) const noexcept;

    // --- Per-part live MIDI state --------------------------------------------

    double pitchBendSemitones (int part) const noexcept;
    void   setPitchBendSemitones (int part, double semitones) noexcept;

    bool sustainPedalHeld (int part) const noexcept;
    void setSustainPedalHeld (int part, bool held) noexcept;

    // CC 121 (Reset All Controllers) target for one part: bend back to centre,
    // sustain released. Does not touch the routing table.
    void resetControllers (int part) noexcept;

private:
    // 1-indexed (channels 1..16), slot 0 unused so lookups need no -1.
    std::array<Destination, 17>                  channelMap {};
    std::array<double, PartManager::kNumParts>   bendSemitones {};
    std::array<bool,   PartManager::kNumParts>   sustainHeld {};
};
