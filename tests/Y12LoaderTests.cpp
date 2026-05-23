#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

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

    // Pack a per-operator 16-byte Y12 block from a set of decoded field values.
    // Layout mirrors the YM2612 hardware register order (verified against
    // Furnace's DivEngine::loadY12); see PatchSystem.cpp's loadY12 comment.
    void writeY12OpBlock (std::array<uint8_t, 128>& bytes,
                          std::size_t base,
                          uint8_t mul, uint8_t hwDt, uint8_t tl,
                          uint8_t ks, uint8_t ar,
                          uint8_t amon, uint8_t dr,
                          uint8_t sr,
                          uint8_t sl, uint8_t rr,
                          uint8_t ssgEg)
    {
        bytes[base + 0] = static_cast<uint8_t> (((hwDt & 0x07) << 4) | (mul & 0x0F));
        bytes[base + 1] = static_cast<uint8_t> (tl & 0x7F);
        bytes[base + 2] = static_cast<uint8_t> (((ks & 0x03) << 6) | (ar & 0x1F));
        bytes[base + 3] = static_cast<uint8_t> (((amon & 0x01) << 7) | (dr & 0x1F));
        bytes[base + 4] = static_cast<uint8_t> (sr & 0x1F);
        bytes[base + 5] = static_cast<uint8_t> (((sl & 0x0F) << 4) | (rr & 0x0F));
        bytes[base + 6] = ssgEg;
        // Bytes +7..+15 are padding; left at 0 by std::array's value-init.
    }

    // A complete, well-formed Y12 buffer with distinct values per operator so
    // the tests can detect field-order regressions (e.g. swapping SL and RR).
    std::array<uint8_t, 128> makeY12Fixture()
    {
        std::array<uint8_t, 128> b {};
        for (int op = 0; op < 4; ++op)
        {
            writeY12OpBlock (
                b, static_cast<std::size_t> (op) * 16,
                static_cast<uint8_t> (op + 1),         // mul
                static_cast<uint8_t> (op + 1),         // hwDt: 1,2,3,4 → TFI 1,2,3,0
                static_cast<uint8_t> (10 + op * 20),   // tl
                static_cast<uint8_t> (op & 3),         // ks
                static_cast<uint8_t> (15 + op),        // ar
                static_cast<uint8_t> (op & 1),         // amon
                static_cast<uint8_t> (5 + op),         // dr
                static_cast<uint8_t> (7 + op),         // sr
                static_cast<uint8_t> (2 + op),         // sl
                static_cast<uint8_t> (8 + op),         // rr
                static_cast<uint8_t> (8 + op));        // ssg-eg (8..11, all valid)
        }
        b[0x40] = 5;                                  // alg
        b[0x41] = 3;                                  // fb
        b[0x42] = 2;                                  // ams
        b[0x43] = 6;                                  // pms
        b[0x44] = 0;                                  // legacy AMON-packed (ignored)
        b[0x45] = static_cast<uint8_t> ((1 << 3) | 4); // LFO: bit3 enable, bits0-2 rate=4
        return b;
    }

    fs::path writeTempY12 (const std::array<uint8_t, 128>& bytes, const char* filename)
    {
        const fs::path tmp = fs::temp_directory_path() / filename;
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        out.write (reinterpret_cast<const char*> (bytes.data()),
                   static_cast<std::streamsize> (bytes.size()));
        return tmp;
    }
}

// A hand-built 128-byte Y12 buffer loads with every field unpacked correctly,
// including the per-operator bit-packing (DT/MUL, KS/AR, AMON/DR, SL/RR) and
// the channel-level fields at 0x40-0x45.
TEST (PatchLoaderY12, FixtureLoadsAndUnpacksAllFields)
{
    const fs::path tmp = writeTempY12 (makeY12Fixture(), "genvst_y12_fixture.y12");
    const PatchLoadResult r = loadY12 (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_EQ (p.alg, 5);
    EXPECT_EQ (p.fb,  3);
    EXPECT_EQ (p.ams, 2);
    EXPECT_EQ (p.pms, 6);
    EXPECT_EQ (p.lr,  3) << "Y12 carries no L/R; loader must default to both";
    EXPECT_EQ (p.lfo_enable, 1);
    EXPECT_EQ (p.lfo_rate,   4);

    // HW DT 1, 2, 3, 4 → TFI 1, 2, 3, 0 (registerToDetune: HW 4 is the
    // hardware's "second zero" and collapses to TFI 0).
    const std::array<uint8_t, 4> expectedTfiDt { 1, 2, 3, 0 };

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_EQ (p.mul[op],  op + 1);
        EXPECT_EQ (p.dt[op],   expectedTfiDt[static_cast<std::size_t> (op)]);
        EXPECT_EQ (p.tl[op],   10 + op * 20);
        EXPECT_EQ (p.ks[op],   op & 3);
        EXPECT_EQ (p.ar[op],   15 + op);
        EXPECT_EQ (p.amon[op], op & 1);
        EXPECT_EQ (p.dr[op],   5 + op);
        EXPECT_EQ (p.sr[op],   7 + op);
        EXPECT_EQ (p.sl[op],   2 + op);
        EXPECT_EQ (p.rr[op],   8 + op);
        EXPECT_EQ (p.ssg[op],  8 + op);
    }

    EXPECT_EQ (p.name, "genvst_y12_fixture");
}

