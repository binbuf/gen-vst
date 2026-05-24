#include <gtest/gtest.h>

#include <cmath>

#include "Tuning.h"

#ifndef GENVST_FIXTURES_TUNINGS_DIR
 #define GENVST_FIXTURES_TUNINGS_DIR "tests/fixtures/tunings"
#endif

namespace
{
    juce::String fixture (const char* name)
    {
        return juce::String (GENVST_FIXTURES_TUNINGS_DIR) + "/" + name;
    }
}

// ---- TuningTable::makeDefault --------------------------------------------------

TEST (TuningTable, DefaultMatchesEqualTemperament)
{
    auto t = TuningTable::makeDefault();
    for (int note = 0; note <= 127; ++note)
    {
        SCOPED_TRACE (note);
        const double expected = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        EXPECT_NEAR (t->freq[static_cast<std::size_t> (note)], expected, expected * 1e-9);
    }
}

TEST (TuningTable, A4Is440)
{
    auto t = TuningTable::makeDefault();
    EXPECT_NEAR (t->freq[69], 440.0, 1e-9);
}

TEST (TuningTable, LookupIntegerNoteMatchesTable)
{
    auto t = TuningTable::makeDefault();
    EXPECT_NEAR (t->lookupHz (60.0), t->freq[60], 1e-12);
    EXPECT_NEAR (t->lookupHz (69.0), 440.0, 1e-9);
}

TEST (TuningTable, LookupFractionalAppliesSemitoneOffset)
{
    auto t = TuningTable::makeDefault();
    // A fractional +0.5 semitone on A4 should give sqrt(2)^(1/12) * 440
    const double half = t->lookupHz (69.5);
    const double expected = 440.0 * std::pow (2.0, 0.5 / 12.0);
    EXPECT_NEAR (half, expected, expected * 1e-9);
}

// ---- parseScl: 12-TET fixture --------------------------------------------------

TEST (ParseScl, TwelveTetFixtureLoads)
{
    juce::String error;
    auto t = parseScl (fixture ("12tet.scl"), error);
    ASSERT_NE (t, nullptr) << error;
    EXPECT_EQ (t->description, "12-tone equal temperament");
}

TEST (ParseScl, TwelveTetMatchesDefaultTable)
{
    juce::String error;
    auto t = parseScl (fixture ("12tet.scl"), error);
    ASSERT_NE (t, nullptr) << error;

    auto def = TuningTable::makeDefault();
    for (int note = 0; note <= 127; ++note)
    {
        SCOPED_TRACE (note);
        EXPECT_NEAR (t->freq[static_cast<std::size_t> (note)],
                     def->freq[static_cast<std::size_t> (note)],
                     def->freq[static_cast<std::size_t> (note)] * 1e-6);
    }
}

// ---- parseScl: Pythagorean fixture ---------------------------------------------

TEST (ParseScl, PythagoreanFixtureLoads)
{
    juce::String error;
    auto t = parseScl (fixture ("pythagorean.scl"), error);
    ASSERT_NE (t, nullptr) << error;
    EXPECT_EQ (t->description, "Pythagorean tuning");
}

TEST (ParseScl, PythagoreanA4Is440)
{
    juce::String error;
    auto t = parseScl (fixture ("pythagorean.scl"), error);
    ASSERT_NE (t, nullptr) << error;
    EXPECT_NEAR (t->freq[69], 440.0, 1e-9);
}

TEST (ParseScl, PythagoreanOctaveDoublesFrequency)
{
    juce::String error;
    auto t = parseScl (fixture ("pythagorean.scl"), error);
    ASSERT_NE (t, nullptr) << error;

    // Any pair of notes an octave apart must have a 2:1 frequency ratio.
    for (int note = 12; note <= 115; ++note)
    {
        SCOPED_TRACE (note);
        const double low  = t->freq[static_cast<std::size_t> (note)];
        const double high = t->freq[static_cast<std::size_t> (note + 12)];
        EXPECT_NEAR (high / low, 2.0, 1e-9);
    }
}

TEST (ParseScl, PythagoreanFifthIsWiderThan12TET)
{
    juce::String error;
    auto pyth = parseScl (fixture ("pythagorean.scl"), error);
    ASSERT_NE (pyth, nullptr) << error;
    auto tet  = TuningTable::makeDefault();

    // The Pythagorean fifth (7 semitones above root) is ~701.955¢ vs 700¢.
    // So MIDI 76 (E5, +7 from A4) should be higher in Pythagorean than 12-TET.
    EXPECT_GT (pyth->freq[76], tet->freq[76]);
}

