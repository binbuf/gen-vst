// Task 24 — Bank bundle JSON roundtrip tests.
//
// Covers the BankIO serialiser used by the IMPORT-tab Export Bank / Import
// Bank buttons. The serialiser is pure data — no JUCE-AudioProcessor
// coupling — so these tests stand alone without spinning up a plugin.

#include <gtest/gtest.h>

#include "BankIO.h"

namespace bank = genvst::bank;

namespace
{
    // Build a small fixture covering each row type. Keeps the per-test
    // boilerplate down and pins down exactly which fields get exercised
    // across the roundtrip.
    bank::Bank makeFixture()
    {
        bank::Bank b;
        b.version = bank::kCurrentVersion;

        bank::BankRow fm;
        fm.type         = "fm";
        fm.slot         = 0;
        fm.patchPath    = "C:/patches/lead.tfi";
        fm.midiCh       = 1;
        fm.transposeSt  = 7;
        fm.transposeOct = -1;
        fm.noteLo       = 24;
        fm.noteHi       = 96;
        fm.detuneCents  = 12;
        fm.balance      = -0.25f;
        b.rows.push_back (fm);

        bank::BankRow sq;
        sq.type         = "sq";
        sq.slot         = 2;
        sq.patchPath    = "";       // SQ has no patch
        sq.midiCh       = 13;
        sq.transposeSt  = 0;
        sq.transposeOct = 0;
        sq.noteLo       = 0;
        sq.noteHi       = 127;
        sq.detuneCents  = -5;
        sq.balance      = 0.5f;
        b.rows.push_back (sq);

        bank::BankRow d;
        d.type        = "d";
        d.slot        = 0;
        d.patchPath   = "";       // DAC sample lives in plugin state, not bank
        d.midiCh      = 16;
        d.balance     = 0.0f;
        b.rows.push_back (d);

        return b;
    }
}

// -- roundtrip via toJson/fromJson ------------------------------------------

TEST (BankIO, RoundTripsMultiRowBank)
{
    const auto original = makeFixture();
    const auto json     = bank::toJson (original);
    ASSERT_FALSE (json.isEmpty());

    juce::String err;
    const auto parsed = bank::fromJson (json, err);

    ASSERT_TRUE (err.isEmpty()) << err.toStdString();
    ASSERT_EQ (parsed.version, original.version);
    ASSERT_EQ (parsed.rows.size(), original.rows.size());

    for (std::size_t i = 0; i < original.rows.size(); ++i)
    {
        const auto& a = original.rows[i];
        const auto& b = parsed.rows[i];
        EXPECT_EQ (a.type, b.type);
        EXPECT_EQ (a.slot, b.slot);
        EXPECT_EQ (a.patchPath, b.patchPath);
        EXPECT_EQ (a.midiCh, b.midiCh);
        EXPECT_EQ (a.transposeSt, b.transposeSt);
        EXPECT_EQ (a.transposeOct, b.transposeOct);
        EXPECT_EQ (a.noteLo, b.noteLo);
        EXPECT_EQ (a.noteHi, b.noteHi);
        EXPECT_EQ (a.detuneCents, b.detuneCents);
        EXPECT_FLOAT_EQ (a.balance, b.balance);
    }
}

// -- empty-bank edge case --------------------------------------------------
// Save State with an empty rack must still produce a valid file (per task
// spec: "Export Bank with empty rack toasts and aborts" only at the editor
// layer — the JSON serialiser itself must round-trip cleanly).

TEST (BankIO, EmptyBankRoundTrips)
{
    bank::Bank empty;
    const auto json = bank::toJson (empty);
    ASSERT_FALSE (json.isEmpty());

    juce::String err;
    const auto parsed = bank::fromJson (json, err);
    ASSERT_TRUE (err.isEmpty());
    EXPECT_EQ (parsed.version, bank::kCurrentVersion);
    EXPECT_TRUE (parsed.rows.empty());
}

// -- malformed input rejected with a descriptive error --------------------

TEST (BankIO, RejectsMalformedJson)
{
    juce::String err;
    const auto parsed = bank::fromJson ("{not really json", err);
    EXPECT_FALSE (err.isEmpty());
    EXPECT_TRUE (parsed.rows.empty());
}

TEST (BankIO, RejectsUnknownVersion)
{
    juce::String err;
    const auto parsed = bank::fromJson ("{\"version\": 99, \"rows\": []}", err);
    EXPECT_FALSE (err.isEmpty());
    EXPECT_TRUE (parsed.rows.empty());
}

TEST (BankIO, RejectsNonObjectRoot)
{
    juce::String err;
    const auto parsed = bank::fromJson ("[1, 2, 3]", err);
    EXPECT_FALSE (err.isEmpty());
    EXPECT_TRUE (parsed.rows.empty());
}

// -- file write/read roundtrip --------------------------------------------

TEST (BankIO, FileWriteAndReadRoundTrip)
{
    const auto original = makeFixture();
    const juce::File tmp = juce::File::createTempFile ("genvst-banktest.gnbank");
    // RAII deleter so a failed assertion doesn't leak the temp file.
    struct Cleanup { juce::File f; ~Cleanup() { f.deleteFile(); } } cleanup { tmp };

    const auto writeErr = bank::writeToFile (original, tmp);
    ASSERT_TRUE (writeErr.isEmpty()) << writeErr.toStdString();
    ASSERT_TRUE (tmp.existsAsFile());

    bank::Bank parsed;
    const auto readErr = bank::readFromFile (tmp, parsed);
    ASSERT_TRUE (readErr.isEmpty()) << readErr.toStdString();

    ASSERT_EQ (parsed.rows.size(), original.rows.size());
    EXPECT_EQ (parsed.rows[0].type, "fm");
    EXPECT_EQ (parsed.rows[0].midiCh, original.rows[0].midiCh);
}

TEST (BankIO, ReadingMissingFileReturnsError)
{
    bank::Bank parsed;
    const auto err = bank::readFromFile (juce::File ("/does/not/exist.gnbank"),
                                         parsed);
    EXPECT_FALSE (err.isEmpty());
}
