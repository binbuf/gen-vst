#pragma once

#include <array>
#include <atomic>
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