TEST (ParseScl, PythagoreanFifthRatioIsThreeHalves)
{
    juce::String error;
    auto t = parseScl (fixture ("pythagorean.scl"), error);
    ASSERT_NE (t, nullptr) << error;

    // G = 3/2 = 7 semitones above root (MIDI 69+7 = 76).
    // In one octave above A4, the 7-semitone step should be exactly 3/2 * 440.
    EXPECT_NEAR (t->freq[76], 440.0 * 3.0 / 2.0, 1e-6);
}

// ---- parseScl: lookup range MIDI 60-72 -----------------------------------------

TEST (ParseScl, LookupOctaveSpanMidi60To72)
{
    juce::String error;
    auto t = parseScl (fixture ("12tet.scl"), error);
    ASSERT_NE (t, nullptr) << error;

    for (int note = 60; note <= 72; ++note)
    {
        SCOPED_TRACE (note);
        const double hz = t->lookupHz (static_cast<double> (note));
        EXPECT_GT (hz, 0.0);
        if (note < 72)
            EXPECT_LT (t->lookupHz (static_cast<double> (note)),
                       t->lookupHz (static_cast<double> (note + 1)));
    }
}

// ---- parseScl: malformed inputs ------------------------------------------------

TEST (ParseScl, MissingFileReturnsError)
{
    juce::String error;
    auto t = parseScl (fixture ("nonexistent.scl"), error);
    EXPECT_EQ (t, nullptr);
    EXPECT_TRUE (error.isNotEmpty());
}

TEST (ParseScl, WrongDegreeCountReturnsError)
{
    // Write a temp file with 7 degrees
    const juce::File tmp = juce::File::createTempFile (".scl");
    tmp.replaceWithText (
        "! bad.scl\n"
        "Seven-note scale\n"
        "7\n"
        "200.0\n400.0\n600.0\n800.0\n1000.0\n1100.0\n1200.0\n");

    juce::String error;
    auto t = parseScl (tmp.getFullPathName(), error);
    EXPECT_EQ (t, nullptr);
    EXPECT_TRUE (error.contains ("12-degree") || error.contains ("7 degrees"));
    tmp.deleteFile();
}

TEST (ParseScl, MalformedRatioReturnsError)
{
    const juce::File tmp = juce::File::createTempFile (".scl");
    tmp.replaceWithText (
        "! bad.scl\n"
        "Bad ratio\n"
        "12\n"
        "0/0\n"   // bad ratio — numerator and denominator are zero
        "200.0\n300.0\n400.0\n500.0\n600.0\n700.0\n800.0\n900.0\n1000.0\n1100.0\n1200.0\n");

    juce::String error;
    auto t = parseScl (tmp.getFullPathName(), error);
    EXPECT_EQ (t, nullptr);
    EXPECT_TRUE (error.isNotEmpty());
    tmp.deleteFile();
}

TEST (ParseScl, TooFewDegreeLines)
{
    const juce::File tmp = juce::File::createTempFile (".scl");
    tmp.replaceWithText (
        "! bad.scl\n"
        "Truncated scale\n"
        "12\n"
        "100.0\n200.0\n");   // only 2 of 12 required degree lines

    juce::String error;
    auto t = parseScl (tmp.getFullPathName(), error);
    EXPECT_EQ (t, nullptr);
    EXPECT_TRUE (error.isNotEmpty());
    tmp.deleteFile();
}

TEST (ParseScl, MalformedLastDegreeNotOctave)
{
    const juce::File tmp = juce::File::createTempFile (".scl");
    tmp.replaceWithText (
        "! bad.scl\n"
        "Bad last degree\n"
        "12\n"
        "100.0\n200.0\n300.0\n400.0\n500.0\n600.0\n700.0\n800.0\n900.0\n1000.0\n1100.0\n"
        "1201.5\n");   // last degree is 1201.5 cents, not 1200

    juce::String error;
    auto t = parseScl (tmp.getFullPathName(), error);
    EXPECT_EQ (t, nullptr);
    EXPECT_TRUE (error.isNotEmpty());
    tmp.deleteFile();
}
