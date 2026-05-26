#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

#include "SN76489Wrapper.h"

class VgmLogger;

// SN76489 PSG voice engine — 3 tone channels + 1 noise channel
// (03-psg-synthesis.md "SN76489Engine Class Sketch", ADR-0009).
//
// Per-channel soft pan needs per-channel signal access, which the hardware
// chip does not expose (it sums all four channels into a single mono output).
// The engine therefore owns FOUR `SN76489Wrapper` instances — one per
// output channel — each with a mute mask isolating its own channel. Every
// register write is broadcast to all four chips so their internal state stays
// in lockstep; only the mute-mask-filtered output mix differs.
//
// SOFTWARE AMPLITUDE ADSR (Task 23): the SN76489 has no envelope hardware, so
// the engine synthesises one per channel (ATK→DR1→SUS→DR2→RR) and multiplies
// the resulting amplitude into the mix gain before the chip output. Stage
// names mirror the FM operator panel so the JS UI can reuse the same widget,
// but the math runs entirely in software here.
class SN76489Engine
{
public:
    static constexpr int kNumChannels = 4;   // 0..2 = tone, 3 = noise
    static constexpr int kNumToneChs  = 3;
    static constexpr int kNoiseCh     = 3;

    // PsgEnvelope — per-channel software ADSR (Task 23).
    //
    // ATK / DR1 / SUS / DR2 / RR mirror the FM operator-panel knob names but
    // the semantics are software-defined here: ATK is an ATTACK TIME (0 =
    // instant, higher = longer ramp), DR1 / DR2 are decay times in the same
    // sense, SUS is a sustain LEVEL (0 = peak / no decay, 15 = silent), and
    // RR is a release time. Amplitudes are 0..1 floats applied as a mix
    // multiplier; the chip's own 4-bit attenuation register still encodes
    // velocity (so the floating-point envelope rides on top of velocity).
    struct PsgEnvelope
    {
        enum class Stage : std::uint8_t { Idle, Attack, Decay1, Sustain, Decay2, Release };

        // Rates / level mirrored from apvts each block.
        int   atk = 0, dr1 = 0, sus = 0, dr2 = 0, rr = 0;

        // Velocity sensitivity 0..1: 0 = ignore velocity (always full peak),
        // 1 = peak scales linearly with MIDI velocity / 127.
        float velSensitivity = 1.0f;

        // Live state, advanced per render block.
        Stage stage         = Stage::Idle;
        float amplitude     = 0.0f;
        float peakLevel     = 1.0f;     // velocity-scaled target for Attack
        float sustainAmp    = 1.0f;     // peak * (1 - sus/15)
        float stageDelta    = 0.0f;     // amp change per sample in current stage
        int   stageSamples  = 0;        // samples remaining in current stage; -1 = hold
        bool  keyDown       = false;

        void prepare (double sr) noexcept;
        void setSampleRate (double sr) noexcept;
        void setRates (int atk, int dr1, int sus, int dr2, int rr) noexcept;
        void setVelocitySensitivity (float vel01) noexcept;
        void noteOn (int velocity) noexcept;       // velocity 0..127
        void noteOff() noexcept;
        void reset() noexcept;

        // Advance the envelope by `n` samples; updates amplitude + stage.
        void advance (int n) noexcept;

    private:
        double sampleRate = 44100.0;

        // Map a 0..maxRate "time" value to a sample count. rate == 0 → 0
        // (instant); higher → longer ramp. Calibrated so rate == maxRate is
        // roughly 2 seconds at 44.1 kHz; intermediate values interpolate
        // linearly. Exposed as a private helper so test fixtures hitting
        // setRates() see consistent timings.
        int stageSamplesFromRate (int rate, int maxRate) const noexcept;

        void enterAttack() noexcept;
        void enterDecay1() noexcept;
        void enterSustain() noexcept;
        void enterDecay2() noexcept;
        void enterRelease() noexcept;
        void advanceToNextStage() noexcept;
    };

    SN76489Engine();

    SN76489Engine (const SN76489Engine&)            = delete;
    SN76489Engine& operator= (const SN76489Engine&) = delete;

    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    // Task 29 — install a VGM logger pointer so every chip-broadcast write
    // (writeAllChips) mirrors one entry into the on-disk capture. The four
    // shadow chips share one logical write, so we log once per logical write
    // here rather than four times at the wrapper level. nullptr disables.
    void setVgmLogger (VgmLogger* logger) noexcept { vgmLogger = logger; }

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

    // Per-channel software-envelope params (Task 23). Push from apvts at the
    // top of each render block; the engine forwards into the per-channel
    // PsgEnvelope state.
    void setEnvelopeRates  (int psgChannel, int atk, int dr1, int sus, int dr2, int rr) noexcept;
    void setEnvelopeVel    (int psgChannel, float vel01) noexcept;

    // Task 28 — per-tone-channel portamento time in ms. 0 = instant (current
    // behaviour); >0 makes a new note-on slide from the channel's current
    // pitch to the new note over the configured time. Noise has no pitch and
    // ignores this setter for any psgChannel >= kNumToneChs.
    void setGlideTimeMs (int psgChannel, double ms) noexcept;

    // Per-tone-channel detune in cents (-100..+100). Added to the channel's
    // MIDI note before the divider is computed, alongside pitch bend +
    // glide. Noise has no pitch and ignores any psgChannel >= kNumToneChs.
    void setToneDetuneCents (int psgChannel, double cents) noexcept;

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

    // Envelope introspection for PsgEnvelopeTests — exposes the per-channel
    // PsgEnvelope so the tests can assert amplitude / stage after rendering
    // sub-blocks without owning a PsgEnvelope themselves.
    float            channelAmplitude (int psgChannel) const noexcept;
    PsgEnvelope::Stage channelStage   (int psgChannel) const noexcept;

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
        // v2: pitch bend is engine-global (no per-channel UI opt-in — the v1
        // `psg_bend[0..3]` toggles were dropped, see docs/tasks/mvp2/02-strip-v1.md).
        // Default ON so PluginProcessor::renderSqBlock's per-block
        // setPitchBendSemitones reaches the chip; setChannelBendEnabled
        // survives only as a test backdoor.
        bool           bendEnabled   = true;
        double         bendSemitones = 0.0;
        std::uint64_t  timestamp     = 0;
        float          volumeGain    = 1.0f;
        float          panLeft       = 1.0f;       // pan + mix combined L gain
        float          panRight      = 1.0f;       // pan + mix combined R gain

        // Software ADSR — multiplied into the per-block mix gain. Task 23.
        PsgEnvelope    envelope;

        // Task 28 — portamento state. glideTimeMs is mirrored from apvts each
        // block; the *NotesPerSample rate is computed at note-on (and is the
        // signed semitones-per-host-sample delta added each block while the
        // glide is active). When current == target the rate is held at 0.0.
        double         glideTimeMs             = 0.0;
        double         glideCurrentMidi        = 0.0;
        double         glideTargetMidi         = 0.0;
        double         glideRateNotesPerSample = 0.0;

        // Per-channel detune in semitones (apvts cents / 100). Mirrored from
        // apvts each block; added to the MIDI note before the divider write,
        // alongside pitch bend + glide. Noise channel keeps it at 0.0.
        double         detuneSemitones         = 0.0;
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

    double hostSampleRate = 44100.0;

    // Task 29 — VGM logger pointer (owned by PluginProcessor). Set once at
    // prepare time; read by writeAllChips on every PSG register write.
    VgmLogger* vgmLogger = nullptr;
};
