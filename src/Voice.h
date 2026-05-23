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
    void noteOn (int part, int note, const Patch& patch, std::uint64_t timestamp);

    // Key off: release the envelope. The voice keeps sounding its release tail
    // and stays allocated (State::Released) until it is reused.
    void noteOff();

    // Dirty-diff: re-derive the param registers from `patch` and write only the
    // ones that differ from the shadow, so a live parameter edit reaches a
    // sounding voice without retriggering its envelope.
    void updateRegisters (const Patch& patch);

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
    std::uint64_t timestamp() const noexcept { return lastNoteOnTime; }

private:
    void writeReg (std::uint8_t reg, std::uint8_t value);

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    State         voiceState     = State::Idle;
    int           partIndex      = -1;
    int           midiNote       = -1;
    std::uint64_t lastNoteOnTime = 0;

    // Last value written to each bank-0 register, indexed by register address;
    // -1 means "never written". Diffed each block by updateRegisters so only
    // changed registers are re-sent (01-architecture.md "Parameter System").
    std::array<int, 256> shadow {};
};