// HW DT register values 4-7 round-trip to TFI 0,4,5,6 — the negative-detune
// branch of registerToDetune (HW 4 collapses to TFI 0; 5/6/7 shift down by 1).
TEST (PatchLoaderY12, NegativeDetuneRegisterIsConvertedToTfiEncoding)
{
    std::array<uint8_t, 128> b {};
    writeY12OpBlock (b, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0);    // hwDt=4 → TFI 0
    writeY12OpBlock (b, 16, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0);   // hwDt=5 → TFI 4
    writeY12OpBlock (b, 32, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0);   // hwDt=6 → TFI 5
    writeY12OpBlock (b, 48, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0);   // hwDt=7 → TFI 6

    const fs::path tmp = writeTempY12 (b, "genvst_y12_negdetune.y12");
    const PatchLoadResult r = loadY12 (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->dt[0], 0);
    EXPECT_EQ (r.patch->dt[1], 4);
    EXPECT_EQ (r.patch->dt[2], 5);
    EXPECT_EQ (r.patch->dt[3], 6);
}

// Bytes that overshoot a field's bit-width are clamped/masked rather than
// wrapping. An all-0xFF buffer must produce a patch whose every field sits
// inside its valid hardware range — including the SSG-EG "0 or 8-15 only"
// rule (clampSsg collapses raw bits 0-7 to 0 if bit 3 is clear).
TEST (PatchLoaderY12, OutOfRangeBytesAreClampedToHardwareRange)
{
    std::array<uint8_t, 128> b {};
    b.fill (0xFF);

    const fs::path tmp = writeTempY12 (b, "genvst_y12_clamp.y12");
    const PatchLoadResult r = loadY12 (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_LE (p.alg, 7);
    EXPECT_LE (p.fb,  7);
    EXPECT_LE (p.ams, 3);
    EXPECT_LE (p.pms, 7);
    EXPECT_LE (p.lfo_rate, 7);
    EXPECT_LE (p.lfo_enable, 1);

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_LE (p.mul[op],  15);
        EXPECT_LE (p.dt[op],    6);
        EXPECT_LE (p.tl[op],  127);
        EXPECT_LE (p.ks[op],    3);
        EXPECT_LE (p.ar[op],   31);
        EXPECT_LE (p.dr[op],   31);
        EXPECT_LE (p.sr[op],   31);
        EXPECT_LE (p.rr[op],   15);
        EXPECT_LE (p.sl[op],   15);
        EXPECT_LE (p.amon[op],  1);

        const int ssg = p.ssg[op];
        EXPECT_TRUE (ssg == 0 || (ssg >= 8 && ssg <= 15)) << "ssg=" << ssg;
    }
}

// A buffer of the wrong size is rejected with an error and no patch.
TEST (PatchLoaderY12, WrongSizeFileIsRejected)
{
    std::vector<uint8_t> truncated (64, 0);
    const fs::path tmp = fs::temp_directory_path() / "genvst_y12_short.y12";
    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        out.write (reinterpret_cast<const char*> (truncated.data()),
                   static_cast<std::streamsize> (truncated.size()));
    }

    const PatchLoadResult r = loadY12 (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A missing file is rejected with an error and no patch.
TEST (PatchLoaderY12, MissingFileIsRejected)
{
    const PatchLoadResult r = loadY12 (fixturesDir() / "does_not_exist.y12");
    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// The on-disk Y12 fixture loads through the full file-read path so a
// platform-specific I/O regression (e.g. a Windows text-mode read smuggling a
// CRLF translation into a binary stream) shows up as a test failure.
TEST (PatchLoaderY12, OnDiskFixtureLoads)
{
    const fs::path fixture = fixturesDir() / "synth_lead.y12";
    ASSERT_TRUE (fs::exists (fixture)) << fixture;

    const PatchLoadResult r = loadY12 (fixture);
    ASSERT_TRUE (r.patch.has_value()) << r.error;

    const Patch& p = *r.patch;
    EXPECT_EQ (p.name, "synth_lead");
    EXPECT_LE (p.alg, 7);
    EXPECT_LE (p.fb,  7);

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_LE (p.tl[op], 127);
        EXPECT_LE (p.dt[op],   6);
        const int ssg = p.ssg[op];
        EXPECT_TRUE (ssg == 0 || (ssg >= 8 && ssg <= 15)) << "ssg=" << ssg;
    }
}
