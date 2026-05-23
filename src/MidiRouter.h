#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

#include "PartManager.h"

// Maps MIDI channels to destinations and tracks per-part live MIDI state
// (pitch bend, sustain pedal).
//
// Storage model (Task 13): the source of truth is `destChannel[]`, a
// destination-centric array (one slot per FM part, PSG tone, PSG noise, DAC)
// holding the MIDI channel currently assigned to each destination (0 = off,
// 1..16 = mapped channel). A derived `channelDestMask[]` — one bitmask per
// MIDI channel — is rebuilt from `destChannel` on every write and is what the
// audio thread reads, so multi-destination layering (two destinations sharing
// a channel) is supported without locks.
//
// The class is *state-only*: it owns no callbacks into the audio engine. The
// PluginProcessor uses `forEachDestination` to dispatch a MIDI event to every
// destination subscribed to its channel.
class MidiRouter
{
public:
    struct Destination
    {
        // Kinds:
        //   FmPart   - index 0..5 selects one of the six FM parts.
        //   PsgTone  - index 0..2 selects one of the three SN76489 tone
        //              channels.
        //   PsgNoise - the single SN76489 noise channel (index unused).
        //   Dac      - the dedicated DAC channel (index unused).
        enum class Kind { None, FmPart, PsgTone, PsgNoise, Dac };

        Kind kind  = Kind::None;
        int  index = -1;

        bool isFmPart()   const noexcept { return kind == Kind::FmPart && index >= 0; }
        bool isPsgTone()  const noexcept { return kind == Kind::PsgTone && index >= 0; }
        bool isPsgNoise() const noexcept { return kind == Kind::PsgNoise; }
        bool isDac()      const noexcept { return kind == Kind::Dac; }
    };

    // Destination IDs are dense 0..kNumDestinations-1 indices used internally
    // and exposed to the JS routing UI:
    //   0..5  -> FM parts 0..5
    //   6..8  -> PSG tones 0..2
    //   9     -> PSG noise
    //   10    -> DAC
    static constexpr int kNumDestinations = 11;

    static int          destinationId  (Destination d) noexcept;
    static Destination  destinationFromId (int id) noexcept;

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

    // Default binding:
    //   FM parts 0..5  -> MIDI 1..6
    //   PSG tones 0..2 -> MIDI 11..13
    //   PSG noise      -> MIDI 14
    //   DAC            -> MIDI 16
    void resetRouting() noexcept;

    // Destination-centric API used by the Task 13 UI (08-ui-views.md view 5).
    // `midiChannel` 1..16 = mapped, 0 = off. Concurrent UI writers and
    // audio-thread readers stay synchronised via the atomic `channelDestMask`.
    void setDestinationChannel (int destId, int midiChannel) noexcept;
    int  destinationChannel    (int destId) const noexcept;

    // Channel-centric backward-compat shims. `setDestination` is a
    // destination-move: it places `dest` on `midiChannel`, removing it from
    // any previous channel. (The pre-Task-13 channel-centric semantics — where
    // a single channel held one destination and multiple channels could
    // target the same destination — are replaced by the destination-centric
    // model.)
    void        setDestination (int midiChannel /* 1-16, 0 = off */,
                                Destination dest) noexcept;
    Destination destinationFor (int midiChannel /* 1-16 */) const noexcept;

    // Multi-destination audio-thread dispatch (Task 13): iterate every
    // destination currently subscribed to `midiChannel`. Two destinations on
    // the same channel is permitted ("a valid layer setup", 08-ui-views.md
    // view 5) — fn() is called once per destination, in destination-id order.
    template <typename Fn>
    void forEachDestination (int midiChannel, Fn&& fn) const noexcept
    {
        if (midiChannel < 1 || midiChannel > 16) return;
        std::uint16_t bits = channelDestMask[static_cast<std::size_t> (midiChannel)]
                                .load (std::memory_order_relaxed);
        for (int d = 0; bits != 0; ++d, bits >>= 1)
            if ((bits & 1u) != 0u)
                fn (destinationFromId (d));
    }

    // --- Per-part live MIDI state --------------------------------------------

    double pitchBendSemitones (int part) const noexcept;
    void   setPitchBendSemitones (int part, double semitones) noexcept;

    bool sustainPedalHeld (int part) const noexcept;
    void setSustainPedalHeld (int part, bool held) noexcept;

    // CC 121 (Reset All Controllers) target for one part: bend back to centre,
    // sustain released. Does not touch the routing table.
    void resetControllers (int part) noexcept;

private:
    // Recompute channelDestMask from destChannel. Called on every routing
    // edit. Message thread only.
    void rebuildChannelMask() noexcept;

    // Source of truth: each destination's assigned MIDI channel (0 = off).
    std::array<std::atomic<std::int8_t>, kNumDestinations> destChannel {};

    // Forward lookup, indexed by MIDI channel 0..16 (slot 0 unused so lookups
    // need no -1). Bit `d` set = destination `d` receives from that channel.
    std::array<std::atomic<std::uint16_t>, 17>             channelDestMask {};

    std::array<double, PartManager::kNumParts>             bendSemitones {};
    std::array<bool,   PartManager::kNumParts>             sustainHeld {};
};
