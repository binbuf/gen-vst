#pragma once

#include <array>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "SN76489Wrapper.h"

// SN76489 PSG voice engine — 3 tone channels + 1 noise channel
// (03-psg-synthesis.md "SN76489Engine Class Sketch", ADR-0009).
//
// Per-channel soft pan needs per-channel signal access, which the hardware
// chip does not expose (it sums all four channels into a single mono output).
// The engine therefore owns FOUR `SN76489Wrapper` instances — one per
// output channel — each with a mute mask isolating its own channel. Every
// register write is broadcast to all four chips so their internal state stays
// in lockstep; only the mute-mask-filtered output mix differs.
class SN76489Engine
{
public:
    static constexpr int kNumChannels = 4;   // 0..2 = tone, 3 = noise
    static constexpr int kNumToneChs  = 3;
    static constexpr int kNoiseCh     = 3;

    SN76489Engine();

    SN76489Engine (const SN76489Engine&)            = delete;
    SN76489Engine& operator= (const SN76489Engine&) = delete;

    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    // --- MIDI events ---------------------------------------------------------

    // Allocate a tone channel (0..2) for `note` via round-robin LRU and key
    // it on. A fourth concurrent note steals the oldest tone channel
    // (03-psg-synthesis.md "PSG Voice Allocation").
    void noteOnTone (int midiNote, int velocity);

    // Last-note priority on the noise channel: any incoming note-on replaces
    // the previous one immediately.
    void noteOnNoise (int midiNote, int velocity);

    // Release the tone channel currently playing `midiNote` (no-op if none).
    void noteOffTone (int midiNote);

    // Release the noise channel if it is playing `midiNote`.
    void noteOffNoise (int midiNote);

    // Pitch bend reaches PSG channels only when their per-channel bend-enable
    // toggle is on (03-psg-synthesis.md "MIDI Routing Options").
    void setPitchBendSemitones (int psgChannel, double semitones);

    // --- Parameter snapshot (called once per block off-thread before render)
    //
    // The engine maintains a small struct of per-channel parameter values
    // mirrored from the apvts so the audio thread never reads atomic floats
    // mid-render. The PluginProcessor calls these setters at the top of each
    // processBlock with the current parameter snapshot.

    void setChannelVolume      (int psgChannel, float gain01) noexcept;
    void setChannelPan         (int psgChannel, float pan_neg1_pos1) noexcept;
    void setChannelBendEnabled (int psgChannel, bool on) noexcept;
    void setNoiseType          (int periodicOrWhite) noexcept;   // 0=periodic, 1=white
    void setNoiseShiftRate     (int rate0to3) noexcept;
    void setNoiseAutoMode      (bool on) noexcept;
    void setMixLevel           (float mix01) noexcept;

    // --- Per-block render ----------------------------------------------------

    // Add the PSG output (host-rate, stereo) to the given buffers. PSG
    // resamples internally (ADR-0011), so this is a pure additive mixdown
    // into the already-rendered FM output.
    void renderAdd (float* outL, float* outR, int numSamples);

    // --- Introspection (tests) -----------------------------------------------

    bool isToneChannelActive (int psgChannel) const noexcept;
    int  toneChannelNote     (int psgChannel) const noexcept;
    bool isNoiseChannelActive() const noexcept;
    int  noiseChannelNote()     const noexcept;

    // Re-derive the noise control byte and write it to the chips. Public for
    // test access — the engine calls this internally on any noise param/note
    // change.
    void refreshNoiseControl();

private:
    void writeAllChips (std::uint8_t byte);
    void writeToneFreq (int toneChannel, double midiNoteWithBend);
    void writeToneVolume (int toneChannel, std::uint8_t attenuation);
    void writeNoiseVolume (std::uint8_t attenuation);

    static std::uint8_t velocityToAttenuation (int velocity) noexcept;
    static int          midiNoteToShiftRate (int midiNote) noexcept;

    std::array<SN76489Wrapper, kNumChannels> chips;

    // Scratch buffer for one chip's host-rate mono output between channels.
    std::vector<float> chipScratch;

    // Per-channel runtime state. For tone channels (0..2) `note` is the
    // currently-keyed MIDI note (-1 = idle), `timestamp` is the LRU counter,
    // and `velocity` is held for volume re-derivation on parameter changes.
    struct ChannelState
    {
        int            note          = -1;
        int            velocity      = 0;
        bool           active        = false;
        bool           bendEnabled   = false;
        double         bendSemitones = 0.0;
        std::uint64_t  timestamp     = 0;
        float          volumeGain    = 1.0f;
        float          panLeft       = 1.0f;       // pan + mix combined L gain
        float          panRight      = 1.0f;       // pan + mix combined R gain
    };

    std::array<ChannelState, kNumChannels> ch;

    // Direct noise parameters (03-psg-synthesis.md "Noise Control — Direct UI
    // Parameters"). The auto-mode maps MIDI note -> shift rate as an
    // optional convenience, off by default.
    int  noiseType    = 1;     // 0=periodic, 1=white
    int  noiseRate    = 1;     // 0..3 (00,01,10,11)
    bool noiseAuto    = false;

    float mixLevel       = 0.8f;
    std::uint64_t nextTimestamp = 0;
};
