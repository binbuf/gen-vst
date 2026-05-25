#pragma once

#include <memory>

#include <juce_core/juce_core.h>

class GenVstAudioProcessor;

namespace genvst::state
{
    // Root tag for the v2 state envelope. The v1 fields (parts, PSG MIDI
    // bindings, embedded DAC PCM, .gnbank refs) are gone — `save` writes the
    // apvts XML wrapped in <GenVstState/> and nothing else; Task 10 reintroduces
    // active-patch-path + custom-root persistence.
    inline constexpr const char* kRootTag = "GenVstState";

    // Build the state XML for `proc`: a <GenVstState/> wrapper containing only
    // the apvts.copyState() XML.
    std::unique_ptr<juce::XmlElement> save (GenVstAudioProcessor& proc);

    // Restore apvts from `xml`. Accepts both the v2 <GenVstState/> wrapper and
    // a bare apvts root (the v1 / pre-Task-10 format) so a project saved on a
    // mismatched build still loads its parameters cleanly.
    void restore (GenVstAudioProcessor& proc, const juce::XmlElement& xml);
}
