#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace genvst::state
{
    // Root tag for the v2 state envelope (07-feature-spec.md *State
    // Persistence*). Contents:
    //   * Optional <patch mode="FM|SQ" path="…"/> entries — the active patch
    //     path per mode. Persisting both lets a flip back to the other mode
    //     restore that mode's last patch label without re-loading the file
    //     (the apvts is the source of truth for sound — the path is the
    //     UI label).
    //   * <customRoots><root path="…"/>…</customRoots> — user-registered
    //     browser roots (04-patch-system.md *Patch roots*).
    //   * The full apvts parameter tree (mode_select + FM + SQ + D + globals).
    //
    // The single-`<patch>` form in 07-feature-spec.md is extended here to a
    // per-mode pair so verification step 6 in mvp2/11 (flip mode then
    // re-open → both paths remembered) holds.
    inline constexpr const char* kRootTag = "GenVstState";

    // Pending-state-restore payload — parsed from XML at setStateInformation
    // time, drained at the first prepareToPlay after the JUCE wrapper has set
    // the wrapper type so the patch browser can initialise.
    struct PendingRestore
    {
        juce::String              activeFmPath;
        juce::String              activeSqPath;
        std::vector<juce::String> customRoots;

        // Embedded FM drum-kit JSON (ADR-0021 amendment). Non-empty when the
        // project was saved with a `.gnkit` active; the full kit (every pad's
        // patch) is embedded so the project is self-contained even if the
        // source `.gnkit` / `.tfi` files move. Empty = no kit active.
        juce::String              kitJson;
    };

    // Build the v2 state XML envelope from its constituent inputs. Keeps the
    // function purely data-in / data-out so tests can drive it without
    // standing up a full plugin processor.
    std::unique_ptr<juce::XmlElement> save (
        const juce::ValueTree&             apvtsState,
        const juce::String&                activeFmPath,
        const juce::String&                activeSqPath,
        const std::vector<juce::String>&   customRootPaths,
        const juce::String&                kitJson = {});

    // Parse `xml` into a pending restore + replace apvts state. Returns the
    // pending restore payload on success; std::nullopt when `xml` is not a
    // v2 envelope (legacy v1 byte streams are rejected and apvts is left
    // untouched — projects saved on v1 stay on v1).
    std::optional<PendingRestore> restore (
        juce::AudioProcessorValueTreeState& apvts,
        const juce::XmlElement&             xml);
}
