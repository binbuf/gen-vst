#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PartManager.h"
#include "PatchSystem.h"
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
    ~GenVstAudioProcessor() override = default;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Store `patch` on `part` and push its values into the apvts parameter
    // tree. Message thread only.
    void applyPatchToPart (int part, const Patch& patch);

    // Dev wiring: load factory .tfi files into the parts so the plugin sounds
    // before the patch browser exists (Task 09).
    void loadDevPatches();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* masterGainParam = nullptr;

    PartManager    partManager;
    VoiceAllocator voiceAllocator;
    FmParamCache   paramCache;

    // Pre-allocated processBlock scratch — never allocated on the audio thread.
    Patch                                    noteOnPatch;
    std::array<Patch, PartManager::kNumParts> partPatches;

    juce::HeapBlock<float> monoScratch;   // R-channel sink when the host bus is mono

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
