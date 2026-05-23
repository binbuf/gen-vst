#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "PartManager.h"
#include "PluginProcessor.h"
#include "SN76489Engine.h"
#include "Telemetry.h"

// Hosts the Gen VST web UI: a juce::WebBrowserComponent filling the fixed
// 960x560 window, configured with native integration, every FM/global parameter
// relay, and — in release builds — a resource provider serving the embedded
// Vite bundle. Under GENVST_DEV_SERVER it loads the Vite dev server instead.
// See docs/design/05-ui-ux.md "C++ Integration Contract".
//
// FM channel paging (05-ui-ux.md "FM channel paging"): the FM relays are named
// **without** the `_part<n>` suffix (e.g. `atk_op1`). selectChannel(n) tears
// down every FM attachment and rebuilds it against part `n`'s parameter, so
// every FM widget repaints to the new part's values in one batch. Global, PSG
// and DAC relays bind once at construction and never rebind.
//
// Native file drop (08-ui-views.md view 11 / 05-ui-ux.md "File drag-and-drop"):
// the editor implements juce::FileDragAndDropTarget directly — an HTML5 drop
// inside the WebView only yields File objects, not real paths, and cannot
// enumerate a dropped folder. Dropped patch files import into the user-imported
// root; dropped directories register as custom roots.
class GenVstAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::FileDragAndDropTarget,
                                   private juce::Timer
{
public:
    static constexpr int kNumOps        = 4;
    static constexpr int kNumOpParams   = 11;   // per-operator FM params
    static constexpr int kNumPartParams = 7;    // per-part FM params

    // Resolution of the scope payload pushed to the JS oscilloscope. ~768
    // points is what the design (05-ui-ux.md / 08-ui-views.md) calls for; the
    // payload is built once per timer tick and packed into the "meterData"
    // event's `scope` array.
    static constexpr int kScopeOutPoints = 768;

    // Source window into the telemetry ring: how many of the most recent
    // host-rate samples we read before downsampling to kScopeOutPoints.
    // 2048 samples ~ 43 ms at 48 kHz — enough history for low-frequency
    // periodicity to be visible.
    static constexpr int kScopeReadSamples = 2048;
    static_assert (kScopeReadSamples <= Telemetry::kScopeBufferSize,
                   "scope read window must fit in the telemetry ring");

    explicit GenVstAudioProcessorEditor (GenVstAudioProcessor&);
    ~GenVstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // juce::FileDragAndDropTarget — accepts directories (registered as a
    // custom root) and any .tfi/.vgi/.dmp file (copied into the user-imported
    // root). Other files are rejected, so dropping a random WAV onto the
    // plugin window does nothing. The patch-browser modal's tree refreshes
    // via the patchRootsChanged event emitted afterwards.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

private:
    juce::WebBrowserComponent::Options makeOptions();

    // juce::Timer — runs at ~30 Hz on the message thread, builds the
    // "meterData" event payload from the processor's telemetry, and emits it
    // via emitEventIfBrowserIsVisible (08-ui-views.md "Header meter bay"). The
    // timer is started in the editor's constructor and stopped on destruction
    // so it never outlives the WebView and never runs when no editor is open.
    void timerCallback() override;

    // Rebuild every FM attachment so each FM relay re-binds to part `n`'s
    // apvts parameter. Pushes the new values into the relays as a side effect,
    // which fires valueChangedEvent on the JS side -> every FM widget repaints.
    void rebuildFmAttachments (int part);

    // Build relays for the 4 x 11 per-operator FM params with names stripped
    // of the `_part<n>` suffix ("dt_op1", "mul_op1", ...). The unique_ptr
    // indirection is mandated by the relays themselves: WebSliderRelay is
    // marked JUCE_DECLARE_NON_MOVEABLE, so std::vector<WebSliderRelay> cannot
    // exist; the pointers keep the relay objects pinned on the heap so the
    // `withOptionsFrom` references handed to the WebBrowserComponent stay
    // valid for the editor's lifetime.
    static std::vector<std::unique_ptr<juce::WebSliderRelay>> makeOpRelays();

    // Build relays for the 7 per-part FM params ("alg", "fb", "ams", "pms",
    // "lr", "lfo_enable", "lfo_rate") with the same stripped-name convention.
    static std::vector<std::unique_ptr<juce::WebSliderRelay>> makePartRelays();

    // Build the per-PSG-channel relays (one per SN76489 channel for each of
    // psg_vol_*, psg_pan_*, psg_bend_*). Each must be heap-pinned for the
    // same NON_MOVEABLE reason as the FM relays.
    static std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> makePsgVolRelays();
    static std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> makePsgPanRelays();
    static std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels> makePsgBendRelays();

    // Emit a notify event ({level, message}) into the JS toast pipeline
    // (05-ui-ux.md "C++ -> JS notifications"). Safe to call from the message
    // thread.
    void emitNotify (const juce::String& level, const juce::String& message);

    GenVstAudioProcessor& processor;

    // The currently edited FM part. The JS UI moves it via selectChannel; the
    // C++ editor uses it to target patch-load operations and to rebuild the
    // FM attachments.
    int selectedPart = 0;

    // --- Global relays (bound once, never rebind) ----------------------------
    // Declaration order is load-bearing: each relay registers as a WebView
    // lifetime listener (via withOptionsFrom), so relays must outlive webView;
    // attachments bind a relay to its apvts parameter, so they must be torn
    // down first. Hence relays -> webView -> attachments.
    juce::WebSliderRelay masterGainRelay { "master_gain" };

    // FM-part-scoped relays — stripped names, rebound on selectChannel.
    // Per-op relays indexed [op * kNumOpParams + paramIndex]; param order
    // matches kFmOpParamIds in the .cpp. Per-part relays indexed by
    // kFmPartParamIds order.
    std::vector<std::unique_ptr<juce::WebSliderRelay>> opRelays   { makeOpRelays() };
    std::vector<std::unique_ptr<juce::WebSliderRelay>> partRelays { makePartRelays() };

    // View 10 — per-part polyphony controls. Single relay per control, rebound
    // to the selected part's apvts parameter on selectChannel (same paging
    // contract as the rest of the FM-part relays — 05-ui-ux.md).
    juce::WebComboBoxRelay polyModeRelay      { "poly_mode" };
    juce::WebComboBoxRelay monoGlideRelay     { "mono_glide" };
    juce::WebSliderRelay   unisonSpreadRelay  { "unison_spread" };

    // Task 13 — global PSG / DAC / Settings relays.
    juce::WebSliderRelay        psgMixRelay         { "psg_mix" };
    juce::WebToggleButtonRelay  psgLayerRelay       { "psg_layer" };
    juce::WebComboBoxRelay      psgNoiseTypeRelay   { "psg_noise_type" };
    juce::WebComboBoxRelay      psgNoiseRateRelay   { "psg_noise_rate" };
    juce::WebToggleButtonRelay  psgNoiseAutoRelay   { "psg_noise_auto" };

    juce::WebToggleButtonRelay  dacEnableRelay      { "dac_enable" };
    juce::WebComboBoxRelay      dacRateRelay        { "dac_rate" };
    juce::WebComboBoxRelay      dacModeRelay        { "dac_mode" };
    juce::WebSliderRelay        dacLevelRelay       { "dac_level" };

    juce::WebComboBoxRelay      bendRangeRelay        { "bend_range" };
    juce::WebToggleButtonRelay  velToTlRelay          { "vel_to_tl" };
    juce::WebComboBoxRelay      aftertouchTargetRelay { "aftertouch_target" };
    juce::WebComboBoxRelay      voiceCountRelay       { "voice_count" };
    juce::WebComboBoxRelay      uiScaleRelay          { "ui_scale" };

    // Per-PSG-channel relays — same NON_MOVEABLE pinning as the FM relays.
    std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> psgVolRelays  { makePsgVolRelays()  };
    std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> psgPanRelays  { makePsgPanRelays()  };
    std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels> psgBendRelays { makePsgBendRelays() };

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

    // --- Global attachments (bound once) ------------------------------------
    juce::WebSliderParameterAttachment masterGainAttachment;

    // Task 13 — global attachments.
    juce::WebSliderParameterAttachment        psgMixAttachment;
    juce::WebToggleButtonParameterAttachment  psgLayerAttachment;
    juce::WebComboBoxParameterAttachment      psgNoiseTypeAttachment;
    juce::WebComboBoxParameterAttachment      psgNoiseRateAttachment;
    juce::WebToggleButtonParameterAttachment  psgNoiseAutoAttachment;

    juce::WebToggleButtonParameterAttachment  dacEnableAttachment;
    juce::WebComboBoxParameterAttachment      dacRateAttachment;
    juce::WebComboBoxParameterAttachment      dacModeAttachment;
    juce::WebSliderParameterAttachment        dacLevelAttachment;

    juce::WebComboBoxParameterAttachment      bendRangeAttachment;
    juce::WebToggleButtonParameterAttachment  velToTlAttachment;
    juce::WebComboBoxParameterAttachment      aftertouchTargetAttachment;
    juce::WebComboBoxParameterAttachment      voiceCountAttachment;
    juce::WebComboBoxParameterAttachment      uiScaleAttachment;

    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,        SN76489Engine::kNumChannels> psgVolAttachments;
    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,        SN76489Engine::kNumChannels> psgPanAttachments;
    std::array<std::unique_ptr<juce::WebToggleButtonParameterAttachment>,  SN76489Engine::kNumChannels> psgBendAttachments;

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

    // --- FM attachments (rebuilt on every selectChannel) --------------------
    // Owned via unique_ptr so they can be torn down + reconstructed when
    // paging to a different part. Index layout mirrors the relay vectors.
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> opAttachments;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> partAttachments;

    // View 10 polyphony attachments — torn down + rebuilt against the new
    // part's apvts parameter on selectChannel.
    std::unique_ptr<juce::WebComboBoxParameterAttachment> polyModeAttachment;
    std::unique_ptr<juce::WebComboBoxParameterAttachment> monoGlideAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment>   unisonSpreadAttachment;

    // Scratch buffer for the per-tick telemetry scope read — sized at
    // kScopeReadSamples and reused so the timer callback does no allocation.
    std::array<float, kScopeReadSamples> scopeScratch {};

    // Owned native file chooser for the Task 13 LOAD WAV button. Kept as a
    // unique_ptr so each launch creates a fresh dialog without re-using state
    // — the dialog must outlive the async callback, which is why it lives
    // here rather than as a local in the native-function lambda.
    std::unique_ptr<juce::FileChooser> wavChooser;

    // Task 14 — owned choosers for the patch-browser modal's native dialogs
    // (Import / Export / Add Folder). Same lifetime story as wavChooser:
    // each launchAsync stash a fresh chooser here so the dialog outlives the
    // async callback that uses it.
    std::unique_ptr<juce::FileChooser> importChooser;
    std::unique_ptr<juce::FileChooser> exportChooser;
    std::unique_ptr<juce::FileChooser> folderChooser;

    // Preview release timer: armed by the previewPatch native function so the
    // synthetic middle-C is released after ~1s without the JS side having to
    // schedule a second native call. Resets the timer on each Preview click
    // so a rapid second press still gets a full release window.
    std::unique_ptr<juce::Timer> previewReleaseTimer;
    int                          previewActivePart = -1;
    int                          previewActiveNote = -1;

    // Emit a `patchRootsChanged` JS event so the open patch-browser modal
    // refreshes its tree + the quick-access lists rebuild. Pushed after any
    // operation that mutates a root: import, save, drop, add-folder, delete.
    void emitPatchRootsChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessorEditor)
};
