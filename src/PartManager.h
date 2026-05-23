#pragma once

#include <array>
#include <atomic>
#include <optional>

#include <juce_events/juce_events.h>

#include "PatchSystem.h"

// One FM part: its patch plus the MIDI channel it answers to.
struct Part
{
    Patch patch;
    int   midiChannel = 1;   // 1-16
};

// The six FM parts of the multitimbral instrument (ADR-0013). Each part owns a
// patch and an assigned MIDI channel; the default binding is part i -> MIDI
// channel i + 1 (parts 0-5 -> channels 1-6). Voices are drawn from the shared
// pool in VoiceAllocator and are not owned here.
//
// Task 22 — Rack model: PartManager also tracks per-rack-slot "active" state
// for the user-curated instrument rack (08-ui-views.md view 1 revised). Active
// flags are written by the rack UI when a row is added (+) or removed (-);
// they are independent of the audio path and do not change voice allocation.
// The rack pool spans:
//   FM slots 0..4  (FM parts 0..4; part 5 stays reserved per ADR-0014)
//   SQ slots 0..3  (PSG tones 0..2 plus PSG noise as slot 3)
//   D  slot         (the dedicated DAC instance)
class PartManager : public juce::ChangeBroadcaster
{
public:
    static constexpr int kNumParts = 6;

    // Rack slot pool sizes (Task 22). FM is capped at 5 slots because part 5
    // (1-indexed channel 6) is reserved per ADR-0014 for the DAC slot; SQ
    // packs all four PSG slots (3 tones + 1 noise); D is the lone DAC slot.
    static constexpr int kNumRackFmSlots = 5;
    static constexpr int kNumRackSqSlots = 4;   // tones 0..2 + noise as slot 3
    static constexpr int kNumRackDSlots  = 1;
    static constexpr int kPsgNoiseSqSlot = 3;   // the noise slot's index inside the SQ pool

    enum class InstrumentType { FM, SQ, D };

    // A single rack slot identifier — the slot type plus its index within the
    // type's pool. (FM, 0) means "FM part 0"; (SQ, 0..2) means a PSG tone;
    // (SQ, 3) means the PSG noise channel; (D, 0) means the DAC.
    struct SlotId
    {
        InstrumentType type;
        int            index = 0;

        bool operator== (const SlotId& other) const noexcept
        {
            return type == other.type && index == other.index;
        }
    };

    PartManager();

    // Store `patch` as part `part`'s active patch. The audio thread reads FM
    // parameters from the apvts, not from here, so the caller also pushes the
    // patch into the parameter tree (see GenVstAudioProcessor).
    void loadPatch (int part, const Patch& patch);

    const Patch& getPatch (int part) const;

    int midiChannelForPart (int part) const;

    // The part bound to a 1-16 MIDI channel, or -1 if no part answers to it
    // (channels 7-16 stay unbound until PSG/DAC routing arrives).
    int partForMidiChannel (int channel) const;

    // --- Rack slot active state (Task 22) -------------------------------------
    //
    // The rack UI calls setSlotActive() when a row is added (+) or removed (-).
    // The widgets that watch ChangeBroadcaster repaint the rack list when the
    // active set changes.

    bool isSlotActive (SlotId slot) const noexcept;
    void setSlotActive (SlotId slot, bool active);

    // Find the first free slot of the requested type, or nullopt if the pool
    // is full. Slots are scanned in ascending index order.
    std::optional<SlotId> getFreeSlot (InstrumentType type) const noexcept;

    // Number of slots configured for a given instrument type (5 / 4 / 1).
    static int slotPoolSize (InstrumentType type) noexcept;

private:
    std::array<Part, kNumParts> parts;

    // Active-slot bitmaps per pool. Written from the message thread (rack
    // add/remove + state restore); read on the audio thread by the MIDI
    // dispatch to silence a removed slot's destination. Atomic so the data-
    // race is well-defined; relaxed memory order is sufficient because the
    // sound producers (Voice / SN76489Engine / DACPlayer) tolerate either
    // value on a transition block — the next block reads the updated value.
    std::array<std::atomic<bool>, kNumRackFmSlots> fmActive {};
    std::array<std::atomic<bool>, kNumRackSqSlots> sqActive {};
    std::array<std::atomic<bool>, kNumRackDSlots>  dActive  {};
};
