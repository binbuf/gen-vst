#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class GenVstAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
