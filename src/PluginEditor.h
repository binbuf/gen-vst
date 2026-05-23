#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

// Hosts the Gen VST web UI: a juce::WebBrowserComponent filling the fixed
// 960x560 window, configured with native integration, the master_gain
// parameter relay, and — in release builds — a resource provider serving the
// embedded Vite bundle. Under GENVST_DEV_SERVER it loads the Vite dev server
// instead. See docs/design/05-ui-ux.md "C++ Integration Contract".
class GenVstAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::WebBrowserComponent::Options makeOptions();

    GenVstAudioProcessor& processor;

    // The "currently selected part" loaded patches target (ADR-0013). The JS
    // UI moves it via the `selectChannel` native function (Task 14 owns the
    // UI; we own the C++ state). Default to 0 (Part 1) — matches the Genny
    // layout's "CH 1" initial selection.
    int selectedPart = 0;

    // Declaration order is load-bearing: the relay registers as a WebView
    // lifetime listener (via withOptionsFrom), so it must outlive webView; the
    // attachment binds the relay to the apvts parameter, so it must be torn
    // down first. Hence relay -> webView -> attachment.
    juce::WebSliderRelay masterGainRelay { "master_gain" };
    juce::WebBrowserComponent webView;
    juce::WebSliderParameterAttachment masterGainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
