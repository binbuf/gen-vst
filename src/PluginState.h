#pragma once

#include <memory>

#include <juce_core/juce_core.h>

class GenVstAudioProcessor;

namespace genvst::state
{
    // The XML wrapper tag enclosing the apvts state plus all custom non-apvts
    // fields (07-feature-spec.md "State Persistence"). Stored as a string so
    // tests can compare without re-declaring the constant.
    inline constexpr const char* kRootTag = "GenVstState";

    // Build the full state XML for `proc`: the apvts.copyState() XML as a
    // child plus the custom fields below.
    //
    //   <GenVstState schemaVersion="1">
    //     <PARAMETERS .../>                                  <-- apvts
    //     <parts>
    //       <part index="0" midiChannel="1" patchPath="..."/>...
    //     </parts>
    //     <psg ch0="11" ch1="12" ch2="13" noise="14"/>
    //     <dac midiChannel="16" rate="22050" name="kick.wav" pcm="base64..."/>
    //     <customRoots>
    //       <root path="..."/>
    //     </customRoots>
    //   </GenVstState>
    std::unique_ptr<juce::XmlElement> save (GenVstAudioProcessor& proc);

    // Parse `xml` and restore everything onto `proc`. Accepts both the new
    // GenVstState-wrapped format and a bare apvts root (the pre-Task-16
    // format) — the latter restores apvts only and leaves the routing /
    // custom roots / DAC PCM at their defaults.
    //
    // Order (matches 16-state-persistence.md "Implementation steps"):
    //   1. apvts.replaceState
    //   2. routing table re-bind
    //   3. patch reloads via PatchBrowser::loadIntoPart (deferred to
    //      prepareToPlay if the browser is not yet initialised)
    //   4. DAC PCM restore
    //   5. custom-root re-register (deferred together with the patch reloads
    //      if the browser is not yet initialised)
    //
    // Unresolved patch paths and unresolved custom roots are reported via
    // GenVstAudioProcessor::addPendingNotification — the editor drains the
    // queue on its next timer tick and emits each as a "notify" toast.
    void restore (GenVstAudioProcessor& proc, const juce::XmlElement& xml);

    // Helper exposed for use from prepareToPlay after the patch browser is
    // initialised — replays the deferred custom-root + patch-reload steps the
    // parent setStateInformation queued (no-op if nothing pending).
    void applyPendingPatchAndRootRestore (GenVstAudioProcessor& proc);
}
