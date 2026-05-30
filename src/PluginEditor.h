#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

// The v2 editor hosts the Vite-built WebView UI (ADR-0001) in a fixed
// 1200x660 window (ADR-0023 + keyboard strip). It owns the parameter relays + attachments and
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
                                   public juce::FileDragAndDropTarget,
                                   private juce::Timer
{
public:
    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;

    // juce::FileDragAndDropTarget — Task 09 drag-and-drop entry point. The
    // editor accepts any file with a supported patch extension, a `.vgm` /
    // `.vgz` (Import Bank), or a folder (recursive import).
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;

    void tryInitWebView();
    void showFallbackPanel();

    juce::WebBrowserComponent::Options makeOptions();

    // Ableton Auto-Scale workaround — see syncToHostSize implementation.
    void syncToHostSize (const char* origin);

    // Fire the `patchLoaded` event into the WebView so the header LCD
    // updates. Called from the processor's patch-loaded callback.
    void emitPatchLoaded (const PatchLoadedNotifier& note);

    // Surface a notification toast in the UI. `level` is one of
    // "info" / "warn" / "error".
    void emitToast (const juce::String& level, const juce::String& message);

    // Native function helpers — push results back through the
    // NativeFunctionCompletion API.
    void doLoadPatch     (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doSavePatch     (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doImportPatch   (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doExportPatch   (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doAddPatchRoot  (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doDeletePatch   (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doPatchNav      (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doExpandFolder  (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doGetPatchList  (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doGetPatchRoots (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);
    void doGetActivePatchPath (const juce::Array<juce::var>& args,
                          juce::WebBrowserComponent::NativeFunctionCompletion completion);

    // Internal helper — DnD per the rules in Task 09 *Context* §
    // Drag-and-drop. Each file is dispatched by its extension; folders run
    // the recursive importer.
    void handleDroppedPaths (const std::vector<juce::String>& paths);

    // Pending file-chooser used by importPatch / exportPatch / addPatchRoot.
    // JUCE 8 requires the chooser to outlive its callback; keep it as a
    // member so a second prompt cancels the first cleanly.
    std::unique_ptr<juce::FileChooser> fileChooser;

    GenVstAudioProcessor& processor;

    // --- Per-apvts-parameter relays ----------------------------------------
    // Lifetime contract: relays register as WebView listeners via
    // withOptionsFrom in makeOptions(), so they must outlive webView;
    // attachments bridge a relay ↔ apvts parameter and must be torn down
    // first. Declaration order therefore is: relays, then webView, then
    // attachments.
    //
    // Populated in the editor constructor by iterating
    // processor.getParameters() and classifying each apvts parameter by
    // type (Bool → toggle, Choice → combo, anything else → slider). This
    // makes adding a new apvts parameter zero-effort on the editor side —
    // the JUCE-side relay registration and the JS-side
    // `bindSlider/bindToggle/bindCombo` connect automatically.
    //
    // The previous code declared named relays only for the header /
    // Settings / Gallery params (Task 08 + 04). The per-mode panel relays
    // (FM/SQ/D operator + part params) were planned for Tasks 05-07 but
    // never landed, so the JS-side `bindSlider("tl_op1")` ran against a
    // phantom SliderState — `setValueNotifyingHost` updates from a preset
    // load reached the audio thread but never echoed back to the UI.
    // Iterating apvts here closes that gap exhaustively.
    struct OwnedSliderRelay { juce::String id; std::unique_ptr<juce::WebSliderRelay>       relay; };
    struct OwnedToggleRelay { juce::String id; std::unique_ptr<juce::WebToggleButtonRelay> relay; };
    struct OwnedComboRelay  { juce::String id; std::unique_ptr<juce::WebComboBoxRelay>     relay; };
    std::vector<OwnedSliderRelay> sliderRelays;
    std::vector<OwnedToggleRelay> toggleRelays;
    std::vector<OwnedComboRelay>  comboRelays;

    // Owned WebView -- created by tryInitWebView(), reset on fallback.
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // --- Attachments --------------------------------------------------------
    // Built in tryInitWebView() once the WebView exists; one per relay (so
    // the indices stay 1:1 with the relay vectors). Reset together with
    // webView so the relay ↔ apvts wiring tears down cleanly in the Retry
    // flow.
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>>       sliderAtts;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAtts;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>>     comboAtts;

    // UI scale apvts parameter listener — resizes the editor host on change.
    // Held via a juce::ParameterAttachment so its lifetime tracks the editor.
    // Independent of the combo relay attachment built by the loop above; the
    // relay handles the UI ↔ apvts bridge, this drives the resize side-effect.
    std::unique_ptr<juce::ParameterAttachment> uiScaleListener;

    // Apply integer scale `n` (1, 2, or 3) to the WebView host bounds. The
    // base canvas is 1200x660 at 1x (keyboard visible) or 1200x560 (keyboard
    // hidden); 2x and 3x grow proportionally.
    void applyUiScale (int n);

    // Show or hide the keyboard strip. Resizes the editor window accordingly.
    void applyKeyboardVisible (bool visible);

    // Recompute and apply the window size from the cached scale + visibility.
    void applyWindowSize();

    // Cached state for the combined resize calculation.
    int  currentUiScale    = 1;
    bool keyboardVisible   = true;

    // ParameterAttachment for keyboard_visible (analogous to uiScaleListener).
    std::unique_ptr<juce::ParameterAttachment> keyboardVisibleListener;

    // --- Fallback panel (shown when WebView fails to initialise) -----------
    // Lives next to the WebView in the component tree; only one is ever
    // visible at a time.
    juce::Label       fallbackTitle   { {}, "Gen VST -- WebView unavailable" };
    juce::Label       fallbackMessage { {}, {} };
    juce::TextButton  retryButton     { "Retry" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
