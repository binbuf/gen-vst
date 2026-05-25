// Tests for the Task 11 v2 state-envelope: round-trips through the
// <GenVstState/> wrapper, per-mode <patch> elements, the <customRoots>
// block, the apvts subtree, and the legacy-rejection path. Plus the
// path-validation logic the processor's drainPendingStateRestore relies
// on (existsAsFile / isDirectory).
//
// The save/restore primitives in PluginState.cpp are purely data-in /
// data-out by design so this file can exercise them without standing up a
// full GenVstAudioProcessor (which would pull the editor + WebView + JUCE
// gui modules into the test binary).

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "PluginState.h"

namespace fs = std::filesystem;

namespace
{
    // A minimal apvts parameter layout — only the handful of params the
    // round-trip tests exercise. The save/restore code is schema-agnostic
    // (it just shuttles ValueTree<->XML), so a tiny layout is sufficient.
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "mode_select", 1 }, "Mode",
            juce::StringArray { "FM", "SQ", "D" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "master_volume", 1 }, "Master Volume",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "prescaler", 1 }, "Prescaler",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "mono", 1 }, "Mono", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "dry_wet", 1 }, "Dry/Wet",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        return layout;
    }

    // A pretend AudioProcessor — apvts requires one. Defaults everywhere; no
    // audio is ever processed.
    class StubProcessor : public juce::AudioProcessor
    {
    public:
        StubProcessor()
            : apvts (*this, nullptr, "PARAMETERS", makeLayout()) {}

        juce::AudioProcessorValueTreeState apvts;

        const juce::String getName() const override                    { return "Stub"; }
        void prepareToPlay (double, int) override                      {}
        void releaseResources() override                               {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override            { return nullptr; }
        bool hasEditor() const override                                { return false; }
        bool acceptsMidi() const override                              { return false; }
        bool producesMidi() const override                             { return false; }
        double getTailLengthSeconds() const override                   { return 0.0; }
        int getNumPrograms() override                                  { return 1; }
        int getCurrentProgram() override                               { return 0; }
        void setCurrentProgram (int) override                          {}
        const juce::String getProgramName (int) override               { return {}; }
        void changeProgramName (int, const juce::String&) override     {}
        void getStateInformation (juce::MemoryBlock&) override         {}
        void setStateInformation (const void*, int) override           {}
    };

    void setParam (juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& id, float normalisedValue)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const auto& r = p->getNormalisableRange();
            const float n = r.convertTo0to1 (juce::jlimit (r.start, r.end, normalisedValue));
            p->beginChangeGesture();
            p->setValueNotifyingHost (n);
            p->endChangeGesture();
        }
    }

    float rawValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        if (auto* p = apvts.getRawParameterValue (id))
            return p->load();
        return 0.0f;
    }
}

// =============================================================================
// save() — envelope shape
// =============================================================================

// Bare save (no patches, no custom roots) produces a <GenVstState/> envelope
// whose only child is the apvts subtree. No `<patch>` elements; an empty
// `<customRoots/>` is still present so the schema is stable.
TEST (PluginState, SaveProducesV2EnvelopeWithApvtsAndEmptyCustomRoots)
{
    StubProcessor proc;
    auto xml = genvst::state::save (proc.apvts.copyState(), {}, {}, {});
    ASSERT_NE (xml, nullptr);
    EXPECT_EQ (xml->getTagName(), juce::String (genvst::state::kRootTag));

    // No `<patch>` children when both per-mode paths are empty.
    EXPECT_EQ (xml->getChildByName ("patch"), nullptr);

    // `<customRoots>` is always emitted, even when empty, so the parser
    // sees a stable shape.
    auto* roots = xml->getChildByName ("customRoots");
    ASSERT_NE (roots, nullptr);
    EXPECT_EQ (roots->getNumChildElements(), 0);

    // The apvts subtree rides as a child.
    auto* apvtsChild = xml->getChildByName (proc.apvts.state.getType());
    ASSERT_NE (apvtsChild, nullptr);
}

// Save with per-mode patch paths emits one `<patch mode="…" path="…"/>` per
// non-empty path, in FM-then-SQ order.
TEST (PluginState, SaveEmitsPerModePatchElements)
{
    StubProcessor proc;
    auto xml = genvst::state::save (proc.apvts.copyState(),
                                    "C:/factory/bass.tfi",
                                    "C:/factory/strings.psg",
                                    {});
    ASSERT_NE (xml, nullptr);

    juce::Array<juce::XmlElement*> patches;
    for (auto* c : xml->getChildIterator())
        if (c->hasTagName ("patch")) patches.add (c);

    ASSERT_EQ (patches.size(), 2);
    EXPECT_EQ (patches[0]->getStringAttribute ("mode"), juce::String ("FM"));
    EXPECT_EQ (patches[0]->getStringAttribute ("path"), juce::String ("C:/factory/bass.tfi"));
    EXPECT_EQ (patches[1]->getStringAttribute ("mode"), juce::String ("SQ"));
    EXPECT_EQ (patches[1]->getStringAttribute ("path"), juce::String ("C:/factory/strings.psg"));
}

