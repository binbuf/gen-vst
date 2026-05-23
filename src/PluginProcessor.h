#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DACPlayer.h"
#include "MidiRouter.h"
#include "PartManager.h"
#include "PatchBrowser.h"
#include "PatchSystem.h"
#include "SN76489Engine.h"
#include "Telemetry.h"
#include "VoiceAllocator.h"

// Raw std::atomic<float>* views of every per-part FM parameter, cached so the
// audio thread reads parameters with no map lookups, locks or allocation
// (01-architecture.md "Parameter System"). Built once in prepareToPlay.
//
// The schema-size constants below are the source of truth for the descriptor
// tables in PluginProcessor.cpp, which are static_assert'd against them.
struct FmParamCache
{
    static constexpr int kNumOps        = 4;    // YM2612 operators per channel
    static constexpr int kNumOpParams   = 11;   // per-operator params
    static constexpr int kNumPartParams = 7;    // per-part channel params

    void connect (juce::AudioProcessorValueTreeState& apvts);

    // Snapshot part `part`'s current parameter values into `dest`. Audio-thread
    // safe — atomic loads only; dest.name is left untouched.
    void readPatch (int part, Patch& dest) const noexcept;

    std::atomic<float>* opParam[kNumOpParams][PartManager::kNumParts][kNumOps] {};
    std::atomic<float>* partParam[kNumPartParams][PartManager::kNumParts] {};
};

class GenVstAudioProcessor : public juce::AudioProcessor
{
public:
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

    // Exposed for the editor's WebView native functions and for state-save
    // (Task 16), which needs read access to active patch paths + custom roots.
    genvst::PatchBrowser& getPatchBrowser() noexcept       { return patchBrowser; }
    PartManager&          getPartManager()  noexcept       { return partManager;  }
    const PartManager&    getPartManager()  const noexcept { return partManager;  }

    // Exposed for the Task 13 routing modal + inline routing step-fields
    // (08-ui-views.md views 1-3, 5). The UI manipulates the routing table via
    // the editor's getRouting/setRouting/resetRouting native functions, which
    // forward to this object.
    MidiRouter&           getMidiRouter()  noexcept        { return midiRouter; }
    const MidiRouter&     getMidiRouter()  const noexcept  { return midiRouter; }

    // Exposed for the Task 13 D-section view (08-ui-views.md view 3): the
    // editor's loadWavDialog / clearDac / getDacInfo native functions call
    // into the DAC player.
    DACPlayer&            getDacPlayer()   noexcept        { return dacPlayer;  }
    const DACPlayer&      getDacPlayer()   const noexcept  { return dacPlayer;  }

    // Exposed for the editor's ~30 Hz telemetry timer (08-ui-views.md "Header
    // meter bay"). The audio thread writes lock-free; the editor reads
    // snapshots and emits a combined "meterData" event.
    Telemetry&            getTelemetry()    noexcept       { return telemetry;    }

    // Snapshot a part's live patch from the apvts (the audio-thread source of
    // truth — kept fresher than PartManager's stored copy, which only updates
    // through message-thread paths). Used by savePatch / exportPatch so they
    // capture whatever the user is currently hearing, not the last
    // applyPatchToPart write.
    void readLivePatch (int part, Patch& dest) const noexcept
    {
        paramCache.readPatch (part, dest);
    }

    // Queue a synthetic note-on / note-off for `part`, bypassing the MIDI
    // router. Used by the patch browser's *Preview* button (08-ui-views.md
    // view 4) so a click auditions whatever patch the selected part currently
    // holds, regardless of how MIDI is routed. Lock-free: the message thread
    // pushes, the audio thread drains at the top of processBlock.
    void queuePreviewNoteOn  (int part, int note, int velocity) noexcept;
    void queuePreviewNoteOff (int part, int note) noexcept;

