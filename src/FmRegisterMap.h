#pragma once

#include <array>
#include <cstdint>

#include "PatchSystem.h"

// One ymfm register write: an address byte followed by a data byte. A note-on
// is produced as an ordered list of these, so the exact sequence can be
// captured and asserted by a unit test without a live chip; the render engine
// simply replays the list onto its ymfm::ym2612 instance.
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

    // Order operators are written in during a note-on: S1, S3, S2, S4 —
    // ascending register offset. Values are Patch operator indices.
    inline constexpr std::array<int, 4> kOperatorWriteOrder { 0, 2, 1, 3 };

    // A note-on is always this many (register, value) writes:
    // 1 key-off + 1 LFO + 4 operators x 7 registers + 2 channel + 2 frequency
    // + 1 key-on.
    inline constexpr int kNoteOnWriteCount = 35;

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
}