// =============================================================================
// restore() — happy-path round trips
// =============================================================================

// Round-trip of apvts: change a few params, save state, replace state with
// defaults, restore — every changed param comes back exactly.
TEST (PluginState, RoundTripsAllApvtsParameters)
{
    StubProcessor proc;

    // Set some non-default values.
    setParam (proc.apvts, "mode_select",   2.0f);    // D
    setParam (proc.apvts, "master_volume", 0.42f);
    setParam (proc.apvts, "prescaler",     0.73f);
    setParam (proc.apvts, "mono",          1.0f);
    setParam (proc.apvts, "dry_wet",       0.31f);

    auto xml = genvst::state::save (proc.apvts.copyState(), {}, {}, {});
    ASSERT_NE (xml, nullptr);

    // Reset apvts to defaults — replaceState with a defaultLayout tree.
    StubProcessor freshProc;
    EXPECT_NEAR (rawValue (freshProc.apvts, "master_volume"), 0.8f, 1e-5f);

    auto pending = genvst::state::restore (freshProc.apvts, *xml);
    ASSERT_TRUE (pending.has_value());

    EXPECT_NEAR (rawValue (freshProc.apvts, "mode_select"),   2.0f,  1e-3f);
    EXPECT_NEAR (rawValue (freshProc.apvts, "master_volume"), 0.42f, 1e-5f);
    EXPECT_NEAR (rawValue (freshProc.apvts, "prescaler"),     0.73f, 1e-5f);
    EXPECT_NEAR (rawValue (freshProc.apvts, "mono"),          1.0f,  1e-5f);
    EXPECT_NEAR (rawValue (freshProc.apvts, "dry_wet"),       0.31f, 1e-5f);

    EXPECT_TRUE  (pending->activeFmPath.isEmpty());
    EXPECT_TRUE  (pending->activeSqPath.isEmpty());
    EXPECT_TRUE  (pending->customRoots.empty());
}

// Round-trip with one `<patch>` element (FM mode, no SQ) — restore returns
// the path in PendingRestore.activeFmPath and leaves activeSqPath empty.
TEST (PluginState, RoundTripsSingleFmPatchPath)
{
    StubProcessor proc;
    auto xml = genvst::state::save (proc.apvts.copyState(),
                                    "D:/patches/lead.tfi", {}, {});
    ASSERT_NE (xml, nullptr);

    StubProcessor freshProc;
    auto pending = genvst::state::restore (freshProc.apvts, *xml);

    ASSERT_TRUE (pending.has_value());
    EXPECT_EQ (pending->activeFmPath, juce::String ("D:/patches/lead.tfi"));
    EXPECT_TRUE (pending->activeSqPath.isEmpty());
}

// Round-trip with both per-mode paths populated.
TEST (PluginState, RoundTripsBothPerModePatchPaths)
{
    StubProcessor proc;
    auto xml = genvst::state::save (proc.apvts.copyState(),
                                    "D:/patches/lead.tfi",
                                    "D:/patches/pad.psg",
                                    {});
    StubProcessor freshProc;
    auto pending = genvst::state::restore (freshProc.apvts, *xml);

    ASSERT_TRUE (pending.has_value());
    EXPECT_EQ (pending->activeFmPath, juce::String ("D:/patches/lead.tfi"));
    EXPECT_EQ (pending->activeSqPath, juce::String ("D:/patches/pad.psg"));
}

// Round-trip with two `<customRoots>` entries — both paths come back in
// the same order.
TEST (PluginState, RoundTripsCustomRootsList)
{
    StubProcessor proc;
    std::vector<juce::String> roots {
        juce::String ("D:/my-presets/genny"),
        juce::String ("E:/share/community-bank")
    };
    auto xml = genvst::state::save (proc.apvts.copyState(), {}, {}, roots);

    StubProcessor freshProc;
    auto pending = genvst::state::restore (freshProc.apvts, *xml);

    ASSERT_TRUE (pending.has_value());
    ASSERT_EQ (pending->customRoots.size(), 2u);
    EXPECT_EQ (pending->customRoots[0], roots[0]);
    EXPECT_EQ (pending->customRoots[1], roots[1]);
}

// An unresolvable patch path round-trips through save/restore — the
// PendingRestore payload carries it forward to the drain step, which is
// what surfaces the "Patch could not be loaded" toast. The drain's
// existence check is JUCE's juce::File API (verified below).
TEST (PluginState, UnresolvablePatchPathRoundTripsForDrainStep)
{
    StubProcessor proc;
    const juce::String missingPath ("D:/never/created/missing.tfi");
    auto xml = genvst::state::save (proc.apvts.copyState(), missingPath, {}, {});

    StubProcessor freshProc;
    auto pending = genvst::state::restore (freshProc.apvts, *xml);

    ASSERT_TRUE (pending.has_value());
    EXPECT_EQ (pending->activeFmPath, missingPath);
    EXPECT_FALSE (juce::File (missingPath).existsAsFile());
}

