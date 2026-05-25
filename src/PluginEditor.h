#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

// The v2 editor hosts the Vite-built WebView UI (ADR-0001) in a fixed
// 1200x560 window (ADR-0023). It owns the parameter relays + attachments and
// pushes telemetry via a juce::Timer at ~30 Hz (05-ui-ux.md *C++ -> JS
// telemetry push*).
//
// **Fallback panel.** If the WebView fails to initialise (e.g. WebView2
// runtime missing on Windows -- the construction throws or the page reports a
// network error before mount), the editor swaps to a static native panel with
// a Retry button (08-ui-views.md view 9). Retry calls tryInitWebView() again.
//
// Task 04 only registers the gallery scratch relays + the global tooltip
// toggle; the per-mode panel relays (FM/SQ/D) come in Tasks 05-07 and are
// folded into makeOptions() alongside these.
class GenVstAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void tryInitWebView();
    void showFallbackPanel();

    juce::WebBrowserComponent::Options makeOptions();

    GenVstAudioProcessor& processor;

    // --- Relays (bound once, never rebound in Task 04) -----------------------
    // Lifetime contract: relays register as WebView listeners via
    // withOptionsFrom, so they must outlive webView; attachments bridge a
    // relay <-> apvts parameter and must be torn down first. Declaration order
    // therefore is: relays, then webView, then attachments.

    juce::WebToggleButtonRelay tooltipsEnabledRelay { "tooltips_enabled" };

    // Gallery scratch relays (Task 04). One per widget kind in
    // ui/src/widgets/. Bound to gallery_* params declared in
    // createParameterLayout. Always present (storage cost trivial); the
    // gallery page is only included in dev-server builds via Vite's
    // multi-page entry but the relays don't care which page is loaded.
    juce::WebSliderRelay        galleryKnobA   { "gallery_knob_a" };
    juce::WebSliderRelay        galleryKnobB   { "gallery_knob_b" };
    juce::WebSliderRelay        galleryKnobC   { "gallery_knob_c" };
    juce::WebSliderRelay        galleryKnobD   { "gallery_knob_d" };
    juce::WebToggleButtonRelay  galleryToggleA { "gallery_toggle_a" };
    juce::WebToggleButtonRelay  galleryToggleB { "gallery_toggle_b" };
    juce::WebToggleButtonRelay  galleryToggleC { "gallery_toggle_c" };
    juce::WebToggleButtonRelay  galleryToggleD { "gallery_toggle_d" };
    juce::WebComboBoxRelay      galleryComboA  { "gallery_combo_a" };
    juce::WebSliderRelay        galleryAlgo    { "gallery_algo" };
    juce::WebSliderRelay        galleryStepper { "gallery_stepper" };
    juce::WebSliderRelay        galleryLevel   { "gallery_level" };
    juce::WebToggleButtonRelay  galleryNoteOn  { "gallery_noteon" };
    juce::WebSliderRelay        galleryWheel   { "gallery_wheel" };

    // Owned WebView -- created by tryInitWebView(), reset on fallback.
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // Attachments -- created alongside webView; reset together with it so the
    // relay <-> apvts wiring tears down cleanly.
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> tooltipsAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryKnobAAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryKnobBAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryKnobCAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryKnobDAtt;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> galleryToggleAAtt;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> galleryToggleBAtt;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> galleryToggleCAtt;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> galleryToggleDAtt;
    std::unique_ptr<juce::WebComboBoxParameterAttachment>     galleryComboAAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryAlgoAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryStepperAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryLevelAtt;
    std::unique_ptr<juce::WebToggleButtonParameterAttachment> galleryNoteOnAtt;
    std::unique_ptr<juce::WebSliderParameterAttachment>       galleryWheelAtt;

    // --- Fallback panel (shown when WebView fails to initialise) -----------
    // Lives next to the WebView in the component tree; only one is ever
    // visible at a time.
    juce::Label       fallbackTitle   { {}, "Gen VST -- WebView unavailable" };
    juce::Label       fallbackMessage { {}, {} };
    juce::TextButton  retryButton     { "Retry" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
