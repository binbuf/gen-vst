#include "PluginEditor.h"

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& processor)
    : juce::AudioProcessorEditor (processor)
{
    setOpaque (true);
    setSize (960, 560);   // fixed window — ADR-0007
}

void GenVstAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    g.setColour (juce::Colour (0xfff5c842));
    g.setFont (48.0f);
    g.drawText ("GEN VST", getLocalBounds(), juce::Justification::centred);
}

void GenVstAudioProcessorEditor::resized()
{
}
