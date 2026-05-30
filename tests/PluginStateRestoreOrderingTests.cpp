// Integration tests for DAW state-restore across the two host instantiation
// orderings — the regression guard for the "patch isn't restored on reopen"
// bug (Ableton). These drive a *real* GenVstAudioProcessor (compiled headless
// via GENVST_HEADLESS_TEST — createEditor returns nullptr and the WebView
// editor isn't linked), exercising the actual prepareToPlay cold-start,
// the mode_select AsyncUpdater pathway, and drainPendingStateRestore.
//
// The bug: a cold-start default-patch load (or the mode_select listener firing
// during replaceState) queued an AsyncUpdater that, if it ran *after*
// setStateInformation had restored the apvts but *before* the per-mode active
// paths were installed, reloaded the FACTORY DEFAULT on top of the restored
// values. The fix makes a state restore authoritative: setStateInformation
// cancels the pending async, re-anchors lastHandledMode, and drains
// immediately when the browser is live; handleAsyncUpdate bails while a
// restore is pending.
//
// To observe the clobber we need the message loop to actually dispatch the
// queued async (the clobber lives inside handleAsyncUpdate). ScopedJuceInitialiser_GUI
// + runDispatchLoopUntil are both juce_events facilities, so no GUI module is
// pulled in. resolveFactoryRoot() resolves to GENVST_DEV_PATCH_DIR
// (extern/patches) in this binary, giving the cold-start a real FM default
// (fm/bass/bass.tfi) to load — i.e. something to clobber with.

#include <gtest/gtest.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "PluginProcessor.h"

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr int    kBlockSize  = 512;

    float rawParam (GenVstAudioProcessor& p, const juce::String& id)
    {
        auto* v = p.getValueTreeState().getRawParameterValue (id);
        return v != nullptr ? v->load() : std::numeric_limits<float>::quiet_NaN();
    }

    void setParamDenormalised (GenVstAudioProcessor& p, const juce::String& id, float value)
    {
        if (auto* param = p.getValueTreeState().getParameter (id))
        {
            const auto& r = param->getNormalisableRange();
            param->beginChangeGesture();
            param->setValueNotifyingHost (r.convertTo0to1 (juce::jlimit (r.start, r.end, value)));
            param->endChangeGesture();
        }
    }
}

class StateRestoreOrdering : public ::testing::Test
{
protected:
    juce::ScopedJuceInitialiser_GUI juceInit;   // spins up a MessageManager

    // Drain the message queue so any queued AsyncUpdater (cold-start /
    // mode_select default-load) actually runs — that's where the clobber lived.
    static void pump (int ms = 300)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    // The FM `alg` value the factory default (bass.tfi) installs via the
    // cold-start. Computed once so saved states can pick a *different* value
    // and a clobber becomes observable.
    int defaultFmAlg()
    {
        GenVstAudioProcessor probe;
        probe.prepareToPlay (kSampleRate, kBlockSize);   // cold-start loads FM default
        pump();
        return juce::roundToInt (rawParam (probe, "alg"));
    }

    // A project-state blob for an FM instance whose `alg` and `master_volume`
    // are set to the given distinctive values (no patch path — the clobber
    // reproduces regardless, because the path is only applied during drain).
    juce::MemoryBlock makeSavedState (int savedAlg, float savedVolume)
    {
        GenVstAudioProcessor src;
        setParamDenormalised (src, "mode_select",   0.0f);   // FM
        setParamDenormalised (src, "alg",           (float) savedAlg);
        setParamDenormalised (src, "master_volume", savedVolume);

        juce::MemoryBlock mb;
        src.getStateInformation (mb);
        return mb;
    }
};

// Ordering 1 — setStateInformation BEFORE prepareToPlay (typical VST3 flow).
// The restored alg must survive; the cold-start default-load must not fire.
TEST_F (StateRestoreOrdering, RestoreSurvivesWhenStateSetBeforePrepare)
{
    const int defAlg   = defaultFmAlg();
    const int savedAlg = (defAlg + 4) % 8;
    ASSERT_NE (savedAlg, defAlg);

    auto blob = makeSavedState (savedAlg, 0.137f);

    GenVstAudioProcessor proc;
    proc.setStateInformation (blob.getData(), (int) blob.getSize());
    proc.prepareToPlay (kSampleRate, kBlockSize);
    pump();

    EXPECT_EQ (juce::roundToInt (rawParam (proc, "alg")), savedAlg);
    EXPECT_NEAR (rawParam (proc, "master_volume"), 0.137f, 1e-3f);
}

// Ordering 2 — prepareToPlay BEFORE setStateInformation (Ableton's flow).
// This is the case that previously clobbered the restored patch: the
// cold-start async from prepareToPlay is still queued when setStateInformation
// restores the apvts, then fires and reloads the factory default. The fix must
// keep the restored alg.
TEST_F (StateRestoreOrdering, RestoreSurvivesWhenPrepareBeforeState)
{
    const int defAlg   = defaultFmAlg();
    const int savedAlg = (defAlg + 4) % 8;
    ASSERT_NE (savedAlg, defAlg);

    auto blob = makeSavedState (savedAlg, 0.137f);

    GenVstAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlockSize);   // cold-start QUEUES a default-load async
    // NOTE: deliberately do not pump here — the async stays pending across the
    // restore, exactly the race the fix addresses.
    proc.setStateInformation (blob.getData(), (int) blob.getSize());
    pump();   // let any surviving async run; it must NOT clobber the restore

    EXPECT_EQ (juce::roundToInt (rawParam (proc, "alg")), savedAlg);
    EXPECT_NEAR (rawParam (proc, "master_volume"), 0.137f, 1e-3f);
}

// A second prepareToPlay (host starting playback after load) must not re-run
// the cold-start default-load over the restored state.
TEST_F (StateRestoreOrdering, RestoreSurvivesAcrossSecondPrepare)
{
    const int defAlg   = defaultFmAlg();
    const int savedAlg = (defAlg + 4) % 8;

    auto blob = makeSavedState (savedAlg, 0.42f);

    GenVstAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlockSize);
    proc.setStateInformation (blob.getData(), (int) blob.getSize());
    pump();
    proc.prepareToPlay (kSampleRate, kBlockSize);   // simulate playback start
    pump();

    EXPECT_EQ (juce::roundToInt (rawParam (proc, "alg")), savedAlg);
    EXPECT_NEAR (rawParam (proc, "master_volume"), 0.42f, 1e-3f);
}