    // Public so unit tests and the MidiRoutingTests fixture can build the
    // full layout standalone (no AudioProcessor instance required).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- Task 16: state-restore notify channel --------------------------------
    // Notifications queued during setStateInformation (unresolved patch paths
    // / unresolved custom roots) live here until the editor drains them on
    // its next timer tick — the editor may not even exist yet when state is
    // first restored, so we cannot push events into the WebView directly.
    // Message thread only (JUCE setStateInformation + editor::timerCallback
    // both live there); no mutex required.
    struct PendingNotification
    {
        juce::String level;     // "info" / "warning" / "error"
        juce::String message;
    };
    void addPendingNotification (juce::String level, juce::String message);

    // Drain every pending notification into `sink` (in FIFO order) and clear
    // the queue. Returns true if anything was drained. The brief mutex keeps
    // pluginval --strictness 8's threaded state-save fuzzers safe even if a
    // host happens to violate the message-thread-only invariant.
    template <typename Sink>
    bool drainPendingNotifications (Sink&& sink)
    {
        std::vector<PendingNotification> snapshot;
        {
            const std::lock_guard<std::mutex> lk (pendingNotificationsMutex);
            if (pendingNotifications.empty()) return false;
            snapshot.swap (pendingNotifications);
        }
        for (auto& n : snapshot)
            sink (n);
        return true;
    }

    // Deferred state-restore data: setStateInformation may run before the
    // patch browser is initialised (some DAWs load project state before the
    // first prepareToPlay). The apvts / routing / DAC parts run immediately;
    // the patch reloads + custom-root re-register need the browser and are
    // recorded here for the next prepareToPlay to replay (Task 16).
    struct PendingStateRestore
    {
        bool                                                   active = false;
        std::vector<juce::String>                              customRootPaths;
        std::array<juce::String, PartManager::kNumParts>       patchPaths {};
    };
    PendingStateRestore&       pendingStateRestoreData()       noexcept { return pendingStateRestore; }
    const PendingStateRestore& pendingStateRestoreData() const noexcept { return pendingStateRestore; }

    // Did setStateInformation run on this instance? prepareToPlay's first-time
    // dev-patch loading skips when this is true so a restored project's
    // patches survive instead of being clobbered by the dev fallback.
    bool wasStateRestored() const noexcept { return stateRestored; }

    // Editor UI selection state. Persisted across DAW project save/load so
    // reopening the project returns the user to the same FM channel + tab
    // they last edited. Written by the editor on UI events; written by the
    // PluginState restore path; read by a freshly-mounted editor.
    // The state isn't audio-affecting, so non-atomic int is fine — these
    // are only touched on the message thread.
    int  uiSelectedPart() const noexcept { return uiSelectedPartIndex; }
    void setUiSelectedPart (int n) noexcept
    {
        if (n >= 0 && n < PartManager::kNumParts) uiSelectedPartIndex = n;
    }
    int  uiPresetTab() const noexcept { return uiPresetTabIndex; }
    void setUiPresetTab (int t) noexcept
    {
        if (t == 0 || t == 1) uiPresetTabIndex = t;
    }

private:
    // Store `patch` on `part` and push its values into the apvts parameter
    // tree. Message thread only — uses setValueNotifyingHost.
    void applyPatchToPart (int part, const Patch& patch);

    // Audio-thread patch apply: atomic-stores every per-part FM parameter
    // from `patch` into the apvts via the raw-pointer cache. Lock-free, no
    // allocation. Used by the patch-delivery queue drain at the top of
    // processBlock and by Program Change (audio thread).
    void applyPatchOnAudioThread (int part, const Patch& patch) noexcept;

    // Dev wiring: load a known WAV from GENVST_DEV_DAC_WAV (if present) into
    // the DAC so a note on MIDI channel 16 plays an 8-bit sample without
    // needing the WAV loader UI (Task 13) or the state-embedded PCM (Task 16).
    void loadDevDacSample();

    // Resolve every per-part apvts parameter pointer needed by the CC
    // dispatch ahead of time, so the audio thread does no juce::String work.
    void buildCcParamLookup();

