#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "PsgPreset.h"

#ifndef GENVST_FACTORY_PATCHES_DIR
 #error "GENVST_FACTORY_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path tempPath (const std::string& stem)
    {
        const auto dir = fs::temp_directory_path() / "genvst-psg-tests";
        fs::create_directories (dir);
        return dir / (stem + ".psg");
    }

    void writeText (const fs::path& path, const std::string& text)
    {
        std::ofstream f (path, std::ios::binary | std::ios::trunc);
        f << text;
    }
}

// Round-trip: save → load → compare every field.
TEST (PsgPreset, RoundTripPreservesEveryField)
{
    PsgPreset original;
    original.version = 1;
    original.name    = "Soft Lead";

    original.tones[0] = { /*atk*/ 8, /*dr1*/ 4, /*sus*/ 12, /*dr2*/ 0,
                          /*rr*/ 6, /*vol*/ 0.8f, /*pan*/ -0.3f, /*detune*/ 0 };
    original.tones[1] = { 8, 4, 12, 0, 6, 0.8f, 0.3f, 7 };
    original.tones[2] = { 0, 0, 15, 0, 0, 0.0f, 0.0f, 0 };
    original.noise    = { 0, 0, 15, 0, 0, 0.0f, 0.0f, 0 };
    original.noiseType = "periodic";
    original.noiseRate = "ch2";

    const auto path = tempPath ("roundtrip");
    const auto saveErr = savePsgPreset (original, path);
    ASSERT_TRUE (saveErr.empty()) << saveErr;

    const auto loaded = loadPsgPreset (path);
    ASSERT_TRUE (loaded.preset.has_value()) << loaded.error;
    EXPECT_EQ (loaded.preset->version, 1);
    EXPECT_EQ (loaded.preset->name,    "Soft Lead");

    for (int i = 0; i < PsgPreset::kNumTones; ++i)
    {
        SCOPED_TRACE (i);
        const auto& a = original.tones[(std::size_t) i];
        const auto& b = loaded.preset->tones[(std::size_t) i];
        EXPECT_EQ (b.atk, a.atk);
        EXPECT_EQ (b.dr1, a.dr1);
        EXPECT_EQ (b.sus, a.sus);
        EXPECT_EQ (b.dr2, a.dr2);
        EXPECT_EQ (b.rr,  a.rr);
        EXPECT_FLOAT_EQ (b.vol, a.vol);
        EXPECT_FLOAT_EQ (b.pan, a.pan);
        EXPECT_EQ (b.detune, a.detune);
    }
    EXPECT_EQ (loaded.preset->noise.atk, original.noise.atk);
    EXPECT_EQ (loaded.preset->noise.sus, original.noise.sus);
    EXPECT_EQ (loaded.preset->noiseType, "periodic");
    EXPECT_EQ (loaded.preset->noiseRate, "ch2");
}

// Out-of-range integer values clamp into the apvts hardware ranges.
TEST (PsgPreset, OutOfRangeValuesClamp)
{
    const std::string json = R"({
      "version": 1,
      "name": "Out Of Range",
      "channels": {
        "tone1": { "atk": 999, "dr1": -5, "sus": 100, "dr2": -1, "rr": 999,
                   "vol": 5.0, "pan": -3.5, "detune": 500 },
        "tone2": { "atk": 8, "dr1": 4, "sus": 12, "dr2": 0, "rr": 6,
                   "vol": 1.0, "pan": 0.3, "detune": 0 },
        "tone3": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
                   "vol": 0.0, "pan": 0.0, "detune": 0 },
        "noise": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
                   "vol": 0.0, "pan": 0.0,
                   "type": "white", "rate": "mid" }
      }
    })";
    const auto path = tempPath ("clamp");
    writeText (path, json);

    const auto loaded = loadPsgPreset (path);
    ASSERT_TRUE (loaded.preset.has_value()) << loaded.error;

    const auto& t = loaded.preset->tones[0];
    EXPECT_EQ (t.atk,    31);                     // clamped to atk max
    EXPECT_EQ (t.dr1,    0);                      // negative clamps to 0
    EXPECT_EQ (t.sus,    15);                     // clamped to sus max
    EXPECT_EQ (t.dr2,    0);
    EXPECT_EQ (t.rr,     15);
    EXPECT_FLOAT_EQ (t.vol,    1.0f);             // > 1 clamps to 1
    EXPECT_FLOAT_EQ (t.pan,   -1.0f);             // < -1 clamps to -1
    EXPECT_EQ (t.detune, 100);                    // clamped to detune max
}

// Missing fields fall back to defaults instead of failing.
TEST (PsgPreset, MissingFieldsDefault)
{
    const std::string json = R"({
      "version": 1,
      "name": "Sparse",
      "channels": {
        "tone1": { "atk": 5 }
      }
    })";
    const auto path = tempPath ("sparse");
    writeText (path, json);

    const auto loaded = loadPsgPreset (path);
    ASSERT_TRUE (loaded.preset.has_value()) << loaded.error;
    EXPECT_EQ (loaded.preset->tones[0].atk, 5);
    EXPECT_EQ (loaded.preset->tones[0].dr1, 0);
    EXPECT_FLOAT_EQ (loaded.preset->tones[0].vol, 1.0f);     // default
    EXPECT_FLOAT_EQ (loaded.preset->tones[0].pan, 0.0f);     // default
    EXPECT_EQ (loaded.preset->noiseType, "white");
    EXPECT_EQ (loaded.preset->noiseRate, "mid");
}

