#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "PatchSystem.h"

// One ymfm register write: an address byte followed by a data byte. A note-on
// is produced as an ordered list of these, so the exact sequence can be
// captured and asserted by a unit test without a live chip; the render engine
// simply replays the list onto its ymfm::ym2612 instance.
//
// Channel 3 Special / CSM mode writes split across Bank 0 (chip ports 0/1) and
// the bank-1-targeting register block — every register in 0x30..0xAF can be
// addressed for ch4..6 by adding 0x100 to the address. `regBank == 0` writes
// via chip ports 0/1; `regBank == 1` writes via chip ports 2/3. The v2 FM
// engine only writes ch3 special-mode registers (which all live in bank 0), so
// the existing tests + code do not need to change.
struct RegWrite
{
    uint8_t reg   = 0;
    uint8_t value = 0;
};

constexpr bool operator== (const RegWrite& a, const RegWrite& b)
{
    return a.reg == b.reg && a.value == b.value;
}

// Turns a Patch + MIDI note into the YM2612 register writes for a note-on.
// Pure integer math — no ymfm dependency — so it is trivially unit-testable.
namespace FmRegisterMap
{
    // Patch operator index (0 = OP1/S1 .. 3 = OP4/S4) -> hardware register
    // offset. The YM2612 lays operators out S1 +0x00, S3 +0x04, S2 +0x08,
    // S4 +0x0C: S2 and S3 are swapped relative to their numbers.
    inline constexpr std::array<uint8_t, 4> kOperatorRegOffset { 0x00, 0x08, 0x04, 0x0C };

    // Channel 3 per-operator F-number register table for the YM2612 Channel 3
    // Special / CSM modes (02-fm-synthesis.md § *FREQ Control Mode*). The
    // ch3-special F-number registers are not on the regular 0xA0/0xA4 layout
    // — each operator has its own (HIGH byte, LOW byte) pair in bank 0:
    //   OP1 / S1 -> HIGH 0xA6, LOW 0xA2  (ch3's normal frequency registers)
    //   OP2 / S2 -> HIGH 0xAD, LOW 0xA9
    //   OP3 / S3 -> HIGH 0xAC, LOW 0xA8
    //   OP4 / S4 -> HIGH 0xAE, LOW 0xAA
    // Per Patch operator-index ordering (S1, S2, S3, S4); the mapping mirrors
    // 02-fm-synthesis.md *Channel 3 Special Mode Frequencies*.
    inline constexpr std::array<uint8_t, 4> kChannel3OpFreqHigh { 0xA6, 0xAD, 0xAC, 0xAE };
    inline constexpr std::array<uint8_t, 4> kChannel3OpFreqLow  { 0xA2, 0xA9, 0xA8, 0xAA };

    // Order operators are written in during a note-on: S1, S3, S2, S4 —
    // ascending register offset. Values are Patch operator indices.
    inline constexpr std::array<int, 4> kOperatorWriteOrder { 0, 2, 1, 3 };

    // A note-on is always this many (register, value) writes:
    // 1 key-off + 1 LFO + 4 operators x 7 registers + 2 channel + 2 frequency
    // + 1 key-on.
    inline constexpr int kNoteOnWriteCount = 35;

    // Channel 3 mode select values for the 0x27 register's bits 7:6.
    //   00 = normal (default), 01 = Special, 11 = CSM.
    enum class Channel3Mode : std::uint8_t
    {
        Normal  = 0x00,
        Special = 0x40,    // bit 6
        Csm     = 0xC0,    // bits 7:6
    };

    // Per-algorithm carrier mask: bit i set means OP(i+1) is a carrier. Used
    // to scope velocity -> TL scaling to carriers only — modulator TL controls
    // timbre, not output level, and must stay at the patch value.
    inline constexpr std::array<uint8_t, 8> kCarrierMaskByAlg
    {
        0b1000, 0b1000, 0b1000, 0b1000,   // alg 0-3: only OP4 sounds
        0b1010,                             // alg 4:    OP2 + OP4
        0b1110,                             // alg 5:    OP2 + OP3 + OP4
        0b1110,                             // alg 6:    OP2 + OP3 + OP4
        0b1111                              // alg 7:    all four carriers
    };

    // YM2612 frequency register fields for one note.
    struct FreqRegs
    {
        int blk;   // 0-7:    block / octave
        int fnum;  // 0-2047: F-number
    };

    // Convert a MIDI note (with an optional fractional semitone offset for
    // pitch bend) to (BLK, F-number). BLK is chosen so the F-number stays
    // inside 0x000-0x7FF; notes above the chip's range clamp to the top.
    FreqRegs midiNoteToFreq (double midiNote);

    // Convert a TFI detune value (0-6) to the YM2612 register field (0x30
    // bits 6:4). TFI: 0-3 = none/+detune, 4-6 = -detune. Hardware: 0-3 =
    // none/+detune, 4 = "same as 0" (unused), 5-7 = -detune — so TFI 4-6
    // shift up by one to land on hardware 5-7.
    uint8_t detuneToRegister (uint8_t tfiDetune);