    // Sample-accurate MIDI dispatch (Task 06): the per-event handlers called
    // from processBlock. They mutate partPatches/voiceAllocator/apvts directly
    // on the audio thread and never allocate.
    void renderSubBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void dispatchMidi (const juce::MidiMessage& msg);
    void handleNoteOn  (int channel, int note, int velocity);
    void handleNoteOff (int channel, int note);
    void handlePitchBend (int channel, int bend14bit);
    void handleAftertouch (int channel, int pressure);
    void handleProgramChange (int channel, int program);
    void handleControlChange (int channel, int cc, int value);
    void resetControllersForChannel (int channel);

    // Atomic-store a hardware-range integer value into an int/bool/choice apvts
    // parameter via the raw pointer cache. Audio-thread safe — no allocation,
    // no listener notifications. The host's automation lane sees the change
    // on its next poll; UI relays poll periodically.
    void writeIntParam (std::atomic<float>* target, int value) noexcept;

    int  currentBendRangeSemitones() const noexcept;   // 1 / 2 / 7 / 12
    bool currentVelToTl()            const noexcept;
    int  currentAftertouchTarget()   const noexcept;   // 0=Off / 1=LFO depth / 2=Carrier TL

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* masterGainParam       = nullptr;
    std::atomic<float>* bendRangeParam        = nullptr;
    std::atomic<float>* velToTlParam          = nullptr;
    std::atomic<float>* aftertouchTargetParam = nullptr;
    std::atomic<float>* voiceCountParam       = nullptr;

    // Per-part polyphony (Task 15 / view 10): mode (Poly / Mono / Unison),
    // mono glide (Retrigger / Legato) and unison F-number spread in cents.
    std::array<std::atomic<float>*, PartManager::kNumParts> polyModeParam     {};
    std::array<std::atomic<float>*, PartManager::kNumParts> monoGlideParam    {};
    std::array<std::atomic<float>*, PartManager::kNumParts> unisonSpreadParam {};

    // Task 22 — Per-rack-slot routing params (midi channel, transpose, range,
    // detune cents, balance). Cached as raw atomic pointers so the audio
    // thread doesn't pay a parameter-map lookup on every note-on.
    //
    // The rack pool is the union of FM parts (6), PSG channels (3 tones + 1
    // noise) and DAC. For the FM parts we keep the full 6 slots cached even
    // though the rack widget only exposes 5 — keeps the indexing trivial and
    // leaves part 5 (channel 6 / DAC chip channel) under DAW-automation control.
    struct RackParams
    {
        std::atomic<float>* midiCh      = nullptr;
        std::atomic<float>* transposeSt = nullptr;
        std::atomic<float>* transposeOct = nullptr;
        std::atomic<float>* noteLo      = nullptr;
        std::atomic<float>* noteHi      = nullptr;
        std::atomic<float>* detuneCents = nullptr;
        std::atomic<float>* balance     = nullptr;
    };
    std::array<RackParams, PartManager::kNumParts>            fmRackParams  {};
    std::array<RackParams, SN76489Engine::kNumChannels>       psgRackParams {};
    RackParams                                                dacRackParams {};

    // Helper accessors for the rack-routing logic. These read the cached
    // atomic pointers and apply the standard clamps. Audio-thread safe.
    int    fmPartTransposedNote (int part, int noteIn) const noexcept;
    bool   fmPartAcceptsNote    (int part, int transposedNote) const noexcept;
    double fmPartDetuneSemitones(int part) const noexcept;
    int    fmPartMidiChannel    (int part) const noexcept;
    int    psgChannelMidiChannel(int psgCh) const noexcept;
    int    dacMidiChannel       () const noexcept;

    // Snapshot the per-rack-slot midi_ch params and reapply them to the
    // routing table. Called whenever the message thread suspects the rack
    // routing might be stale (UI change, state restore). Run on the message
    // thread; audio thread uses the destChannel atomics directly via
    // forEachDestination on the next block.
    void syncRackRoutingToTable() noexcept;

    // Snapshot the apvts poly-mode params into the VoiceAllocator + cap the
    // pool to the Settings voice count. Called once per block from
    // processBlock, before any note-on / note-off dispatch.
    void pushPolyphonyParameters() noexcept;

