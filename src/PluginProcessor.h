#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DACPlayer.h"
#include "MidiRouter.h"
#include "PartManager.h"
#include "PatchSystem.h"
#include "SN76489Engine.h"
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

class GenVstAudioProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
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

    // Public so unit tests and the MidiRoutingTests fixture can build the
    // full layout standalone (no AudioProcessor instance required).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    // Store `patch` on `part` and push its values into the apvts parameter
    // tree. Message thread only.
    void applyPatchToPart (int part, const Patch& patch);

    // Dev wiring: load factory .tfi files into the parts so the plugin sounds
    // before the patch browser exists (Task 09). Also populates the
    // factoryPatches list used by Program Change.
    void loadFactoryPatches();

    // Dev wiring: load a known WAV from GENVST_DEV_DAC_WAV (if present) into
    // the DAC so a note on MIDI channel 16 plays an 8-bit sample without
    // needing the WAV loader UI (Task 13) or the state-embedded PCM (Task 16).
    void loadDevDacSample();

    // Resolve every per-part apvts parameter pointer needed by the CC
    // dispatch ahead of time, so the audio thread does no juce::String work.
    void buildCcParamLookup();

    // Message-thread async drain: applies pending Program Change patches.
    void handleAsyncUpdate() override;

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

    PartManager    partManager;
    VoiceAllocator voiceAllocator;
    FmParamCache   paramCache;
    MidiRouter     midiRouter;
    SN76489Engine  psgEngine;
    DACPlayer      dacPlayer;

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

    juce::HeapBlock<float> monoScratch;   // R-channel sink when the host bus is mono

    // Factory patches kept resident so a Program Change is a memcpy, not a
    // file load (07-feature-spec.md "Program Change"). Populated at
    // construction (message thread); read-only thereafter.
    std::vector<Patch> factoryPatches;

    // Audio-thread -> message-thread queue: per-part latest pending Program
    // Change index, -1 == nothing pending. The audio thread stamps the slot
    // and triggerAsyncUpdate(); the message thread drains and applies via
    // applyPatchToPart. Last-write-wins per part is intentional — rapid
    // duplicate PCs collapse to the final patch.
    std::array<std::atomic<int>, PartManager::kNumParts> pendingProgramChange {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
