#pragma once

#include <array>

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
class PartManager
{
public:
    static constexpr int kNumParts = 6;

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

private:
    std::array<Part, kNumParts> parts;
};
