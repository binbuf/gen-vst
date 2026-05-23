#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PatchSystem.h"

#ifndef GENVST_FIXTURES_PATCHES_DIR
 #error "GENVST_FIXTURES_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path fixturesDir()
    {
        return fs::path (GENVST_FIXTURES_PATCHES_DIR);
    }

    // Write an inline OPM-format string to a temp file and run loadOPM on it.
    // The on-disk round-trip is what loadOPM actually has to handle in
    // production — building an in-memory parser overload just for the tests
    // would let bugs in the file-read path slip through.
    PatchLoadResult loadFromInlineOpm (std::string_view content, const char* filename)
    {
        const fs::path tmp = fs::temp_directory_path() / filename;
        {
            std::ofstream out (tmp, std::ios::trunc);
            out.write (content.data(), static_cast<std::streamsize> (content.size()));
        }
        const PatchLoadResult r = loadOPM (tmp);
        fs::remove (tmp);
        return r;
    }

    // Minimum well-formed OPM block: one patch named "Lead 1" with all per-op
    // and channel-level fields populated. Distinct values per operator so the
    // tests can detect operator-order regressions (e.g. M2 swapped with C1).
    //
    // Operator line columns:
    //   AR D1R D2R RR D1L TL KS MUL DT1 DT2 AMS-EN
    // 04-patch-system.md mapping: M1→OP1, C1→OP2, M2→OP3, C2→OP4.
    constexpr std::string_view kBasicOpm =
        "@:0 Lead 1\n"
        "LFO: 7 0 0 0 0\n"
        "CH:  64 3 5 2 6 120 0\n"
        "M1:  16  5  7  8  2  10 0 1 1 3 0\n"
        "C1:  17  6  8  9  3  30 1 2 2 2 1\n"
        "M2:  18  7  9 10  4  50 2 3 3 1 0\n"
        "C2:  19  8 10 11  5  70 3 4 4 0 1\n";
}