// Same as above for a custom-root path that doesn't resolve as a directory.
TEST (PluginState, UnresolvableCustomRootRoundTripsForDrainStep)
{
    StubProcessor proc;
    std::vector<juce::String> roots { juce::String ("D:/never/created/folder") };
    auto xml = genvst::state::save (proc.apvts.copyState(), {}, {}, roots);

    StubProcessor freshProc;
    auto pending = genvst::state::restore (freshProc.apvts, *xml);

    ASSERT_TRUE (pending.has_value());
    ASSERT_EQ (pending->customRoots.size(), 1u);
    EXPECT_FALSE (juce::File (pending->customRoots.front()).isDirectory());
}

// =============================================================================
// Legacy / unknown byte streams
// =============================================================================

// A byte stream whose root element is NOT <GenVstState/> is treated as a
// legacy / unknown format and rejected — restore returns std::nullopt and
// the apvts state is left untouched (the user's running plugin sound
// survives a project load from an incompatible version).
TEST (PluginState, LegacyByteStreamIsRejected)
{
    StubProcessor freshProc;

    // First record the apvts default for comparison.
    const float defaultVolume = rawValue (freshProc.apvts, "master_volume");

    // Hand-build a v1-shaped XML — the historical envelope had <parts>,
    // <psg>, <dac> children at the root, no `<GenVstState>` wrapper.
    juce::XmlElement legacy ("GenVstLegacy");
    legacy.createNewChildElement ("parts");
    legacy.createNewChildElement ("psg");
    legacy.createNewChildElement ("dac");

    auto pending = genvst::state::restore (freshProc.apvts, legacy);

    EXPECT_FALSE (pending.has_value());
    EXPECT_FLOAT_EQ (rawValue (freshProc.apvts, "master_volume"), defaultVolume);
}

// A bare apvts subtree (the pre-Task-11 wire format, before the envelope
// was added) is also rejected — same legacy contract.
TEST (PluginState, BareApvtsSubtreeIsRejected)
{
    StubProcessor proc;

    // Bare apvts as XML — no <GenVstState/> wrapper.
    auto bare = proc.apvts.copyState().createXml();
    ASSERT_NE (bare, nullptr);

    StubProcessor freshProc;
    const float defaultVolume = rawValue (freshProc.apvts, "master_volume");

    auto pending = genvst::state::restore (freshProc.apvts, *bare);

    EXPECT_FALSE (pending.has_value());
    EXPECT_FLOAT_EQ (rawValue (freshProc.apvts, "master_volume"), defaultVolume);
}

// A v2 envelope without the apvts child still parses, but apvts is not
// replaced. The PendingRestore is returned so any patch / custom-root
// payload can still be applied.
TEST (PluginState, EnvelopeMissingApvtsChildLeavesApvtsAlone)
{
    juce::XmlElement env (genvst::state::kRootTag);
    auto* patch = env.createNewChildElement ("patch");
    patch->setAttribute ("mode", "FM");
    patch->setAttribute ("path", "D:/patches/lead.tfi");

    StubProcessor freshProc;
    const float defaultVolume = rawValue (freshProc.apvts, "master_volume");
    auto pending = genvst::state::restore (freshProc.apvts, env);

    ASSERT_TRUE (pending.has_value());
    EXPECT_EQ (pending->activeFmPath, juce::String ("D:/patches/lead.tfi"));
    EXPECT_FLOAT_EQ (rawValue (freshProc.apvts, "master_volume"), defaultVolume);
}

// =============================================================================
// Path resolution — the drain step's view of the filesystem
// =============================================================================

// A real file on disk is reported as existsAsFile() — the drain's
// "patch path resolved" branch.
TEST (PluginState, RealFileResolvesViaJuceFile)
{
    const auto tmp = fs::temp_directory_path() / "genvst_state_real_patch.tmp";
    {
        std::ofstream out (tmp);
        out << "placeholder";
    }
    EXPECT_TRUE (juce::File (juce::String (tmp.string())).existsAsFile());
    fs::remove (tmp);
}

// A real directory on disk is reported as isDirectory() — the drain's
// "custom root resolved" branch.
TEST (PluginState, RealDirectoryResolvesViaJuceFile)
{
    const auto tmp = fs::temp_directory_path() / "genvst_state_real_root";
    fs::create_directories (tmp);
    EXPECT_TRUE (juce::File (juce::String (tmp.string())).isDirectory());
    fs::remove (tmp);
}