    // CC -> cached apvts parameter pointer, indexed [part][cc]. Built once in
    // the constructor (off the audio thread) so the CC dispatch never has to
    // construct a juce::String to look up its target. nullptr means
    // "no apvts target for this CC" (sustain, panic, unmapped CC, etc).
    std::array<std::array<std::atomic<float>*, 128>, PartManager::kNumParts> ccParamLookup {};
    std::array<std::atomic<float>*, PartManager::kNumParts> lrParamLookup {};   // CC 10 (pan) target

    PartManager          partManager;
    VoiceAllocator       voiceAllocator;
    FmParamCache         paramCache;
    MidiRouter           midiRouter;
    SN76489Engine        psgEngine;
    DACPlayer            dacPlayer;
    genvst::PatchBrowser patchBrowser;
    Telemetry            telemetry;

    // Per-block PSG / DAC parameter snapshot — apvts pointers cached at
    // prepareToPlay so the audio thread reads with no map lookup. Pushed
    // into psgEngine / dacPlayer at the top of each processBlock.
    struct PsgDacParams
    {
        std::atomic<float>* mix             = nullptr;
        std::atomic<float>* noiseType       = nullptr;
        std::atomic<float>* noiseRate       = nullptr;
        std::atomic<float>* noiseAuto       = nullptr;
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> volume { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> pan    { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> bendOn { nullptr, nullptr, nullptr, nullptr };

        // Task 23 — per-channel software-envelope params, pushed to
        // SN76489Engine::setEnvelopeRates / setEnvelopeVel each block.
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> atk { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> dr1 { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> sus { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> dr2 { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> rr  { nullptr, nullptr, nullptr, nullptr };
        std::array<std::atomic<float>*, SN76489Engine::kNumChannels> vel { nullptr, nullptr, nullptr, nullptr };

        std::atomic<float>* dacEnable = nullptr;
        std::atomic<float>* dacRate   = nullptr;
        std::atomic<float>* dacMode   = nullptr;
        std::atomic<float>* dacLevel  = nullptr;
    };
    PsgDacParams psgDacParams;

    void pushPsgDacParameters();

    // Pre-allocated processBlock scratch — never allocated on the audio thread.
    Patch                                    noteOnPatch;
    std::array<Patch, PartManager::kNumParts> partPatches;

    // Preview queue (Task 14): the browser's *Preview* button pushes
    // synthetic note-on/off events here on the message thread; processBlock
    // drains them at the top and dispatches via the existing voice path.
    // Capacity 16 is enough for several rapid preview clicks before the
    // audio thread drains the queue once per block.
    struct PreviewEvent
    {
        int  part     = 0;
        int  note     = 60;
        int  velocity = 0;     // 0 = note-off
    };
    static constexpr int kPreviewQueueCapacity = 16;
    juce::AbstractFifo                                       previewFifo { kPreviewQueueCapacity };
    std::array<PreviewEvent, (std::size_t) kPreviewQueueCapacity> previewSlots {};
    void drainPreviewQueue();

    juce::HeapBlock<float> monoScratch;   // R-channel sink when the host bus is mono

    // First-prepareToPlay flag: the patch browser needs the wrapper type (set
    // by the plugin client wrapper after the constructor returns), so its
    // initialisation is deferred to the first prepareToPlay call.
    bool patchBrowserInitialised = false;

    // Task 16 state-restore bookkeeping.
    std::vector<PendingNotification> pendingNotifications;
    std::mutex                       pendingNotificationsMutex;
    PendingStateRestore              pendingStateRestore;
    bool                             stateRestored = false;

    // Editor UI selection state (message-thread only). Persisted with the
    // rest of the plugin state so reopening the project restores the user's
    // last-edited FM channel + preset/import tab choice.
    int                              uiSelectedPartIndex = 0;
    int                              uiPresetTabIndex    = 0;   // 0 = PRESETS, 1 = IMPORT

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
