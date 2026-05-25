#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// Placeholder editor for the v2 baseline. Task 04 replaces this with the
// WebView host once the v2 widget library is in place; until then we show
// the WebView fallback panel from 08-ui-views.md view 9 so the plugin always
// renders something predictable when opened.
class GenVstAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GenVstAudioProcessor& processor;
    juce::TextButton      retryButton { "Retry" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
