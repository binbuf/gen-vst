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

    // Declaration order is load-bearing: each relay registers as a WebView
    // lifetime listener (via withOptionsFrom), so relays must outlive webView;
    // attachments bind a relay to its apvts parameter, so they must be torn
    // down first. Hence relays -> webView -> attachments.
    juce::WebSliderRelay masterGainRelay { "master_gain" };

   #if GENVST_DEV_SERVER
    // Widget-gallery scratch relays (Task 10). One per core widget kind, all
    // bound to the matching `gallery_*` parameters declared under the same
    // GENVST_DEV_SERVER guard in createParameterLayout. They exist only in
    // dev-server builds.
    juce::WebSliderRelay      galleryKnobRelay    { "gallery_knob"    };
    juce::WebSliderRelay      gallerySliderRelay  { "gallery_slider"  };
    juce::WebSliderRelay      galleryReadoutRelay { "gallery_readout" };
    juce::WebSliderRelay      galleryStepRelay    { "gallery_step"    };
    juce::WebToggleButtonRelay galleryToggleRelay { "gallery_toggle"  };
    juce::WebComboBoxRelay    gallerySectionRelay { "gallery_section" };
    juce::WebComboBoxRelay    galleryTabsRelay    { "gallery_tabs"    };
    juce::WebComboBoxRelay    galleryListRelay    { "gallery_list"    };
   #endif

    juce::WebBrowserComponent webView;
    juce::WebSliderParameterAttachment masterGainAttachment;

   #if GENVST_DEV_SERVER
    juce::WebSliderParameterAttachment       galleryKnobAttachment;
    juce::WebSliderParameterAttachment       gallerySliderAttachment;
    juce::WebSliderParameterAttachment       galleryReadoutAttachment;
    juce::WebSliderParameterAttachment       galleryStepAttachment;
    juce::WebToggleButtonParameterAttachment galleryToggleAttachment;
    juce::WebComboBoxParameterAttachment     gallerySectionAttachment;
    juce::WebComboBoxParameterAttachment     galleryTabsAttachment;
    juce::WebComboBoxParameterAttachment     galleryListAttachment;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
