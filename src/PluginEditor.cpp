#include "PluginEditor.h"

GenVstAudioProcessorEditor::GenVstAudioProcessorEditor (GenVstAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    // ADR-0023 — fixed 1200×560 editor.
    setSize (1200, 560);

    addAndMakeVisible (retryButton);
    // Retry is a no-op until Task 04 wires the WebView back in.
    retryButton.onClick = [] {};
}

GenVstAudioProcessorEditor::~GenVstAudioProcessorEditor() = default;

void GenVstAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111315));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("Gen VST — UI under construction",
                getLocalBounds().withTrimmedBottom (96),
                juce::Justification::centred);
}

void GenVstAudioProcessorEditor::resized()
{
    const int btnW = 120;
    const int btnH = 32;
    retryButton.setBounds ((getWidth() - btnW) / 2,
                           (getHeight() / 2) + 24,
                           btnW, btnH);
}
