#pragma once

#include <array>
#include <cstdint>

#include "GenVstYmfmInterface.h"
#include "PatchSystem.h"
#include "ymfm_opn.h"

class VgmLogger;

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

    // FREQ CTRL MODE (02-fm-synthesis.md *FREQ Control Mode*). Snapshotted on
    // each note-on from the apvts patch; selects which YM2612 channel +
    // register path the voice uses.
    enum class FreqCtrlMode : std::uint8_t
    {
        IntMul      = 0,   // channel 0, shared F-number per voice, MUL per op
        FloatMul    = 1,   // channel 3 special, per-op F-numbers from mul_float
        AutoRetrig  = 2,   // channel 3 CSM + TimerA-driven auto-retrigger
    };

    // Hard reset: silence the chip immediately, clear the shadow, mark Idle.
    // Used at prepare time and for all-sound-off.
    void reset();

    // Key on: apply the full note-on register sequence for `patch` at `note`,
    // seed the register shadow, and record the serving part / note / timestamp.
    // velocity / velToTl drive the carrier-TL scaling; bendSemitones is the
    // current pitch-wheel offset for the part. `voiceDetuneSemitones` is the
    // per-voice cents offset used by Unison mode — added to bend before the
    // F-number calculation. Pass 0.0 (default) outside of Unison.
    //
    // The FREQ CTRL MODE is taken from `patch.freq_ctrl_mode` at this call;
    // each note-on can use a different mode on the same voice (e.g. the user
    // flips the panel selector mid-playback). The voice retains the mode
    // snapshot so the matching key-off / dirty-diff path runs.
    void noteOn (int part, int note, int velocity, double bendSemitones,
                 bool velToTl, const Patch& patch, std::uint64_t timestamp,
                 double voiceDetuneSemitones = 0.0);

    // Key off: release the envelope. The voice keeps sounding its release tail
    // and stays allocated (State::Released) until it is reused. Routes to the
    // matching key-off register sequence for the voice's currentMode (ch3 in
    // FLOAT_MUL / AUTO_RETRIG; ch0 in INT_MUL — the v1 path).
    void noteOff();

    // Mono legato: update the voice's serving note / velocity / bend and
    // refresh its frequency registers via the dirty-diff path — skipping the
    // key-off / key-on, so the envelope continues from its current level.
    // 07-feature-spec.md Mono "Legato"; 02-fm-synthesis.md *Voice handling*.
    //
    // glideTimeSamples > 0 (Task 28): instead of snapping the frequency to the
    // new note, leave the voice's "current" pitch where it was and walk it
    // linearly toward the target over the given number of native-rate samples.
    // Per-block advance + register write happens in advanceGlide(). Velocity
    // / TL / patch changes still propagate immediately via updateRegisters.
    void legatoTo (int note, int velocity, double bendSemitones, bool velToTl,
                   const Patch& patch, std::uint64_t timestamp,
                   double glideTimeSamples = 0.0);

    // Voice's last-snapshotted FREQ CTRL MODE (read-only). Used by the
    // VoiceAllocator's key-off path so the matching ch3 / ch0 sequence runs.
    FreqCtrlMode currentMode() const noexcept { return freqCtrlMode; }

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

    // Kit mode (ADR-0021 amendment): re-diff this voice against the patch it
    // was keyed with — not a single shared part patch. A drum kit keys each
    // pad with its own FM patch, so the per-block refresh must use each voice's
    // own patch or one pad's update would clobber another's registers.
    void refreshFromKeyedPatch (bool velToTl) { updateRegisters (keyedPatch, velToTl); }

    // The patch this voice was last keyed (or legato'd) with. Read-only — used
    // by the kit refresh path above and by tests.
    const Patch& keyedPatchSnapshot() const noexcept { return keyedPatch; }

    // CC 64 hold state. While `sustained`, a note-off arriving on this voice
    // defers the release; the VoiceAllocator releases on pedal-up.
    void markSustained() noexcept             { sustained = true; }
    void clearSustained() noexcept            { sustained = false; }
    bool isSustained() const noexcept         { return sustained; }

    // Generate `numSamples` native-rate samples, accumulating (+=) into the
    // caller's mix buffers. When `ladderEnabled` is true the YM2612's per-
    // channel +4/-3 DAC discontinuity is applied via ymfm's ym2612::generate;
    // when false, ym3438::generate is used for a clean ASIC-style output.
    void renderAdd (float* accumL, float* accumR, int numSamples, bool ladderEnabled);

    // The chip's native output sample rate (~53267 Hz for the NTSC clock).
    std::uint32_t nativeSampleRate();

    // Task 29 — install a VGM logger pointer so every register write from
    // this voice gets mirrored into the on-disk capture (Voice::writeReg
    // checks logger->isActive() before forwarding). nullptr disables logging.
    void setVgmLogger (VgmLogger* logger) noexcept { vgmLogger = logger; }

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
    // Stored as ym3438 (no built-in DAC discontinuity); when the Ladder Effect
    // toggle is on, renderAdd calls chip.ym2612::generate() to invoke the base
    // class's +4/-3 dac_discontinuity. ym3438 inherits from ym2612 with no
    // extra state — only generate() differs. See ymfm_opn.h:766 + .cpp:2398.
    ymfm::ym3438        chip { interface };

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

    // The patch this voice was last keyed / legato'd with — retained so kit
    // mode can re-diff each sounding voice against its OWN patch
    // (refreshFromKeyedPatch). In single-patch mode the processor still drives
    // updates via updateRegisters(currentPatch, ...) and this copy is unused.
    Patch keyedPatch {};

    // The FREQ CTRL MODE this voice is running under right now — set at
    // every note-on from the patch snapshot. Determines which YM2612 channel
    // the voice writes to (0 for INT_MUL; 3 for FLOAT_MUL / AUTO_RETRIG) and
    // therefore which key-off / dirty-diff path runs.
    FreqCtrlMode freqCtrlMode = FreqCtrlMode::IntMul;

    // Task 29 — VGM logger pointer (owned by PluginProcessor). Set once at
    // prepare time; read by writeReg on every chip write. nullptr-safe.
    VgmLogger* vgmLogger = nullptr;

    // Consecutive native-rate samples of exactly-zero ymfm output while in
    // Released state. Reaches kAutoIdleThreshold → voice auto-idles, stopping
    // the chip tick that would otherwise feed hiss into the LadderEffect.
    int silentNativeSamples = 0;
};
