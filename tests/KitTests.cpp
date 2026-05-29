#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "Kit.h"
#include "PatchSystem.h"

#ifndef GENVST_FACTORY_PATCHES_DIR
 #error "GENVST_FACTORY_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path tempPath (const std::string& stem)
    {
        const auto dir = fs::temp_directory_path() / "genvst-kit-tests";
        fs::create_directories (dir);
        return dir / (stem + ".gnkit");
    }

    // A patch with every field set to a distinctive, in-range value so a
    // round-trip that drops or mistypes any field is caught.
    Patch distinctivePatch (const std::string& name)
    {
        Patch p {};
        p.alg = 5; p.fb = 6; p.lr = 2; p.ams = 3; p.pms = 4;
        p.lfo_enable = 1; p.lfo_rate = 7;
        p.freq_ctrl_mode = 1; p.retrig_rate = 321;
        p.channel_tl = 0.75f; p.fm_dac_prescaler = 0.4f;
        p.name = name;
        for (int i = 0; i < 4; ++i)
        {
            const auto u = (std::size_t) i;
            p.mul[u]  = (std::uint8_t) (i + 1);
            p.dt[u]   = (std::uint8_t) (i % 7);
            p.tl[u]   = (std::uint8_t) (10 + i * 7);
            p.ks[u]   = (std::uint8_t) (i % 4);
            p.ar[u]   = (std::uint8_t) (20 + i);
            p.dr[u]   = (std::uint8_t) (5 + i);
            p.sr[u]   = (std::uint8_t) (3 + i);
            p.rr[u]   = (std::uint8_t) (8 + i);
            p.sl[u]   = (std::uint8_t) (i % 16);
            p.ssg[u]  = (std::uint8_t) (i == 0 ? 8 : 0);   // 8 valid; 1-7 invalid
            p.amon[u] = (std::uint8_t) (i % 2);
            p.mul_float[u]     = 1.5f + (float) i;
            p.fixed[u]         = (i % 2) == 0;
            p.freq_fixed_hz[u] = 220.0f + (float) i * 10.0f;
            p.vel[u]           = 0.1f * (float) i;
        }
        return p;
    }

    void expectPatchEq (const Patch& a, const Patch& b)
    {
        EXPECT_EQ (a.alg, b.alg);
        EXPECT_EQ (a.fb, b.fb);
        EXPECT_EQ (a.lr, b.lr);
        EXPECT_EQ (a.ams, b.ams);
        EXPECT_EQ (a.pms, b.pms);
        EXPECT_EQ (a.lfo_enable, b.lfo_enable);
        EXPECT_EQ (a.lfo_rate, b.lfo_rate);
        EXPECT_EQ (a.freq_ctrl_mode, b.freq_ctrl_mode);
        EXPECT_EQ (a.retrig_rate, b.retrig_rate);
        EXPECT_FLOAT_EQ (a.channel_tl, b.channel_tl);
        EXPECT_FLOAT_EQ (a.fm_dac_prescaler, b.fm_dac_prescaler);
        for (int i = 0; i < 4; ++i)
        {
            SCOPED_TRACE (i);
            const auto u = (std::size_t) i;
            EXPECT_EQ (a.mul[u], b.mul[u]);
            EXPECT_EQ (a.dt[u], b.dt[u]);
            EXPECT_EQ (a.tl[u], b.tl[u]);
            EXPECT_EQ (a.ks[u], b.ks[u]);
            EXPECT_EQ (a.ar[u], b.ar[u]);
            EXPECT_EQ (a.dr[u], b.dr[u]);
            EXPECT_EQ (a.sr[u], b.sr[u]);
            EXPECT_EQ (a.rr[u], b.rr[u]);
            EXPECT_EQ (a.sl[u], b.sl[u]);
            EXPECT_EQ (a.ssg[u], b.ssg[u]);
            EXPECT_EQ (a.amon[u], b.amon[u]);
            EXPECT_FLOAT_EQ (a.mul_float[u], b.mul_float[u]);
            EXPECT_EQ (a.fixed[u], b.fixed[u]);
            EXPECT_FLOAT_EQ (a.freq_fixed_hz[u], b.freq_fixed_hz[u]);
            EXPECT_FLOAT_EQ (a.vel[u], b.vel[u]);
        }
    }
}