// Unparseable JSON returns an error result with no preset.
TEST (PsgPreset, UnparseableFileReturnsError)
{
    const auto path = tempPath ("bad");
    writeText (path, "this is not json {{{");
    const auto loaded = loadPsgPreset (path);
    EXPECT_FALSE (loaded.preset.has_value());
    EXPECT_FALSE (loaded.error.empty());
}

// Unsupported schema version is rejected.
TEST (PsgPreset, UnsupportedVersionRejected)
{
    const auto path = tempPath ("v2");
    writeText (path, R"({ "version": 2, "name": "Future" })");
    const auto loaded = loadPsgPreset (path);
    EXPECT_FALSE (loaded.preset.has_value());
    EXPECT_FALSE (loaded.error.empty());
}

// Unknown noise type / rate strings clamp to defaults (typo-tolerant load).
TEST (PsgPreset, UnknownNoiseEnumsClampToDefaults)
{
    const std::string json = R"({
      "version": 1,
      "name": "Bogus Noise",
      "channels": {
        "noise": { "type": "purple", "rate": "ultra" }
      }
    })";
    const auto path = tempPath ("noise-clamp");
    writeText (path, json);

    const auto loaded = loadPsgPreset (path);
    ASSERT_TRUE (loaded.preset.has_value()) << loaded.error;
    EXPECT_EQ (loaded.preset->noiseType, "white");
    EXPECT_EQ (loaded.preset->noiseRate, "mid");
}

// Missing file returns an error rather than throwing.
TEST (PsgPreset, MissingFileReturnsError)
{
    const auto loaded = loadPsgPreset (fs::path { "Z:/no-such-folder/no-file.psg" });
    EXPECT_FALSE (loaded.preset.has_value());
    EXPECT_FALSE (loaded.error.empty());
}

// All 12 factory `.psg` presets under extern/patches/sq/ parse without errors
// and produce clamped, in-range values. Guards against typos / schema drift in
// the factory bank (Task 10).
TEST (PsgPreset, AllFactorySqPresetsLoad)
{
    const fs::path sqDir = fs::path (GENVST_FACTORY_PATCHES_DIR) / "sq";
    ASSERT_TRUE (fs::is_directory (sqDir)) << sqDir;

    int count = 0;
    for (const auto& entry : fs::directory_iterator (sqDir))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".psg")
            continue;

        ++count;
        SCOPED_TRACE (entry.path().filename().string());
        const auto loaded = loadPsgPreset (entry.path());
        ASSERT_TRUE (loaded.preset.has_value()) << loaded.error;

        const auto& p = *loaded.preset;
        EXPECT_EQ (p.version, 1);
        EXPECT_FALSE (p.name.empty());

        for (int i = 0; i < PsgPreset::kNumTones; ++i)
        {
            SCOPED_TRACE (i);
            const auto& t = p.tones[(std::size_t) i];
            EXPECT_GE (t.atk,    0); EXPECT_LE (t.atk,   31);
            EXPECT_GE (t.dr1,    0); EXPECT_LE (t.dr1,   31);
            EXPECT_GE (t.sus,    0); EXPECT_LE (t.sus,   15);
            EXPECT_GE (t.dr2,    0); EXPECT_LE (t.dr2,   31);
            EXPECT_GE (t.rr,     0); EXPECT_LE (t.rr,    15);
            EXPECT_GE (t.vol, 0.0f); EXPECT_LE (t.vol, 1.0f);
            EXPECT_GE (t.pan,-1.0f); EXPECT_LE (t.pan, 1.0f);
            EXPECT_GE (t.detune, -100); EXPECT_LE (t.detune, 100);
        }

        EXPECT_GE (p.noise.atk, 0); EXPECT_LE (p.noise.atk, 31);
        EXPECT_GE (p.noise.dr1, 0); EXPECT_LE (p.noise.dr1, 31);
        EXPECT_GE (p.noise.sus, 0); EXPECT_LE (p.noise.sus, 15);
        EXPECT_GE (p.noise.vol, 0.0f); EXPECT_LE (p.noise.vol, 1.0f);
        EXPECT_TRUE (p.noiseType == "white" || p.noiseType == "periodic")
            << "noiseType=" << p.noiseType;
        EXPECT_TRUE (p.noiseRate == "low"  || p.noiseRate == "mid"
                  || p.noiseRate == "high" || p.noiseRate == "ch2")
            << "noiseRate=" << p.noiseRate;
    }

    // The factory set is the curated 12 files from the design doc / Task 10.
    EXPECT_EQ (count, 12) << "expected 12 factory .psg presets in " << sqDir;
}
