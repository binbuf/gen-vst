#include <gtest/gtest.h>

#include "FmRegisterMap.h"

namespace
{
    // Reconstruct the frequency (Hz) the chip plays for a (BLK, F-number)
    // pair — the inverse of the F-number formula.
    double effectiveHz (FmRegisterMap::FreqRegs f)
    {
        return f.fnum * 53267.0 / static_cast<double> (1 << (21 - f.blk));
    }
}

// A4 (MIDI 69, 440 Hz) -> BLK 4, F-number ~0x43B. 0x43B (1083) is what the
// standard formula produces; the design docs' "0x28A" was a typo.
TEST (FrequencyCalc, A4LandsOnBlock4)
{
    const auto f = FmRegisterMap::midiNoteToFreq (69);
    EXPECT_EQ (f.blk, 4);
    EXPECT_NEAR (f.fnum, 0x43B, 1);
}

// Every MIDI note must yield an F-number inside the 11-bit hardware range
// and a block inside the 3-bit range.
TEST (FrequencyCalc, AllMidiNotesStayInRange)
{
    for (int note = 0; note <= 127; ++note)
    {
        SCOPED_TRACE (note);
        const auto f = FmRegisterMap::midiNoteToFreq (note);
        EXPECT_GE (f.fnum, 0x000);
        EXPECT_LE (f.fnum, 0x7FF);
        EXPECT_GE (f.blk, 0);
        EXPECT_LE (f.blk, 7);
    }
}

// Raising a note by one octave doubles the effective frequency.
TEST (FrequencyCalc, OctaveDoublesFrequency)
{
    for (int note = 36; note + 12 <= 108; ++note)
    {
        SCOPED_TRACE (note);
        const double low  = effectiveHz (FmRegisterMap::midiNoteToFreq (note));
        const double high = effectiveHz (FmRegisterMap::midiNoteToFreq (note + 12));
        EXPECT_NEAR (high / low, 2.0, 0.02);
    }
}