// Save → load round-trip preserves every slot field and every embedded Patch
// field.
TEST (Kit, RoundTripPreservesEverySlotAndPatchField)
{
    Kit original;
    original.version = 1;
    original.name    = "My Kit";

    KitSlot& kick = original.slots[1];
    kick.enabled  = true;
    kick.midiNote = 36;
    kick.fixedNote = 36;
    kick.label    = "Kick";
    kick.volume   = 0.9f;
    kick.decayRr  = 4;
    kick.sourcePath = "../drums/kick.tfi";
    kick.patch    = distinctivePatch ("kick");

    KitSlot& snare = original.slots[7];
    snare.enabled  = true;
    snare.midiNote = 38;
    snare.fixedNote = 50;
    snare.label    = "Snare";
    snare.volume   = 1.0f;
    snare.decayRr  = -1;
    snare.patch    = distinctivePatch ("snare");

    const auto path = tempPath ("roundtrip");
    const auto saveErr = saveKit (original, path);
    ASSERT_TRUE (saveErr.empty()) << saveErr;

    const auto loaded = loadKit (path);
    ASSERT_TRUE (loaded.kit.has_value()) << loaded.error;
    EXPECT_EQ (loaded.kit->version, 1);
    EXPECT_EQ (loaded.kit->name, "My Kit");

    for (int pad : { 1, 7 })
    {
        SCOPED_TRACE (pad);
        const auto& a = original.slots[(std::size_t) pad];
        const auto& b = loaded.kit->slots[(std::size_t) pad];
        EXPECT_TRUE (b.enabled);
        EXPECT_EQ (b.midiNote, a.midiNote);
        EXPECT_EQ (b.fixedNote, a.fixedNote);
        EXPECT_EQ (b.label, a.label);
        EXPECT_FLOAT_EQ (b.volume, a.volume);
        EXPECT_EQ (b.decayRr, a.decayRr);
        expectPatchEq (a.patch, b.patch);
    }

    // Disabled pads stay disabled across the round-trip.
    EXPECT_FALSE (loaded.kit->slots[0].enabled);
    EXPECT_FALSE (loaded.kit->slots[31].enabled);
}

// slotForNote: maps enabled trigger notes, returns -1 for unmapped notes, and
// resolves a duplicate note to the lower pad index.
TEST (Kit, SlotForNoteRouting)
{
    Kit kit;
    kit.slots[3].enabled = true;  kit.slots[3].midiNote = 36;
    kit.slots[9].enabled = true;  kit.slots[9].midiNote = 38;
    kit.slots[5].enabled = true;  kit.slots[5].midiNote = 36;   // duplicate note
    kit.slots[2].enabled = false; kit.slots[2].midiNote = 40;   // disabled

    EXPECT_EQ (kit.slotForNote (36), 3);   // lower pad index wins
    EXPECT_EQ (kit.slotForNote (38), 9);
    EXPECT_EQ (kit.slotForNote (40), -1);  // disabled pad is not triggerable
    EXPECT_EQ (kit.slotForNote (60), -1);  // unmapped
}

// resolvedPadPatch folds volume into channel_tl (multiplicatively) and the
// decay override onto every operator's RR; a -1 decay leaves RR untouched.
TEST (Kit, ResolvedPadPatchFoldsVolumeAndDecay)
{
    KitSlot slot;
    slot.patch = distinctivePatch ("x");
    slot.patch.channel_tl = 0.8f;
    slot.volume  = 0.5f;
    slot.decayRr = 12;

    const Patch r = resolvedPadPatch (slot);
    EXPECT_FLOAT_EQ (r.channel_tl, 0.4f);   // 0.8 * 0.5
    for (auto rr : r.rr)
        EXPECT_EQ (rr, 12);

    // decayRr == -1 keeps the patch's own RR values.
    KitSlot keep;
    keep.patch = distinctivePatch ("y");
    keep.decayRr = -1;
    keep.volume  = 1.0f;
    const Patch k = resolvedPadPatch (keep);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ (k.rr[(std::size_t) i], keep.patch.rr[(std::size_t) i]);
}

