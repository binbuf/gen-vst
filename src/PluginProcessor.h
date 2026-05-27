#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DspDecimator.h"
#include "LadderEffect.h"
#include "OutputFilter.h"
#include "PatchBrowser.h"
#include "PatchSystem.h"
#include "PluginState.h"
#include "PsgPreset.h"
#include "SN76489Engine.h"
#include "Telemetry.h"
#include "VgmLogger.h"
#include "VoiceAllocator.h"

// Notifier callback signature for patchLoaded → editor. The processor
// invokes the registered callback on the message thread after a preset
// has been applied so the editor can push a `patchLoaded` event into the
// WebView (which updates the header LCD).
struct PatchLoadedNotifier
{
    juce::String name;             // display name (filename stem or preset.name)
    Tag          tag = Tag::FM;    // FM or SQ — auto-switch destination
    juce::String path;             // absolute path of the loaded patch
};

// Raw std::atomic<float>* views of the v2 single-engine FM parameters, cached
// so the audio thread reads parameters with no map lookups, locks or allocation
// (01-architecture.md "Parameter System").
struct FmParamCache
{
    static constexpr int kNumOps        = 4;    // YM2612 operators per channel
    static constexpr int kNumOpParams   = 11;   // per-operator params
    static constexpr int kNumPartParams = 7;    // per-channel params

    void connect (juce::AudioProcessorValueTreeState& apvts);

    // Snapshot the current parameter values into `dest`. Audio-thread safe —
    // atomic loads only; dest.name is left untouched. TL/SL are stored as
    // *level* in apvts (02-fm-synthesis.md *UI level vs hardware attenuation*);
    // this routine inverts them back to hardware *attenuation* on the way out.
    void readPatch (Patch& dest) const noexcept;

    std::atomic<float>* opParam[kNumOpParams][kNumOps] {};
    std::atomic<float>* partParam[kNumPartParams]      {};

    // v2 — per-op float / bool / float-Hz fields, plus per-part floats / enums.
    std::atomic<float>* mulFloatParam   [kNumOps] {};
    std::atomic<float>* fixedParam      [kNumOps] {};
    std::atomic<float>* freqFixedHzParam[kNumOps] {};
    std::atomic<float>* velParam        [kNumOps] {};
    std::atomic<float>* channelTlParam     = nullptr;
    std::atomic<float>* fmDacPrescalerParam = nullptr;
    std::atomic<float>* freqCtrlModeParam   = nullptr;
    std::atomic<float>* retrigRateParam     = nullptr;
};

class GenVstAudioProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    // Active engine — selected by the `mode_select` apvts parameter.
    enum class Mode : int { FM = 0, SQ = 1, D = 2 };

    GenVstAudioProcessor();
    ~GenVstAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    Telemetry& getTelemetry() noexcept { return telemetry; }
    genvst::PatchBrowser& getPatchBrowser() noexcept { return patchBrowser; }

    // Reset every apvts parameter to its juce::AudioParameter default. Used
    // by the Settings → RESET ALL TO DEFAULTS confirmation flow
    // (08-ui-views.md view 6). Also clears the active patch path on the
    // PatchBrowser — see Task 09 for the persisted-path side; for v0.2 the
    // patch path is purely a UI label, but the browser owns it.
    void resetAllParametersToDefaults();

    // ---- Task 09: tagged preset loading ------------------------------------
    // Load the preset at `absolutePath`. Message thread only. Detects the
    // file's Tag (via PatchSystem::tagFromFile), flips `mode_select` if the
    // tag differs from the current mode, applies the preset to the apvts
    // (FM via the patch-delivery FIFO; SQ via setValueNotifyingHost), and
    // records the active path for the destination mode. Returns an empty
    // string on success or a descriptive error message for the toast.
    // On success the registered patch-loaded callback (see
    // setPatchLoadedNotifier) is invoked with the patch name + tag + path.
    juce::String loadPresetFromPath (const juce::String& absolutePath);

    // Save the current mode's apvts state to disk in the user-saved root.
    // Returns the absolute output path on success or empty on failure.
    // FM writes `<name>.tfi`; SQ writes `<name>.psg`. D mode is unsavable
    // (no preset format) — returns empty + non-empty `outError`.
    juce::String savePresetForCurrentMode (const juce::String& name,
                                           juce::String& outError);

    // Export the current mode's apvts state to `destinationPath`. The
    // extension determines the format; FM supports `.tfi` / `.vgi`, SQ
    // supports `.psg`. Returns empty on success or an error string.
    juce::String exportPresetForCurrentMode (const juce::String& destinationPath);

    // Default factory preset path for `mode`. Used by the manual-mode-switch
    // listener and by patchNav when the destination mode has no active path
    // yet. Empty for D (no preset format).
    juce::String defaultPresetPathForMode (Mode mode) const;

    // Active patch paths, set when loadPresetFromPath succeeds. Empty
    // strings mean "no preset has been loaded for this mode yet". Read by
    // the manual-mode-switch listener (decides whether to load the default)
    // and by patchNav (anchors prev/next traversal).
    juce::String activePathForMode (Mode mode) const;

    // Prev / next navigation within the active mode's sorted preset list.
    // Direction: -1 = prev, +1 = next. Loads the resolved preset via
    // loadPresetFromPath (so the patch-loaded callback fires). No-op in D
    // mode (returns empty). Returns an error string on failure.
    juce::String patchNavigate (int direction);

    // Build a flat JSON-ready array describing every preset across every
    // root with its tag (FM/SQ — Pending DMP files are resolved via
    // tagFromFile here so the browser badge is final). Used by the
    // getPatchList native function. Schema:
    //   [{ name, path, tag, rootId, folderPath }, ...]
    juce::var listAllPresetsAsJson() const;

    // Register the patch-loaded callback. Called on the message thread.
    // Pass {} to clear. Editor calls this in its constructor.
    void setPatchLoadedNotifier (std::function<void (const PatchLoadedNotifier&)> cb);

    // Register a notifier for state-restore toasts (unresolved patch path,
    // unresolved custom root, legacy v1 state ignored — see PluginState.cpp).
    // Drains any toasts queued before the editor registered. Pass {} to
    // clear. Called from the editor constructor on the message thread.
    void setStateRestoreNotifier (std::function<void (const juce::String& level,
                                                      const juce::String& message)> cb);

    // Public so unit tests can build the layout standalone (no AudioProcessor
    // instance required).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Inject a synthetic note event from the on-screen keyboard. Called on the
    // message thread (WebView native function callback); drained into the MIDI
    // buffer at the start of the next processBlock on the audio thread.
    void injectNoteOn  (int pitch, int velocity);
    void injectNoteOff (int pitch);

    // The active engine for the current block, as read from the mode_select
    // apvts parameter. Public so the editor can synchronise its UI to the
    // current mode without round-tripping through the apvts itself
    // (e.g. the uiReady handler synthesises a patchLoaded event using
    // currentMode() + activePathForMode()).
    Mode currentMode() const noexcept;

