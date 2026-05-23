#pragma once

#include <array>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PatchSystem.h"
#include "Voice.h"

// The shared 16-voice FM pool (ADR-0013).
//
// Voices are not statically owned by parts. On note-on the allocator takes a
// free voice — or steals one by global LRU, preferring release-phase voices —
// loads it with the part's patch and keys it on. Each block it sums every
// sounding voice at the YM2612 native rate and resamples that mix once to the
// host rate (ADR-0011).
class VoiceAllocator
{
public:
    static constexpr int kNumVoices = 16;

    // The six-part multitimbral model (ADR-0013); updateActiveVoices is handed
    // one current patch per part.
    static constexpr int kNumParts = 6;

    VoiceAllocator() = default;

    VoiceAllocator (const VoiceAllocator&)            = delete;
    VoiceAllocator& operator= (const VoiceAllocator&) = delete;

    // Allocate working buffers and reset every voice. Call from prepareToPlay.
    void prepare (double hostSampleRate, int maxBlockSize);

    // --- MIDI events (block-granular for Task 05) ----------------------------

    // Take a voice for (part, note) and key it on with `patch`.
    void noteOn (int part, int note, int velocity, const Patch& patch);

    // Release the sounding voice matching (part, note), if any.
    void noteOff (int part, int note);

    // Key off every sounding voice; envelopes release naturally.
    void allNotesOff();

    // Silence and free every voice immediately, with no release tail.
    void allSoundOff();

    // --- Per-block render ----------------------------------------------------

    // Dirty-diff every sounding voice against its part's current patch.
    void updateActiveVoices (const std::array<Patch, kNumParts>& partPatches);

    // Render `numSamples` of host-rate stereo audio: sum all sounding voices at
    // the native rate, then resample the mix to the host rate in one pass.
    void render (float* outL, float* outR, int numSamples);

    // --- Introspection (tests / telemetry) -----------------------------------

    int  numActiveVoices()    const;   // keyed on
    int  numReleasingVoices() const;   // keyed off, still ringing
    int  numIdleVoices()      const;   // free
    bool isNoteActive (int part, int note) const;

private:
    // Idle voice if any, else an LRU steal: oldest Released voice, else oldest
    // Active voice.
    Voice& allocateVoice();

    std::array<Voice, kNumVoices> voices;

    std::uint64_t nextTimestamp = 0;

    // Native-rate -> host-rate resampling (ADR-0011): the same scheme as the
    // Task 02 single-voice path, but summing the whole pool before resampling.
    double hostRate   = 44100.0;
    double nativeRate = 53267.0;
    double speedRatio = 1.0;          // nativeRate / hostRate

    juce::AudioBuffer<float> nativeMixBuffer;   // chip-rate L/R mix scratch
    int nativeCapacity = 0;
    int carry          = 0;           // native samples held over between blocks

    juce::LagrangeInterpolator resamplerL;
    juce::LagrangeInterpolator resamplerR;
};
