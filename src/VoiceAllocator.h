#pragma once

#include <array>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PatchSystem.h"
#include "Voice.h"

class VgmLogger;

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

    // Per-part polyphony mode (07-feature-spec.md "Polyphony Modes", view 10).
    struct PartPolyMode
    {
        enum class Mode : std::uint8_t { Poly = 0, Mono = 1, Unison = 2 };

        Mode   mode        = Mode::Poly;
        bool   monoLegato  = false;   // Mono only — false = Retrigger (MVP default), true = Legato
        double spreadCents = 12.0;    // Unison only — symmetric F-number fan-out (view 10 default)
        // Task 28 — portamento time in ms. Audible only in Mono+Legato; Poly /
        // Unison ignore it. 0 = instant (legacy behaviour).
        double glideTimeMs = 0.0;
    };

    VoiceAllocator() = default;

    VoiceAllocator (const VoiceAllocator&)            = delete;
    VoiceAllocator& operator= (const VoiceAllocator&) = delete;

    // Allocate working buffers and reset every voice. Call from prepareToPlay.
    void prepare (double hostSampleRate, int maxBlockSize);

    // Task 29 — propagate a VGM logger pointer to every voice in the pool.
    // Set once at prepare time by the PluginProcessor; nullptr disables VGM
    // capture on every voice.
    void setVgmLogger (VgmLogger* logger) noexcept;

    // --- Mode + voice-count configuration (Task 15) ---------------------------

    // Per-part mode + sub-mode parameters. Pushed each block from the apvts.
    // Affects only new note-ons; voices already sounding keep their original
    // mode behaviour until release.
    void setPartMode (int part, const PartPolyMode& mode) noexcept;

    // Global voice-count cap from the Settings modal (8 / 12 / 16; default 16).
    // New allocations only draw from slots [0, count); voices still sounding in
    // a slot above the cap keep playing until their natural release tail ends.
    void setVoiceCount (int count) noexcept;

    int  voiceCount() const noexcept { return currentVoiceCount; }
    PartPolyMode partMode (int part) const noexcept;

    // --- MIDI events (sample-accurate; Task 06) ------------------------------

    // Take a voice for (part, note) and key it on with `patch`. `velocity` and
    // `velToTl` drive carrier-TL scaling; `bendSemitones` is the part's
    // current pitch-wheel offset (so the new voice starts in tune with held
    // notes of the same part).
    void noteOn (int part, int note, int velocity, double bendSemitones,
                 bool velToTl, const Patch& patch);

    // Release the sounding voice matching (part, note). When `sustainHeld` is
    // true the voice is marked sustained instead — releaseSustained() lets it
    // go on pedal-up.
    void noteOff (int part, int note, bool sustainHeld);

    // Pedal-up companion to noteOff(..., sustainHeld=true): release every
    // sustained voice on the given part.
    void releaseSustained (int part);

    // Pitch-bend dispatch: update every active/released voice of `part` so its
    // frequency registers reflect the new offset, via the dirty-diff path.
    void setPitchBend (int part, double bendSemitones,
                       const Patch& patch, bool velToTl);

    // Key off every sounding voice; envelopes release naturally.
    void allNotesOff();

    // Silence and free every voice immediately, with no release tail.
    void allSoundOff();

    // --- Per-block render ----------------------------------------------------

    // Dirty-diff every sounding voice against its part's current patch. The
    // diff respects velocity -> TL (per voice) and pitch bend (per voice).
    void updateActiveVoices (const std::array<Patch, kNumParts>& partPatches,
                             bool velToTl);

    // Same as updateActiveVoices but limited to voices serving `part` — used
    // when a single CC/AT/PC event mutated just one part's patch.
    void updateActiveVoicesForPart (int part, const Patch& patch, bool velToTl);

    // Render `numSamples` of host-rate stereo audio: sum all sounding voices
    // at the native rate, then resample the mix to the host rate in one
    // pass. `ladderEnabled` selects the ymfm chip variant per voice — ym2612
    // (with +4/-3 DAC discontinuity) when true, ym3438 (clean ASIC) when
    // false. ADR-0024 toggle.
    void render (float* outL, float* outR, int numSamples, bool ladderEnabled);

    // --- Introspection (tests / telemetry) -----------------------------------

    int  numActiveVoices()    const;   // keyed on
    int  numReleasingVoices() const;   // keyed off, still ringing
    int  numIdleVoices()      const;   // free
    bool isNoteActive (int part, int note) const;

    // True iff at least one voice is currently sounding — keyed on or in the
    // release tail. Cheaper than calling numActiveVoices() + numReleasingVoices()
    // separately because it short-circuits on the first non-Idle voice. Used
    // by the FM render path to skip the chip-output / ladder-quantize chain
    // when nothing is sounding (otherwise ymfm's LSB-level idle output gets
    // amplified by the 8-bit ladder into audible background hiss).
    bool hasAudibleVoice() const noexcept;

    // 16-bit voice-activity bitmap: bit i = voice slot i is Active (keyed on,
    // not yet released). Used by the editor's header telemetry feed
    // (08-ui-views.md view 1; ADR-0010).
    std::uint32_t activeVoiceMask() const noexcept;

    // True iff any non-Idle voice is currently using FLOAT_MUL or AUTO_RETRIG
    // (the two FREQ CTRL modes that drive channel-3 special features on the
    // YM2612). Used by the HARDWARE STRICT enforcement — when the option is
    // on and a second voice asks for a ch3-special mode, the second voice
    // silently falls back to INT_MUL (07-feature-spec.md *Hardware strict*).
    bool hasActiveVoiceUsingChannel3() const noexcept;

    // Task 34 — 6-bit per-FM-part "sounding" bitmap: bit p = at least one
    // voice serving FM part p is non-Idle (Active or Released). Released
    // voices count as on until their envelope reaches silence so the per-row
    // activity LEDs decay naturally with the audible tail. Audio-thread only;
    // callers publish to the message thread through an atomic mirror.
    std::uint16_t fmPartSoundingMask() const noexcept;

    // Read-only voice view for unit tests — used to verify per-voice Unison
    // detune offsets. Out-of-range access is UB; callers should iterate over
    // [0, kNumVoices).
    const Voice& voiceAt (int index) const noexcept { return voices[(std::size_t) index]; }

private:
    // Idle voice if any, else an LRU steal: oldest Released voice, else oldest
    // Active voice. Restricted to slots [0, currentVoiceCount) per ADR-0013 /
    // 07-feature-spec.md "Polyphony" (the configurable global voice count).
    Voice& allocateVoice();

    // Mono / Unison note-on dispatch helpers — branched on partModes[part].
    void noteOnMono   (int part, int note, int velocity, double bendSemitones,
                       bool velToTl, const Patch& patch);
    void noteOnUnison (int part, int note, int velocity, double bendSemitones,
                       bool velToTl, const Patch& patch);

    // Per-voice cents-as-semitones offset for the Nth Unison voice in a stack
    // of given spread. Symmetric fan-out: 0, +1, -1, +2, -2, ... per
    // 07-feature-spec.md *Unison*.
    static double unisonVoiceDetuneSemitones (int voiceIndex, double spreadCents) noexcept;

    std::array<Voice, kNumVoices>           voices;
    std::array<PartPolyMode, kNumParts>     partModes {};
    int                                     currentVoiceCount = kNumVoices;

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