private:
    // juce::AudioProcessorValueTreeState::Listener — called when mode_select
    // changes (audio or message thread). We forward to the AsyncUpdater so
    // the default-load runs on the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // juce::AsyncUpdater — runs on the message thread after a mode_select
    // change. Loads the default preset for the new mode if no preset is
    // already active there (and the mode is FM or SQ).
    void handleAsyncUpdate() override;

    // Internal apply paths for the two preset formats. Both run on the
    // message thread. The FM path enqueues into the patch-delivery FIFO and
    // also calls setValueNotifyingHost so the host + UI see the change.
    juce::String applyFmPatch (const Patch& patch, const juce::String& absolutePath);
    juce::String applyPsgPreset (const PsgPreset& preset, const juce::String& absolutePath);

    // Per-mode active path setter (internal — apvts state-save persists the
    // pair via genvst::state::save). State-restore drives the parallel
    // restoreActivePathForMode helper below.
    void setActivePathForMode (Mode mode, const juce::String& path);

    // Drain the pending-restore payload queued by setStateInformation. Called
    // at the end of the first prepareToPlay after restore (once the JUCE
    // wrapper type has been set and the patch browser has been initialised).
    // Each per-mode path is verified against the filesystem; resolvable paths
    // are recorded as active + the editor's patchLoaded callback fires (so
    // the header LCD updates); unresolvable paths raise a state-restore
    // toast and the active path is left empty (the restored apvts values
    // stay in place — 01-architecture.md *State Persistence*). Each custom
    // root is re-registered via PatchBrowser::addCustomRoot; unresolvable
    // paths raise a toast.
    void drainPendingStateRestore();

    // Emit a state-restore toast. If the editor has registered a notifier
    // the call routes through; otherwise the toast is queued and drained
    // when the editor registers (covers the "setStateInformation runs
    // before createEditor" case typical for Reaper / Logic / etc.).
    void emitStateRestoreToast (const juce::String& level,
                                const juce::String& message);

    void renderFmBlock  (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderSqBlock  (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderDBlock   (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    // Push the v2 polyphony / legato apvts state into the voice allocator. Run
    // each FM render block so the user's panel edits flow through.
    void pushPolyphonyParameters();

    void dispatchMidi (const juce::MidiMessage& msg);
    void handleNoteOn  (int note, int velocity);
    void handleNoteOff (int note);
    void handlePitchBend (int bend14bit);
    void handleControlChange (int cc, int value);
    void handleChannelPressure (int value);

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw pointers — all touched on the audio thread.
    std::atomic<float>* modeSelectParam      = nullptr;
    std::atomic<float>* masterVolumeParam    = nullptr;
    std::atomic<float>* outputFilterParam    = nullptr;
    std::atomic<float>* ladderEffectParam    = nullptr;
    std::atomic<float>* pitchBendRangeParam  = nullptr;
    std::atomic<float>* modWheelMirrorParam  = nullptr;
    std::atomic<float>* pitchBendMirrorParam = nullptr;
    std::atomic<float>* prescalerParam       = nullptr;
    std::atomic<float>* monoParam            = nullptr;
    std::atomic<float>* dryWetParam          = nullptr;

    // v2 — polyphony / legato / velocity-to-TL globals.
    std::atomic<float>* noteModeParam      = nullptr;
    std::atomic<float>* polyVoicesParam    = nullptr;
    std::atomic<float>* velocityToTlParam  = nullptr;
    std::atomic<float>* hardwareStrictParam = nullptr;
    std::atomic<float>* aftertouchTargetParam = nullptr;

    // SQ engine param cache — pushed to psgEngine each renderSqBlock.
    // SN76489Engine.h:128-133 contracts that the processor snapshots apvts
    // into the engine every block; without that the engine sits at defaults
    // and presets / panel knobs have no audible effect.
    static constexpr int kPsgCacheChannels = 4;   // ch1/ch2/ch3/noise
    static constexpr int kPsgCacheToneChs  = 3;
    std::atomic<float>* psgAtkParam  [kPsgCacheChannels] {};
    std::atomic<float>* psgDr1Param  [kPsgCacheChannels] {};
    std::atomic<float>* psgSusParam  [kPsgCacheChannels] {};
    std::atomic<float>* psgDr2Param  [kPsgCacheChannels] {};
    std::atomic<float>* psgRrParam   [kPsgCacheChannels] {};
    std::atomic<float>* psgVelParam  [kPsgCacheChannels] {};
    std::atomic<float>* psgVolParam  [kPsgCacheChannels] {};
    std::atomic<float>* psgPanParam  [kPsgCacheChannels] {};
    std::atomic<float>* psgGlideParam [kPsgCacheToneChs] {};
    std::atomic<float>* psgDetuneParam[kPsgCacheToneChs] {};
    std::atomic<float>* psgNoiseTypeParam = nullptr;
    std::atomic<float>* psgNoiseRateParam = nullptr;
    std::atomic<float>* psgNoiseAutoParam = nullptr;

    FmParamCache         paramCache;
    VoiceAllocator       voiceAllocator;
    SN76489Engine        psgEngine;
    genvst::PatchBrowser patchBrowser;
    Telemetry            telemetry;
    VgmLogger            vgmLogger;

    DspDecimator decimator;    // D mode bitcrush (input bus)
    DspDecimator fmDecimator;  // FM mode DAC-prescaler decimator (voice-sum bus)
    OutputFilter outputFilter;
    LadderEffect ladder;

    bool patchBrowserInitialised = false;

    // Pre-allocated processBlock scratch — never allocated on the audio thread.
    Patch                    currentPatch;
    juce::HeapBlock<float>   monoScratch;
    juce::AudioBuffer<float> inputCopyBuffer;   // D mode dry signal (stereo).

    // Mode-change fade: when `mode_select` differs from the previous block's
    // mode, the new mode renders normally and the entire output is multiplied
    // by a 0 → 1 ramp across the block to hide the boundary.
    Mode lastMode = Mode::FM;

    // Latest channel-pressure value, normalised [0, 1]. Updated from the MIDI
    // dispatch (handleChannelPressure); read once per FM render block to drive
    // the AFTERTOUCH routing (LFO PMS or Carrier TL) per Settings view 6.
    std::atomic<float> channelPressureNorm { 0.0f };

    // Task 09: per-mode active patch paths + factory-root path cache. Empty
    // string = "no preset has been loaded for this mode". Read by the manual
    // mode-switch listener (decides default-load) and by patchNav.
    juce::String                  activeFmPath;
    juce::String                  activeSqPath;
    mutable juce::CriticalSection activePathLock;

    std::filesystem::path                 factoryRootPath;
    std::function<void (const PatchLoadedNotifier&)> patchLoadedCallback;

    // Latch the previous mode in handleAsyncUpdate so we don't load the
    // default twice in a row (the listener can fire multiple times per
    // change in some hosts).
    Mode lastHandledMode = Mode::FM;

    // Pending state-restore payload (Task 11). setStateInformation parses
    // the v2 envelope into here and clears it once prepareToPlay drains it.
    std::optional<genvst::state::PendingRestore>          pendingStateRestore;

    // State-restore toast callback + queue. The editor registers in its
    // ctor and we drain any toasts queued before that point (covers the
    // setStateInformation-before-createEditor flow common in real hosts).
    std::function<void (const juce::String&, const juce::String&)> stateRestoreToastCallback;
    std::vector<std::pair<juce::String, juce::String>>             pendingStateRestoreToasts;

    // On-screen keyboard note injection. The message thread writes via
    // injectNoteOn/Off; the audio thread drains into midiMessages at the
    // start of processBlock. SPSC — one producer (message thread), one
    // consumer (audio thread).
    struct PendingNoteEvent
    {
        int  pitch    = 0;
        int  velocity = 0;
        bool isNoteOn = false;
    };
    static constexpr int kNoteQueueSize = 64;
    juce::AbstractFifo                             noteQueueFifo { kNoteQueueSize };
    std::array<PendingNoteEvent, kNoteQueueSize>   noteQueueData {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
