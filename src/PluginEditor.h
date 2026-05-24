#pragma once

#include <array>
#include <functional>
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

    // Task 23 — per-PSG-channel envelope params. Nine int sliders (ATK,
    // DR1, SUS, DR2, RR, DETUNE, FREQ, KSR, SSG) + one toggle (VEL). Index
    // order matches kPsgEnvSliderBases below, and the JS-side OperatorPanel
    // binding map.
    static constexpr int kNumPsgEnvSliderParams = 9;

    // Task 22 — Per-rack-slot routing relay counts. The pool covers every slot
    // the rack UI can address: FM parts 1..6 (the widget exposes 1..5; part 6
    // stays automatable via DAW), the four PSG channels (3 tones + noise), and
    // the DAC. 7 params × 11 slots = 77 slider relays.
    static constexpr int kNumRackParamsPerSlot = 7;
    static constexpr int kNumRackSlotSuffixes  = PartManager::kNumParts
                                               + SN76489Engine::kNumChannels
                                               + 1;

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
    // custom root) and any patch file whose extension is in
    // kSupportedPatchExtensions (copied into the user-imported root). Other
    // files are rejected, so dropping a random WAV onto the plugin window does
    // nothing. The patch-browser modal's tree refreshes via the
    // patchRootsChanged event emitted afterwards.
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

    // Cross-instance patch refresh: piggybacks on the 30 Hz telemetry timer.
    // Every ~2 s we re-stat the user-saved + user-imported roots; if either
    // mtime moved (another plugin instance dropped a file there), the
    // PatchBrowser re-scans the roots from disk and the JS UI gets a
    // patchRootsChanged event. Cheap (two file-system stats) and only runs
    // while the editor is mounted.
    void pollWritableRootsForExternalChanges();
    std::int64_t lastSavedMtime    = 0;
    std::int64_t lastImportedMtime = 0;
    int          mtimePollTickCounter = 0;

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

    // Task 23 — per-channel envelope param relays. The 9 slider relays carry
    // the integer-valued ATK/DR1/SUS/DR2/RR/DETUNE/FREQ/KSR/SSG params; the
    // toggle relay carries VEL (a Bool that enables velocity sensitivity).
    static std::array<std::array<std::unique_ptr<juce::WebSliderRelay>,
                                 SN76489Engine::kNumChannels>,
                      kNumPsgEnvSliderParams>
        makePsgEnvSliderRelays();
    static std::array<std::unique_ptr<juce::WebToggleButtonRelay>,
                      SN76489Engine::kNumChannels>
        makePsgEnvVelRelays();

    // Task 22 — Per-rack-slot routing relays. One slider relay per (param,
    // slot-suffix) pair. The 11 slot suffixes are: _part1..6, _psg_ch1, _psg_ch2,
    // _psg_ch3, _psg_noise, _dac. Index layout matches rackRoutingSuffixes()
    // below + rackRoutingParamBases().
    static std::array<std::unique_ptr<juce::WebSliderRelay>,
                      (std::size_t) (kNumRackParamsPerSlot * kNumRackSlotSuffixes)>
        makeRackRoutingRelays();

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
    juce::WebToggleButtonRelay  trueStereoRelay       { "true_stereo" };
    juce::WebComboBoxRelay      aftertouchTargetRelay { "aftertouch_target" };
    juce::WebComboBoxRelay      voiceCountRelay       { "voice_count" };
    juce::WebComboBoxRelay      uiScaleRelay          { "ui_scale" };
    juce::WebToggleButtonRelay  tooltipsEnabledRelay  { "tooltips_enabled" };

    // Per-PSG-channel relays — same NON_MOVEABLE pinning as the FM relays.
    std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> psgVolRelays  { makePsgVolRelays()  };
    std::array<std::unique_ptr<juce::WebSliderRelay>,       SN76489Engine::kNumChannels> psgPanRelays  { makePsgPanRelays()  };
    std::array<std::unique_ptr<juce::WebToggleButtonRelay>, SN76489Engine::kNumChannels> psgBendRelays { makePsgBendRelays() };

    // Task 23 — per-channel envelope relays. Indexed [paramIdx][chIdx];
    // paramIdx order is fixed by kPsgEnvSliderBases in the .cpp.
    std::array<std::array<std::unique_ptr<juce::WebSliderRelay>,
                          SN76489Engine::kNumChannels>,
               kNumPsgEnvSliderParams>
        psgEnvSliderRelays { makePsgEnvSliderRelays() };

    std::array<std::unique_ptr<juce::WebToggleButtonRelay>,
               SN76489Engine::kNumChannels>
        psgEnvVelRelays { makePsgEnvVelRelays() };

    // Task 22 — Per-rack-slot routing relays. One slider relay per (param,
    // slot-suffix) pair, bound permanently to its apvts parameter. JS-side the
    // rack-routing strip binds via `bindSlider("midi_ch_part1")` etc., and the
    // existing relay path keeps the value in sync with apvts / DAW automation
    // / state restore. See kNumRack* constants near the top of the class.
    std::array<std::unique_ptr<juce::WebSliderRelay>,
               (std::size_t) (kNumRackParamsPerSlot * kNumRackSlotSuffixes)>
        rackRoutingRelays { makeRackRoutingRelays() };

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
    juce::WebToggleButtonParameterAttachment  trueStereoAttachment;
    juce::WebComboBoxParameterAttachment      aftertouchTargetAttachment;
    juce::WebComboBoxParameterAttachment      voiceCountAttachment;
    juce::WebComboBoxParameterAttachment      uiScaleAttachment;
    juce::WebToggleButtonParameterAttachment  tooltipsEnabledAttachment;

    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,        SN76489Engine::kNumChannels> psgVolAttachments;
    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,        SN76489Engine::kNumChannels> psgPanAttachments;
    std::array<std::unique_ptr<juce::WebToggleButtonParameterAttachment>,  SN76489Engine::kNumChannels> psgBendAttachments;

    // Task 23 — per-channel envelope attachments. Matched 1:1 with the
    // psgEnv*Relays arrays above; built once in the constructor body.
    std::array<std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,
                          SN76489Engine::kNumChannels>,
               kNumPsgEnvSliderParams>
        psgEnvSliderAttachments;

    std::array<std::unique_ptr<juce::WebToggleButtonParameterAttachment>,
               SN76489Engine::kNumChannels>
        psgEnvVelAttachments;

    // Task 22 — Per-rack-slot routing attachments. Matched 1:1 with
    // rackRoutingRelays. Built once in the constructor, never rebuilt — the
    // rack-routing strip rebinds JS-side by switching which apvts parameter
    // name the widget asks for, while every relay remains pinned.
    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>,
               (std::size_t) (kNumRackParamsPerSlot * kNumRackSlotSuffixes)>
        rackRoutingAttachments;

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

    // Task 21 — VGM bank-import chooser (`*.vgm;*.vgz`). Same lifetime
    // contract as the others; replaced on every launch.
    std::unique_ptr<juce::FileChooser> vgmImportChooser;

    // Task 24 — IMPORT-tab action choosers. Same pattern as the others: each
    // launch stashes a fresh chooser here so the dialog outlives the async
    // callback that uses it.
    std::unique_ptr<juce::FileChooser> bankExportChooser;
    std::unique_ptr<juce::FileChooser> stateSaveChooser;
    std::unique_ptr<juce::FileChooser> stateLoadChooser;

    // Task 30 — Scala tuning import chooser (*.scl). Same lifetime contract.
    std::unique_ptr<juce::FileChooser> sclChooser;

    // Task 21 — VGM bank-import runner. Reads `filePath`, runs the heavy
    // parse on a juce::Thread::launch-spawned background thread, writes each
    // extracted patch to the user-imported root, refreshes the IMPORT-tab
    // tree on the message thread, and finally invokes `done(savedCount,
    // errorMessage)` on the message thread. Shared by the importBankDialog
    // native function and the .vgm/.vgz drag-and-drop branch.
    //
    // If the editor is destroyed before the background thread finishes, the
    // message-thread callback short-circuits via juce::Component::SafePointer
    // and `done` is never invoked — safe because the JS context that owned
    // the Promise is gone with the WebView.
    void runVgmExtractAsync (
        const juce::String& filePath,
        std::function<void (int savedCount, const juce::String& error)> done);

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