// A slot that embeds a real factory .tfi survives a kit round-trip with the
// patch data intact (the embed-not-reference guarantee).
TEST (Kit, EmbeddedFactoryPatchSurvivesRoundTrip)
{
    const fs::path tfi = fs::path (GENVST_FACTORY_PATCHES_DIR) / "fm" / "drums" / "kick.tfi";
    const auto loadedTfi = loadTFI (tfi);
    ASSERT_TRUE (loadedTfi.patch.has_value()) << loadedTfi.error;

    Kit kit;
    kit.name = "Embed Test";
    kit.slots[0].enabled  = true;
    kit.slots[0].midiNote = 36;
    kit.slots[0].label    = "Kick";
    kit.slots[0].patch    = *loadedTfi.patch;

    const auto path = tempPath ("embed");
    ASSERT_TRUE (saveKit (kit, path).empty());

    const auto reloaded = loadKit (path);
    ASSERT_TRUE (reloaded.kit.has_value()) << reloaded.error;
    expectPatchEq (*loadedTfi.patch, reloaded.kit->slots[0].patch);
}

// The factory GM kit loads, resolves all its `source` references into embedded
// patches, and maps the standard GM bass-drum / snare / hat notes.
TEST (Kit, FactoryGmStandardKitLoadsAndResolvesSources)
{
    const fs::path kitPath =
        fs::path (GENVST_FACTORY_PATCHES_DIR) / "fm" / "kits" / "gm-standard.gnkit";
    ASSERT_TRUE (fs::exists (kitPath)) << kitPath.string();

    const auto loaded = loadKit (kitPath);
    ASSERT_TRUE (loaded.kit.has_value()) << loaded.error;
    EXPECT_TRUE (loaded.warning.empty()) << loaded.warning;   // every source resolved
    EXPECT_EQ (loaded.kit->name, "GM Standard Kit");

    int enabled = 0;
    for (const auto& s : loaded.kit->slots)
        if (s.enabled)
            ++enabled;
    EXPECT_EQ (enabled, 23);

    // GM core mapping is present and the sources were embedded (a TFI carries
    // an algorithm and at least one non-zero operator level, so the embedded
    // patch must not be a default-constructed Patch).
    const int kickSlot = loaded.kit->slotForNote (36);
    ASSERT_GE (kickSlot, 0);
    EXPECT_EQ (loaded.kit->slots[(std::size_t) kickSlot].label, "Bass Drum 1");

    ASSERT_GE (loaded.kit->slotForNote (38), 0);   // Acoustic Snare
    ASSERT_GE (loaded.kit->slotForNote (42), 0);   // Closed Hi-Hat
    EXPECT_EQ (loaded.kit->slotForNote (99), -1);  // unmapped
}

// In-memory JSON round-trip (the path used to embed a kit in project state):
// no file, no source resolution — embedded patches only.
TEST (Kit, InMemoryJsonRoundTrip)
{
    Kit original;
    original.name = "State Kit";
    original.slots[5].enabled  = true;
    original.slots[5].midiNote = 42;
    original.slots[5].fixedNote = 42;
    original.slots[5].label    = "Hat";
    original.slots[5].volume   = 0.7f;
    original.slots[5].decayRr  = 9;
    original.slots[5].patch    = distinctivePatch ("hat");

    const std::string json = kitToJson (original);
    const auto loaded = kitFromJson (json);   // empty baseDir — no source resolution
    ASSERT_TRUE (loaded.kit.has_value()) << loaded.error;
    EXPECT_EQ (loaded.kit->name, "State Kit");
    ASSERT_TRUE (loaded.kit->slots[5].enabled);
    EXPECT_EQ (loaded.kit->slots[5].midiNote, 42);
    EXPECT_FLOAT_EQ (loaded.kit->slots[5].volume, 0.7f);
    EXPECT_EQ (loaded.kit->slots[5].decayRr, 9);
    expectPatchEq (original.slots[5].patch, loaded.kit->slots[5].patch);
}

// Schema guard: an unsupported version is rejected with an error and no kit.
TEST (Kit, RejectsUnsupportedVersion)
{
    const auto path = tempPath ("badversion");
    {
        std::ofstream f (path, std::ios::binary | std::ios::trunc);
        f << R"({ "version": 2, "name": "future", "slots": [] })";
    }
    const auto loaded = loadKit (path);
    EXPECT_FALSE (loaded.kit.has_value());
    EXPECT_FALSE (loaded.error.empty());
}