    // Apply the velocity -> carrier-TL formula. Modulator operators always
    // pass through unchanged; for carriers (per kCarrierMaskByAlg), the TL is
    // raised by (127 - velocity) / 2 — a half-range attenuation that's quiet
    // but never wholly silent at v=1, audible at v=127 (no change). With
    // velToTl == false the patch TL is returned untouched.
    uint8_t scaleCarrierTl (uint8_t patchTl, int patchAlg, int opIndex,
                            int velocity, bool velToTl) noexcept;

    // Runtime per-voice modulations layered on top of the patch.
    struct NoteParams
    {
        int    velocity       = 127;   // 0-127; only affects carrier TL when velToTl
        bool   velToTl        = false; // velocity -> carrier TL scaling toggle
        double bendSemitones  = 0.0;   // pitch-wheel offset, signed; 0 = no bend
    };

    // Build the full ordered note-on register sequence for a patch at a note,
    // with optional runtime modulations (velocity scaling + pitch bend).
    std::array<RegWrite, kNoteOnWriteCount> buildNoteOn (const Patch& patch,
                                                         int midiNote,
                                                         NoteParams params = {});

    // The single key-off write (channel 0, all operators off).
    RegWrite buildKeyOff();

    // --- v2 helpers (Task 05) -----------------------------------------------

    // Convert a UI-side level to a hardware attenuation. Level 0 = silent
    // (register value = max), level max = loudest (register value = 0).
    // Negative levels clamp to 0 (silent); levels above max clamp to max
    // (loudest). See 02-fm-synthesis.md § *UI level vs hardware attenuation*.
    int levelToAttenuation (int level, int maxAttenuation) noexcept;

    // Compose the channel-3 mode byte for register 0x27. `mode` sets bits 7:6;
    // the low six bits (timer EN / LOAD / RST) come from `timerBits`.
    std::uint8_t composeRegister27 (Channel3Mode mode, std::uint8_t timerBits) noexcept;

    // Pack a TimerA 10-bit value into the YM2612's two-register split:
    // 0x24 = bits 9:2 (high byte), 0x25 = bits 1:0 (low 2 bits).
    struct TimerAWrites
    {
        std::uint8_t high;  // value for 0x24
        std::uint8_t low;   // value for 0x25
    };
    TimerAWrites buildTimerA (int retrigRate) noexcept;

    // Convert a frequency in Hz to (BLK, F-number) using the YM2612's native
    // sample rate (NTSC). Same math as midiNoteToFreq but skipping the
    // MIDI-note → Hz step, for `fixed[op]` operators in FLOAT_MUL / AUTO_RETRIG.
    FreqRegs hzToFreq (double hz) noexcept;

    // Velocity → TL layering combined formula (02-fm-synthesis.md *Velocity →
    // TL layering*). Stacks the v1 global "velocity_to_tl" carrier-attenuation
    // term and the per-op `velPerOp` term on top of the patch TL × channel_tl
    // attenuation. Returns a 7-bit register value clamped to [0, 127].
    //
    // Arguments:
    //  * patchTlAttenuation -- the hardware attenuation in the Patch (0 = loud,
    //    127 = silent); v1 round-trip target for TFI/VGI/DMP/Y12/OPM.
    //  * channelTl -- 0.0..1.0 master multiplier; folded onto the per-op
    //    attenuation as additional attenuation when < 1.0.
    //  * patchAlg -- 0..7; used to scope the global v1 velocity term to
    //    carriers only.
    //  * opIndex -- 0..3.
    //  * velocity -- 0..127 latched at key-on; layered into both terms.
    //  * velToTl -- the global v1 carrier-velocity toggle.
    //  * velPerOp -- 0.0..1.0 per-op velocity depth (Patch::vel[op]).
    std::uint8_t composeTl (std::uint8_t patchTlAttenuation,
                            float        channelTl,
                            int          patchAlg,
                            int          opIndex,
                            int          velocity,
                            bool         velToTl,
                            float        velPerOp) noexcept;

    // Build the FLOAT_MUL note-on sequence: channel 3 Special mode + per-op
    // F-numbers (note × mul_float[op] or freq_fixed_hz[op] if fixed[op]). All
    // ch3 op / channel registers; ends with a regular 0x28 key-on with the
    // channel-3 OPS mask (channel-select value 2).
    std::vector<RegWrite> buildNoteOnFloatMul (const Patch& patch,
                                               int          midiNote,
                                               NoteParams   params = {});

    // Build the AUTO_RETRIG note-on sequence: channel 3 CSM + per-op
    // F-numbers + TimerA (split across 0x24 / 0x25) + 0x27 LOAD/EN/RST. No
    // 0x28 key-on — the TimerA-driven CSM auto-fires the internal key-on/off
    // pair. Operators must have non-zero RR so each auto-keyed event has
    // audible release (manual page 15).
    std::vector<RegWrite> buildNoteOnAutoRetrig (const Patch& patch,
                                                 int          midiNote,
                                                 NoteParams   params = {});

    // The single key-off write for AUTO_RETRIG: clears the TimerA LOAD bit so
    // the auto-retrigger stops, plus a standard channel-3 key-off. Returned
    // as a fixed pair: { 0x27 (mode bits + cleared timer), 0x28 (ch3 key-off) }.
    std::array<RegWrite, 2> buildKeyOffAutoRetrig() noexcept;

    // The channel-3 key-off (FLOAT_MUL path). Channel select value 2 in 0x28.
    RegWrite buildKeyOffCh3() noexcept;
}
