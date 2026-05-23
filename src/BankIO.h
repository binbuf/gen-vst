#pragma once

#include <vector>

#include <juce_core/juce_core.h>

// Task 24 — Bank bundle I/O.
//
// A "bank" is a snapshot of the user-curated instrument rack
// (Task 22 / 08-ui-views.md view 1 revised) plus each row's per-instrument
// routing values. Export Bank writes one of these to a JSON file the user
// picks; Import Bank reads it back and rebuilds the rack by replaying each
// row through addInstrument + loadPatch + routing-param writes.
//
// Format (single supported `version = 1`, no migration story yet — out of
// MVP scope; older bundles fail with a descriptive error):
//
//   {
//     "version": 1,
//     "rows": [
//       {
//         "type":      "fm" | "sq" | "d",
//         "slot":      <int>,
//         "patchPath": "<absolute path or empty for SQ>",
//         "routing": {
//           "midiCh":       <int 0..16>,
//           "transposeSt":  <int -24..+24>,
//           "transposeOct": <int -2..+2>,
//           "noteLo":       <int 0..127>,
//           "noteHi":       <int 0..127>,
//           "detuneCents":  <int -100..+100>,
//           "balance":      <float -1..+1>
//         }
//       },
//       ...
//     ]
//   }
//
// `type` mirrors PartManager::InstrumentType. `slot` mirrors the SlotId
// index — `0..4` for FM, `0..2` for PSG tone, `3` for PSG noise, `0` for
// DAC. `patchPath` is an absolute filesystem path (cross-OS portability
// caveat applies — see 04-patch-system.md).
//
// The struct is pure data (no JUCE-AudioProcessor coupling) so the test
// binary can roundtrip without linking the plugin.

namespace genvst::bank
{
    inline constexpr int kCurrentVersion = 1;

    struct BankRow
    {
        juce::String type;            // "fm" | "sq" | "d"
        int          slot         = 0;
        juce::String patchPath;       // absolute filesystem path; "" for SQ rows

        // Per-instrument routing — mirrors the apvts params the rack-routing
        // strip binds to. balance is the only float; everything else is int.
        int   midiCh       = 0;
        int   transposeSt  = 0;
        int   transposeOct = 0;
        int   noteLo       = 0;
        int   noteHi       = 127;
        int   detuneCents  = 0;
        float balance      = 0.0f;
    };

    struct Bank
    {
        int                  version = kCurrentVersion;
        std::vector<BankRow> rows;
    };

    // Serialise `bank` to a pretty-printed JSON string. Never fails.
    juce::String toJson (const Bank& bank);

    // Parse a JSON string. On success returns the parsed bank and leaves
    // `error` empty. On failure (malformed JSON, missing/unknown version,
    // not an object) returns a default-constructed Bank and fills `error`
    // with a user-facing message.
    Bank fromJson (const juce::String& json, juce::String& error);

    // Write `bank` as JSON to `file`. Overwrites any existing file. Returns
    // empty on success or an error message on failure.
    juce::String writeToFile (const Bank& bank, const juce::File& file);

    // Read a bank-JSON file. Returns empty on success and fills `outBank`;
    // returns an error message on failure.
    juce::String readFromFile (const juce::File& file, Bank& outBank);
}