// A complete OPM block loads with every channel-level and per-operator field
// in the right place. Operator mapping M1/C1/M2/C2 → OP1/OP2/OP3/OP4 is
// asserted directly: each operator's `tl` value is distinct so a mapping swap
// would produce a wrong TL in at least one slot.
TEST (PatchLoaderOpm, FixtureLoadsAndMapsOperatorsCorrectly)
{
    const PatchLoadResult r = loadFromInlineOpm (kBasicOpm, "genvst_opm_basic.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_EQ (p.name, "Lead 1");
    EXPECT_EQ (p.lr, 3) << "OPM carries no L/R; loader must default to both";

    // CH line: PAN(64) FL(3) CON(5) AMS(2) PMS(6) SLOT(120) NE(0)
    EXPECT_EQ (p.fb,  3);
    EXPECT_EQ (p.alg, 5);
    EXPECT_EQ (p.ams, 2);
    EXPECT_EQ (p.pms, 6);

    // M1 (OP1=idx0): AR=16 DR=5 SR=7 RR=8 SL=2 TL=10 KS=0 MUL=1 DT1=1 DT2=3 AMS-EN=0
    EXPECT_EQ (p.ar[0], 16);
    EXPECT_EQ (p.dr[0],  5);
    EXPECT_EQ (p.sr[0],  7);
    EXPECT_EQ (p.rr[0],  8);
    EXPECT_EQ (p.sl[0],  2);
    EXPECT_EQ (p.tl[0], 10);
    EXPECT_EQ (p.ks[0],  0);
    EXPECT_EQ (p.mul[0], 1);
    EXPECT_EQ (p.dt[0],  1);   // HW 1 → TFI 1
    EXPECT_EQ (p.amon[0], 0);

    // C1 → OP2 (idx 1): TL=30, AMS-EN=1 → amon=1
    EXPECT_EQ (p.tl[1], 30);
    EXPECT_EQ (p.amon[1], 1);

    // M2 → OP3 (idx 2): TL=50, AMS-EN=0
    EXPECT_EQ (p.tl[2], 50);
    EXPECT_EQ (p.amon[2], 0);

    // C2 → OP4 (idx 3): TL=70, DT1=4 → TFI 0 (HW 4 is hw "second zero"),
    // AMS-EN=1.
    EXPECT_EQ (p.tl[3], 70);
    EXPECT_EQ (p.dt[3], 0);   // HW 4 → TFI 0
    EXPECT_EQ (p.amon[3], 1);
}

// SSG-EG defaults to 0 (off) for every operator. OPM has no SSG-EG field, so
// the loader leaves it at the Patch{} default (ADR-0019).
TEST (PatchLoaderOpm, SsgEgDefaultsToZero)
{
    const PatchLoadResult r = loadFromInlineOpm (kBasicOpm, "genvst_opm_ssg.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    for (int op = 0; op < 4; ++op)
        EXPECT_EQ (r.patch->ssg[op], 0) << "op=" << op;
}

// DT2 is silently dropped — no error message, no patch field for it (YM2612
// has no DT2 register). The presence of a non-zero DT2 in the source line
// must not surface as a load failure (ADR-0019).
TEST (PatchLoaderOpm, Dt2IsSilentlyDroppedNotAnError)
{
    // Each operator line has DT2 = 3 (the position-9 column).
    constexpr std::string_view withDt2 =
        "@:0 With DT2\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 3 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 3 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 3 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 3 0\n";

    const PatchLoadResult r = loadFromInlineOpm (withDt2, "genvst_opm_dt2.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_TRUE (r.error.empty()) << "DT2 must NOT produce a load error";
    // No Patch field should be 3-valued from DT2 — every per-op field is 0.
    for (int op = 0; op < 4; ++op)
        EXPECT_EQ (r.patch->dt[op], 0) << "DT1 was 0 in the source; DT2 must not leak";
}

// lfo_enable is set if any of LFRQ/AMD/PMD is non-zero, regardless of which.
TEST (PatchLoaderOpm, LfoEnableDerivedFromNonzeroLfrqAmdPmd)
{
    // LFRQ non-zero only.
    constexpr std::string_view lfrqOnly =
        "@:0 LFRQOnly\n"
        "LFO: 5 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 0 0\n";
    auto r = loadFromInlineOpm (lfrqOnly, "genvst_opm_lfo_lfrq.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->lfo_enable, 1);
    EXPECT_EQ (r.patch->lfo_rate, 5);

    // AMD non-zero only.
    constexpr std::string_view amdOnly =
        "@:0 AMDOnly\n"
        "LFO: 0 9 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 0 0\n";
    r = loadFromInlineOpm (amdOnly, "genvst_opm_lfo_amd.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->lfo_enable, 1);
    EXPECT_EQ (r.patch->lfo_rate, 0);

    // All zero → enable off.
    constexpr std::string_view allZero =
        "@:0 NoLfo\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 0 0\n";
    r = loadFromInlineOpm (allZero, "genvst_opm_lfo_none.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->lfo_enable, 0);
}

// A multi-instrument OPM file loads the first @: block only; the second
// block's fields are not present (post-MVP behavior per ADR-0019). The test
// uses distinct TL values per block to detect any leakage.
TEST (PatchLoaderOpm, MultiInstrumentLoadsFirstBlockOnly)
{
    constexpr std::string_view multi =
        "@:0 First\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 1 1 1 1 0 0\n"
        "M1:  0 0 0 0 0 11 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 22 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 33 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 44 0 0 0 0 0\n"
        "@:1 Second\n"
        "LFO: 7 7 7 0 0\n"
        "CH:  64 7 7 3 7 0 0\n"
        "M1:  0 0 0 0 0 99 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 99 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 99 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 99 0 0 0 0 0\n";

    const PatchLoadResult r = loadFromInlineOpm (multi, "genvst_opm_multi.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;
    EXPECT_EQ (p.name, "First");
    EXPECT_EQ (p.tl[0], 11);
    EXPECT_EQ (p.tl[1], 22);
    EXPECT_EQ (p.tl[2], 33);
    EXPECT_EQ (p.tl[3], 44);
    EXPECT_EQ (p.fb,  1);
    EXPECT_EQ (p.alg, 1);
    EXPECT_EQ (p.ams, 1);
    EXPECT_EQ (p.pms, 1);
}

// Out-of-range integers are clamped to the hardware range — they do not
// silently wrap and they do not turn into a load error.
TEST (PatchLoaderOpm, OutOfRangeValuesAreClamped)
{
    // TL=255 clamps to 127; AR=99 clamps to 31; MUL=99 clamps to 15.
    constexpr std::string_view oor =
        "@:0 OOR\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 9 9 9 9 0 0\n"
        "M1:  99 99 99 99 99 255 9 99 0 0 1\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 0 0\n";
    const PatchLoadResult r = loadFromInlineOpm (oor, "genvst_opm_clamp.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_LE (p.fb,  7);
    EXPECT_LE (p.alg, 7);
    EXPECT_LE (p.ams, 3);
    EXPECT_LE (p.pms, 7);
    EXPECT_EQ (p.tl[0], 127);
    EXPECT_EQ (p.ar[0],  31);
    EXPECT_EQ (p.dr[0],  31);
    EXPECT_EQ (p.sr[0],  31);
    EXPECT_EQ (p.rr[0],  15);
    EXPECT_EQ (p.sl[0],  15);
    EXPECT_EQ (p.ks[0],   3);
    EXPECT_EQ (p.mul[0], 15);
    EXPECT_EQ (p.amon[0], 1);
}

// A file missing a required parameter line returns an error and no patch.
TEST (PatchLoaderOpm, MissingRequiredLineIsRejected)
{
    // Missing C2.
    constexpr std::string_view missingC2 =
        "@:0 Incomplete\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n";
    const PatchLoadResult r = loadFromInlineOpm (missingC2, "genvst_opm_missc2.opm");
    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A file with no `@:` header at all is rejected; parameter lines without a
// patch context are not silently absorbed into a default-named patch.
TEST (PatchLoaderOpm, MissingHeaderIsRejected)
{
    constexpr std::string_view noHeader =
        "LFO: 0 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 0 0 0 0 0 0\n";
    const PatchLoadResult r = loadFromInlineOpm (noHeader, "genvst_opm_nohdr.opm");
    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// An operator line with too few integers is a load error (not a partial-load).
TEST (PatchLoaderOpm, TooFewIntegersOnOperatorLineIsRejected)
{
    // M1 has only 4 numbers (needs 11).
    constexpr std::string_view shortLine =
        "@:0 Short\n"
        "LFO: 0 0 0 0 0\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0\n"
        "C1:  0 0 0 0 0 0 0 0 0 0 0\n"
        "M2:  0 0 0 0 0 0 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 0 0 0 0 0 0\n";
    const PatchLoadResult r = loadFromInlineOpm (shortLine, "genvst_opm_short.opm");
    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// Comment lines (`//`) and blank lines are skipped without affecting parsing.
TEST (PatchLoaderOpm, CommentsAndBlanksAreSkipped)
{
    constexpr std::string_view withComments =
        "// header comment\n"
        "\n"
        "@:0 Commented\n"
        "// after header\n"
        "LFO: 0 0 0 0 0\n"
        "\n"
        "CH:  64 0 0 0 0 0 0\n"
        "M1:  0 0 0 0 0 10 0 0 0 0 0\n"
        "C1:  0 0 0 0 0 20 0 0 0 0 0\n"
        "// trailing comment\n"
        "M2:  0 0 0 0 0 30 0 0 0 0 0\n"
        "C2:  0 0 0 0 0 40 0 0 0 0 0\n";
    const PatchLoadResult r = loadFromInlineOpm (withComments, "genvst_opm_comments.opm");
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->name, "Commented");
    EXPECT_EQ (r.patch->tl[0], 10);
    EXPECT_EQ (r.patch->tl[3], 40);
}

// The on-disk OPM fixture loads through the full file-read path.
TEST (PatchLoaderOpm, OnDiskFixtureLoads)
{
    const fs::path fixture = fixturesDir() / "synth_lead.opm";
    ASSERT_TRUE (fs::exists (fixture)) << fixture;

    const PatchLoadResult r = loadOPM (fixture);
    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_FALSE (p.name.empty());
    EXPECT_LE (p.alg, 7);
    EXPECT_LE (p.fb,  7);
    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_LE (p.tl[op], 127);
        EXPECT_LE (p.dt[op],   6);
        EXPECT_EQ (p.ssg[op],  0) << "OPM has no SSG-EG source; must default to 0";
    }
}
