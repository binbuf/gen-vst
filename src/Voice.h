#pragma once

#include <array>
#include <cstdint>

#include "GenVstYmfmInterface.h"
#include "PatchSystem.h"
#include "ymfm_opn.h"

// One FM voice in the shared 16-voice pool (ADR-0013).
//
// A voice owns a single ymfm::ym2612 instance driven on channel 0 only
// (ADR-0010), plus the bookkeeping the VoiceAllocator needs: which part and
// MIDI note it currently serves, a monotonically increasing "last note-on"
// timestamp for LRU stealing, and a shadow copy of its last-written register
// values so per-block parameter edits can be applied as a dirty-diff.
class Voice
{
public:
    enum class State
    {
        Idle,      // not in use — available for immediate allocation
        Active,    // keyed on and sounding
        Released   // keyed off, ringing out its release tail
    };

    Voice();

    Voice (const Voice&)            = delete;
    Voice& operator= (const Voice&) = delete;

    // Hard reset: silence the chip immediately, clear the shadow, mark Idle.
    // Used at prepare time and for all-sound-off.
    void reset();

    // Key on: apply the full note-on register sequence for `patch` at `note`,
    // seed the register shadow, and record the serving part / note / timestamp.
    // velocity / velToTl drive the carrier-TL scaling; bendSemitones is the
    // current pitch-wheel offset for the part. `voiceDetuneSemitones` is the
    // per-voice cents offset used by Unison mode — added to bend before the
    // F-number calculation. Pass 0.0 (default) outside of Unison.
    void noteOn (int part, int note, int velocity, double bendSemitones,
                 bool velToTl, const Patch& patch, std::uint64_t timestamp,
                 double voiceDetuneSemitones = 0.0);

    // Key off: release the envelope. The voice keeps sounding its release tail
    // and stays allocated (State::Released) until it is reused.
    void noteOff();

    // Mono legato: update the voice's serving note / velocity / bend and
    // refresh its frequency registers via the dirty-diff path — skipping the
    // key-off / key-on, so the envelope continues from its current level.
    // 07-feature-spec.md Mono "Legato".
    //
    // glideTimeSamples > 0 (Task 28): instead of snapping the frequency to the
    // new note, leave the voice's "current" pitch where it was and walk it
    // linearly toward the target over the given number of native-rate samples.
    // Per-block advance + register write happens in advanceGlide(). Velocity
    // / TL / patch changes still propagate immediately via updateRegisters.
    void legatoTo (int note, int velocity, double bendSemitones, bool velToTl,
                   const Patch& patch, std::uint64_t timestamp,
                   double glideTimeSamples = 0.0);

    // Advance the per-voice glide (Task 28) by `numSamples` native-rate samples
    // and re-write the YM2612 F-number registers via the dirty-diff path when
    // the interpolated pitch has moved. No-op when glide is inactive (either
    // glide_time was 0 at note-on, or the current pitch has already caught up
    // to the target). Called once per block from VoiceAllocator::render.
    void advanceGlide (int numSamples);

    // True while a glide is still in progress on this voice. Test introspection
    // for Task 28; the audio path never reads this.
    bool isGliding() const noexcept { return glideRateNotesPerSample != 0.0; }

    // Dirty-diff: re-derive the param registers from `patch` (with the voice's
    // current velocity, bend and velToTl applied) and write only the ones that
    // differ from the shadow, so a live parameter edit reaches a sounding
    // voice without retriggering its envelope.
    void updateRegisters (const Patch& patch, bool velToTl);

    // Update this voice's pitch-bend offset and refresh its frequency
    // registers via the dirty-diff path (no retrigger).
    void setPitchBend (double bendSemitones, const Patch& patch, bool velToTl);

    // CC 64 hold state. While `sustained`, a note-off arriving on this voice
    // defers the release; the VoiceAllocator releases on pedal-up.
    void markSustained() noexcept             { sustained = true; }
    void clearSustained() noexcept            { sustained = false; }
    bool isSustained() const noexcept         { return sustained; }

    // Generate `numSamples` native-rate samples, accumulating (+=) into the
    // caller's mix buffers.
    void renderAdd (float* accumL, float* accumR, int numSamples);

    // The chip's native output sample rate (~53267 Hz for the NTSC clock).
    std::uint32_t nativeSampleRate();

    State         state()     const noexcept { return voiceState; }
    bool          isIdle()    const noexcept { return voiceState == State::Idle; }
    bool          isActive()  const noexcept { return voiceState == State::Active; }
    bool          isReleasing() const noexcept { return voiceState == State::Released; }
    int           part()      const noexcept { return partIndex; }
    int           note()      const noexcept { return midiNote; }
    int           velocity()  const noexcept { return noteVelocity; }
    double        pitchBend() const noexcept { return bendSemitones; }
    double        voiceDetuneSemitones() const noexcept { return voiceDetune; }
    std::uint64_t timestamp() const noexcept { return lastNoteOnTime; }

private:
    void writeReg (std::uint8_t reg, std::uint8_t value);

    // Compute and write the YM2612 frequency-high (0xA4) + frequency-low (0xA0)
    // registers for an effective MIDI note (with bend + detune already folded
    // in). Dirty-diff'd against the shadow so a no-change call costs nothing.
    void writeFreqRegistersForMidi (double effectiveMidi);

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    State         voiceState     = State::Idle;
    int           partIndex      = -1;
    int           midiNote       = -1;
    int           noteVelocity   = 127;
    double        bendSemitones  = 0.0;
    // Per-voice cents-as-semitones offset (Unison F-number spread, view 10).
    // Added to bendSemitones before the F-number calculation, so the part's
    // pitch wheel and the per-voice detune stay independent.
    double        voiceDetune    = 0.0;
    bool          sustained      = false;
    std::uint64_t lastNoteOnTime = 0;

    // Task 28 — portamento / glide state. glideCurrentMidi is the actually-
    // sounding MIDI note (fractional), glideTargetMidi is the destination
    // (always integer in practice, since legatoTo passes int notes), and
    // glideRateNotesPerSample is the signed semitone delta added each native-
    // rate sample while the glide is active. When current == target the rate
    // is held at 0.0 so isGliding() returns false. In the non-glide path
    // (poly mode, unison, fresh note-on) glideCurrentMidi tracks midiNote so
    // updateRegisters() produces identical output to the pre-Task-28 code.
    double        glideCurrentMidi        = 0.0;
    double        glideTargetMidi         = 0.0;
    double        glideRateNotesPerSample = 0.0;

    // Last value written to each bank-0 register, indexed by register address;
    // -1 means "never written". Diffed each block by updateRegisters so only
    // changed registers are re-sent (01-architecture.md "Parameter System").
    std::array<int, 256> shadow {};
};
