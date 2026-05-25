#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PatchBrowser.h"
#include "PatchSystem.h"
#include "SN76489Engine.h"
#include "Telemetry.h"
#include "VgmLogger.h"
#include "VoiceAllocator.h"

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
    // atomic loads only; dest.name is left untouched.
    void readPatch (Patch& dest) const noexcept;

    std::atomic<float>* opParam[kNumOpParams][kNumOps] {};
    std::atomic<float>* partParam[kNumPartParams]      {};
};

class GenVstAudioProcessor : public juce::AudioProcessor
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

    // Public so unit tests can build the layout standalone (no AudioProcessor
    // instance required).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void renderFmBlock  (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderSqBlock  (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void renderDBlock   (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    void dispatchMidi (const juce::MidiMessage& msg);
    void handleNoteOn  (int note, int velocity);
    void handleNoteOff (int note);
    void handlePitchBend (int bend14bit);
    void handleControlChange (int cc, int value);

    Mode currentMode() const noexcept;

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw pointers — all touched on the audio thread.
    std::atomic<float>* modeSelectParam     = nullptr;
    std::atomic<float>* masterVolumeParam   = nullptr;
    std::atomic<float>* outputFilterParam   = nullptr;
    std::atomic<float>* ladderEffectParam   = nullptr;
    std::atomic<float>* pitchBendRangeParam = nullptr;
    std::atomic<float>* modWheelMirrorParam = nullptr;
    std::atomic<float>* pitchBendMirrorParam = nullptr;

    FmParamCache         paramCache;
    VoiceAllocator       voiceAllocator;
    SN76489Engine        psgEngine;
    genvst::PatchBrowser patchBrowser;
    Telemetry            telemetry;
    VgmLogger            vgmLogger;

    bool patchBrowserInitialised = false;

    // Pre-allocated processBlock scratch — never allocated on the audio thread.
    Patch                  currentPatch;
    juce::HeapBlock<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenVstAudioProcessor)
};
