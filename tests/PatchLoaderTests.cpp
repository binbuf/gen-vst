#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "PatchSystem.h"
#include "PsgPreset.h"

#ifndef GENVST_FACTORY_PATCHES_DIR
 #error "GENVST_FACTORY_PATCHES_DIR must be defined by the test build (see tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace
{
    fs::path factoryDir()
    {
        return fs::path (GENVST_FACTORY_PATCHES_DIR);
    }

    // Assert every Patch field sits inside its valid hardware range. loadTFI
    // clamps on load, so this verifies the clamp held.
    void expectInHardwareRange (const Patch& p)
    {
        EXPECT_LE (static_cast<int> (p.alg), 7);
        EXPECT_LE (static_cast<int> (p.fb),  7);
        EXPECT_LE (static_cast<int> (p.lr),  3);
        EXPECT_LE (static_cast<int> (p.ams), 3);
        EXPECT_LE (static_cast<int> (p.pms), 7);

        for (int op = 0; op < 4; ++op)
        {
            SCOPED_TRACE (op);
            EXPECT_LE (static_cast<int> (p.mul[op]),  15);
            EXPECT_LE (static_cast<int> (p.dt[op]),    6);
            EXPECT_LE (static_cast<int> (p.tl[op]),  127);
            EXPECT_LE (static_cast<int> (p.ks[op]),    3);
            EXPECT_LE (static_cast<int> (p.ar[op]),   31);
            EXPECT_LE (static_cast<int> (p.dr[op]),   31);
            EXPECT_LE (static_cast<int> (p.sr[op]),   31);
            EXPECT_LE (static_cast<int> (p.rr[op]),   15);
            EXPECT_LE (static_cast<int> (p.sl[op]),   15);
            EXPECT_LE (static_cast<int> (p.amon[op]),  1);

            const int ssg = static_cast<int> (p.ssg[op]);
            EXPECT_TRUE (ssg == 0 || (ssg >= 8 && ssg <= 15)) << "ssg=" << ssg;
        }
    }
}

// Every factory .tfi file loads successfully.
TEST (PatchLoader, AllFactoryFilesLoad)
{
    ASSERT_TRUE (fs::is_directory (factoryDir())) << factoryDir();

    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator (factoryDir()))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".tfi")
            continue;

        ++count;
        SCOPED_TRACE (entry.path().filename().string());
        const PatchLoadResult result = loadTFI (entry.path());
        EXPECT_TRUE (result.patch.has_value());
        EXPECT_TRUE (result.error.empty());
    }

    EXPECT_GT (count, 0) << "no .tfi files found in " << factoryDir();
}

// Loaded values are clamped within their hardware ranges.
TEST (PatchLoader, LoadedValuesAreInHardwareRange)
{
    ASSERT_TRUE (fs::is_directory (factoryDir())) << factoryDir();

    for (const auto& entry : fs::recursive_directory_iterator (factoryDir()))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".tfi")
            continue;

        SCOPED_TRACE (entry.path().filename().string());
        const PatchLoadResult result = loadTFI (entry.path());
        ASSERT_TRUE (result.patch.has_value());
        expectInHardwareRange (*result.patch);
    }
}

// The patch name is taken from the file's stem.
TEST (PatchLoader, NameComesFromFilename)
{
    const PatchLoadResult result = loadTFI (factoryDir() / "fm" / "keys" / "organ.tfi");
    ASSERT_TRUE (result.patch.has_value());
    EXPECT_EQ (result.patch->name, "organ");
}

// A wrong-size file is rejected with an error and no patch.
TEST (PatchLoader, WrongSizeFileIsRejected)
{
    const fs::path tmp = fs::temp_directory_path() / "genvst_wrongsize.tfi";
    {
        std::ofstream out (tmp, std::ios::binary);
        ASSERT_TRUE (out.is_open());
        const std::vector<char> junk (10, 0);   // 10 bytes, not 42
        out.write (junk.data(), static_cast<std::streamsize> (junk.size()));
    }

    const PatchLoadResult result = loadTFI (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (result.patch.has_value());
    EXPECT_FALSE (result.error.empty());
}

// A missing file is rejected with an error and no patch.
TEST (PatchLoader, MissingFileIsRejected)
{
    const PatchLoadResult result = loadTFI (factoryDir() / "does_not_exist.tfi");
    EXPECT_FALSE (result.patch.has_value());
    EXPECT_FALSE (result.error.empty());
}

// =============================================================================
// VGI / DMP / export tests
//
// The repo ships no `.vgi`/`.dmp` files (the factory bank is TFI-only), so
// these tests build byte-level fixtures in-test and round-trip patches through
// the exporters back into the loaders.
// =============================================================================

namespace
{
    // Write `bytes` to a fresh file in the temp dir; caller is responsible for
    // removing it. Uses distinct filenames per test so a leaked file from one
    // test cannot mask a bug in another.
    fs::path writeTempBinary (const std::vector<uint8_t>& bytes, const char* filename)
    {
        const fs::path tmp = fs::temp_directory_path() / filename;
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        out.write (reinterpret_cast<const char*> (bytes.data()),
                   static_cast<std::streamsize> (bytes.size()));
        return tmp;
    }

    // A Patch populated with distinct non-zero values for every field that any
    // format carries — used for both directed-field checks (was the bit-packing
    // unpacked correctly?) and export→load round-trips.
    Patch makeBusyPatch()
    {
        Patch p {};
        p.alg = 5;
        p.fb  = 3;
        p.ams = 2;
        p.pms = 6;
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op]  = static_cast<uint8_t> (op + 1);          // 1..4
            p.dt[op]   = static_cast<uint8_t> (op % 7);          // 0..3 — stays in 0-6
            p.tl[op]   = static_cast<uint8_t> (10 + op * 20);    // 10,30,50,70
            p.ks[op]   = static_cast<uint8_t> (op & 0x03);       // 0..3
            p.ar[op]   = static_cast<uint8_t> (15 + op);         // 15..18
            p.dr[op]   = static_cast<uint8_t> (5 + op);          // 5..8
            p.sr[op]   = static_cast<uint8_t> (7 + op);          // 7..10
            p.rr[op]   = static_cast<uint8_t> (8 + op);          // 8..11 — fits 0-15
            p.sl[op]   = static_cast<uint8_t> (2 + op);          // 2..5
            p.ssg[op]  = static_cast<uint8_t> (8 + op);          // valid SSG: 8..15
            p.amon[op] = static_cast<uint8_t> (op & 1);          // alternating
        }
        return p;
    }

    // Field-by-field equality excluding `name`/`lr`/LFO (formats don't store
    // those). `compareExtras` covers the VGI-only fields.
    void expectCorePatchEqual (const Patch& a, const Patch& b, bool compareExtras)
    {
        EXPECT_EQ (a.alg, b.alg);
        EXPECT_EQ (a.fb,  b.fb);
        if (compareExtras)
        {
            EXPECT_EQ (a.ams, b.ams);
            EXPECT_EQ (a.pms, b.pms);
        }
        for (int op = 0; op < 4; ++op)
        {
            SCOPED_TRACE (op);
            EXPECT_EQ (a.mul[op], b.mul[op]);
            EXPECT_EQ (a.dt[op],  b.dt[op]);
            EXPECT_EQ (a.tl[op],  b.tl[op]);
            EXPECT_EQ (a.ks[op],  b.ks[op]);
            EXPECT_EQ (a.ar[op],  b.ar[op]);
            EXPECT_EQ (a.dr[op],  b.dr[op]);
            EXPECT_EQ (a.sr[op],  b.sr[op]);
            EXPECT_EQ (a.rr[op],  b.rr[op]);
            EXPECT_EQ (a.sl[op],  b.sl[op]);
            EXPECT_EQ (a.ssg[op], b.ssg[op]);
            if (compareExtras)
                EXPECT_EQ (a.amon[op], b.amon[op]);
        }
    }

    // Build a 43-byte VGI fixture mirroring makeBusyPatch's field values, with
    // AMS/FMS pre-packed into byte 0x02 and AMON pre-packed into each DR byte.
    std::vector<uint8_t> makeVgiFixture()
    {
        std::vector<uint8_t> b (kVgiFileSize, 0);
        b[0] = 5;                          // alg
        b[1] = 3;                          // fb
        b[2] = static_cast<uint8_t> ((2 << 4) | 6);  // ams=2 (bits 5:4), pms=6 (bits 2:0)
        for (int op = 0; op < 4; ++op)
        {
            const std::size_t base = 3 + static_cast<std::size_t> (op) * 10;
            b[base + 0] = static_cast<uint8_t> (op + 1);
            b[base + 1] = static_cast<uint8_t> (op % 7);
            b[base + 2] = static_cast<uint8_t> (10 + op * 20);
            b[base + 3] = static_cast<uint8_t> (op & 3);
            b[base + 4] = static_cast<uint8_t> (15 + op);
            // DR byte: bit 7 = AMON, bits 4:0 = DR.
            b[base + 5] = static_cast<uint8_t> (((op & 1) << 7) | (5 + op));
            b[base + 6] = static_cast<uint8_t> (7 + op);
            b[base + 7] = static_cast<uint8_t> (8 + op);
            b[base + 8] = static_cast<uint8_t> (2 + op);
            b[base + 9] = static_cast<uint8_t> (8 + op);
        }
        return b;
    }

    // Build a 51-byte DMP v11 Genesis FM fixture. Byte order verified against
    // Furnace's DivEngine::loadDMP in fileOpsIns.cpp.
    std::vector<uint8_t> makeDmpV11Fixture()
    {
        std::vector<uint8_t> b (kDmpV11FmFileSize, 0);
        b[0] = 0x0B;   // version 11
        b[1] = 0x02;   // sys = Genesis
        b[2] = 0x01;   // mode = FM
        b[3] = 6;      // FMS
        b[4] = 3;      // FB
        b[5] = 5;      // ALG
        b[6] = 2;      // AMS
        for (int op = 0; op < 4; ++op)
        {
            const std::size_t base = 7 + static_cast<std::size_t> (op) * 11;
            b[base +  0] = static_cast<uint8_t> (op + 1);          // mult
            b[base +  1] = static_cast<uint8_t> (10 + op * 20);    // tl
            b[base +  2] = static_cast<uint8_t> (15 + op);         // ar
            b[base +  3] = static_cast<uint8_t> (5 + op);          // dr
            b[base +  4] = static_cast<uint8_t> (2 + op);          // sl
            b[base +  5] = static_cast<uint8_t> (8 + op);          // rr
            b[base +  6] = static_cast<uint8_t> (op & 1);          // am
            b[base +  7] = static_cast<uint8_t> (op & 3);          // rs
            b[base +  8] = static_cast<uint8_t> (op % 7);          // dt (lo nibble); dt2=0
            b[base +  9] = static_cast<uint8_t> (7 + op);          // d2r
            b[base + 10] = static_cast<uint8_t> (8 + op);          // ssg-eg
        }
        return b;
    }
}

// A hand-built valid VGI fixture loads with every field unpacked correctly,
// including AMS/FMS from byte 0x02 and AMON from each DR byte.
TEST (PatchLoaderVgi, FixtureLoadsAndUnpacksBitPackedFields)
{
    const fs::path tmp = writeTempBinary (makeVgiFixture(), "genvst_vgi_fixture.vgi");
    const PatchLoadResult r = loadVGI (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_EQ (p.alg, 5);
    EXPECT_EQ (p.fb,  3);
    EXPECT_EQ (p.ams, 2);
    EXPECT_EQ (p.pms, 6);

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_EQ (p.mul[op],  op + 1);
        EXPECT_EQ (p.dt[op],   op % 7);
        EXPECT_EQ (p.tl[op],   10 + op * 20);   // TL clamps to 0..127 for all 4 ops
        EXPECT_EQ (p.ks[op],   op & 3);
        EXPECT_EQ (p.ar[op],   15 + op);
        EXPECT_EQ (p.dr[op],   5 + op);
        EXPECT_EQ (p.sr[op],   7 + op);
        EXPECT_EQ (p.rr[op],   8 + op);
        EXPECT_EQ (p.sl[op],   2 + op);
        EXPECT_EQ (p.ssg[op],  8 + op);
        EXPECT_EQ (p.amon[op], op & 1);
    }

    EXPECT_EQ (p.name, "genvst_vgi_fixture");
}

// A wrong-size VGI is rejected with an error and no patch.
TEST (PatchLoaderVgi, WrongSizeFileIsRejected)
{
    std::vector<uint8_t> truncated = makeVgiFixture();
    truncated.resize (20);     // 20 != 43
    const fs::path tmp = writeTempBinary (truncated, "genvst_vgi_short.vgi");

    const PatchLoadResult r = loadVGI (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A hand-built v11 DMP fixture loads as an FM patch with every field set.
TEST (PatchLoaderDmp, V11GenesisFmFixtureLoads)
{
    const fs::path tmp = writeTempBinary (makeDmpV11Fixture(), "genvst_dmp_fixture.dmp");
    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    const Patch& p = *r.patch;

    EXPECT_EQ (p.alg, 5);
    EXPECT_EQ (p.fb,  3);
    EXPECT_EQ (p.ams, 2);
    EXPECT_EQ (p.pms, 6);   // FMS in DMP -> pms in Patch

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE (op);
        EXPECT_EQ (p.mul[op],  op + 1);
        EXPECT_EQ (p.tl[op],   10 + op * 20);
        EXPECT_EQ (p.ar[op],   15 + op);
        EXPECT_EQ (p.dr[op],   5 + op);
        EXPECT_EQ (p.sl[op],   2 + op);
        EXPECT_EQ (p.rr[op],   8 + op);
        EXPECT_EQ (p.amon[op], op & 1);
        EXPECT_EQ (p.ks[op],   op & 3);    // RS
        EXPECT_EQ (p.dt[op],   op % 7);
        EXPECT_EQ (p.sr[op],   7 + op);    // D2R
        EXPECT_EQ (p.ssg[op],  8 + op);
    }
}

// 0x42 (Genesis Extended) is accepted alongside 0x02.
TEST (PatchLoaderDmp, GenesisExtendedSystemIsAccepted)
{
    std::vector<uint8_t> fixture = makeDmpV11Fixture();
    fixture[1] = 0x42;
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_genext.dmp");

    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    EXPECT_EQ (r.patch->alg, 5);
}

// A DMP with version != 11 is rejected with a non-empty descriptive error.
TEST (PatchLoaderDmp, WrongVersionIsRejected)
{
    std::vector<uint8_t> fixture = makeDmpV11Fixture();
    fixture[0] = 0x08;     // legacy v8 — out of scope (ADR-0012)
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_v8.dmp");

    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A DMP with a non-Genesis system byte is rejected.
TEST (PatchLoaderDmp, WrongSystemByteIsRejected)
{
    std::vector<uint8_t> fixture = makeDmpV11Fixture();
    fixture[1] = 0x05;     // PC Engine, not Genesis
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_wrongsys.dmp");

    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A PSG-type (mode=0, STD) DMP is rejected — the loader is FM-only.
TEST (PatchLoaderDmp, PsgInstrumentTypeIsRejected)
{
    std::vector<uint8_t> fixture = makeDmpV11Fixture();
    fixture[2] = 0x00;     // mode = STD/PSG, not FM
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_psg.dmp");

    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A DMP shorter than the 3-byte minimum header is rejected gracefully.
TEST (PatchLoaderDmp, TooShortFileIsRejected)
{
    const std::vector<uint8_t> tiny { 0x0B };   // just a version byte
    const fs::path tmp = writeTempBinary (tiny, "genvst_dmp_tiny.dmp");

    const PatchLoadResult r = loadDMP (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.patch.has_value());
    EXPECT_FALSE (r.error.empty());
}

// exportTFI then loadTFI round-trips every field TFI carries with zero drift.
// TFI does not store AMS/FMS or AMON, so those are not part of the round-trip.
TEST (PatchExportTfi, RoundTripPreservesAllTfiFields)
{
    Patch p = makeBusyPatch();
    // TFI cannot store these — exclude them from the comparison by zeroing
    // the source patch's copies. The load side will produce 0 regardless.
    p.ams = 0;
    p.pms = 0;
    for (int op = 0; op < 4; ++op)
        p.amon[op] = 0;

    const fs::path tmp = fs::temp_directory_path() / "genvst_rt.tfi";
    const std::string exportErr = exportTFI (p, tmp);
    ASSERT_TRUE (exportErr.empty()) << exportErr;

    const PatchLoadResult r = loadTFI (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    expectCorePatchEqual (p, *r.patch, /*compareExtras=*/ false);
    // Loader must report 0 for the fields TFI does not encode.
    EXPECT_EQ (r.patch->ams, 0);
    EXPECT_EQ (r.patch->pms, 0);
    for (int op = 0; op < 4; ++op)
        EXPECT_EQ (r.patch->amon[op], 0) << "op=" << op;
}

// exportVGI then loadVGI round-trips every field VGI carries, including the
// bit-packed AMS/FMS and per-operator AMON.
TEST (PatchExportVgi, RoundTripPreservesAmsFmsAndAmon)
{
    const Patch p = makeBusyPatch();

    const fs::path tmp = fs::temp_directory_path() / "genvst_rt.vgi";
    const std::string exportErr = exportVGI (p, tmp);
    ASSERT_TRUE (exportErr.empty()) << exportErr;

    const PatchLoadResult r = loadVGI (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.patch.has_value()) << r.error;
    expectCorePatchEqual (p, *r.patch, /*compareExtras=*/ true);
}

// =============================================================================
// DMP PSG (ADR-0026) — loadDmpPsg + tagFromFile content-peek
// =============================================================================

namespace
{
    // Append a 32-bit little-endian signed int — matches Furnace SafeReader::readI
    // on x86 / ARM hosts (see third_party/furnace/src/engine/safeReader.cpp).
    void appendI32LE (std::vector<uint8_t>& out, std::int32_t v)
    {
        const auto u = static_cast<std::uint32_t> (v);
        out.push_back (static_cast<uint8_t> (u      & 0xFF));
        out.push_back (static_cast<uint8_t> ((u>>8) & 0xFF));
        out.push_back (static_cast<uint8_t> ((u>>16)& 0xFF));
        out.push_back (static_cast<uint8_t> ((u>>24)& 0xFF));
    }

    // Append a 1-byte signed-char macro length followed by its values (each 4
    // bytes LE) and a 1-byte loop point if len > 0. Mirrors the DMP v11 STD
    // macro on-disk layout per Furnace's DivEngine::loadDMP STD branch.
    void appendDmpStdMacro (std::vector<uint8_t>& out,
                            const std::vector<std::int32_t>& values,
                            int loop)
    {
        out.push_back (static_cast<uint8_t> (static_cast<std::int8_t> (values.size())));
        for (auto v : values) appendI32LE (out, v);
        if (! values.empty())
            out.push_back (static_cast<uint8_t> (static_cast<std::int8_t> (loop)));
    }

    // Build a minimal Genesis-PSG DMP byte buffer (version 11, sys 0x02,
    // mode 0) carrying the given volume + arpeggio macros plus empty duty
    // and wave macros. arpMode is the 1-byte v9+ arp mode field that lives
    // between the arp loop point and the duty macro.
    std::vector<uint8_t> makeDmpV11PsgFixture (const std::vector<std::int32_t>& vol,
                                               const std::vector<std::int32_t>& arp,
                                               int arpMode = 0)
    {
        std::vector<uint8_t> b;
        b.push_back (0x0B);   // version 11
        b.push_back (0x02);   // sys = Genesis
        b.push_back (0x00);   // mode = STD/PSG
        appendDmpStdMacro (b, vol, /*loop*/ 0);
        appendDmpStdMacro (b, arp, /*loop*/ 0);
        b.push_back (static_cast<uint8_t> (arpMode));
        appendDmpStdMacro (b, {}, /*loop*/ 0);   // duty macro (empty)
        appendDmpStdMacro (b, {}, /*loop*/ 0);   // wave macro (empty)
        return b;
    }

    fs::path writePsgDmpFixture (const std::vector<uint8_t>& bytes, const char* filename)
    {
        return writeTempBinary (bytes, filename);
    }
}

// A hand-built PSG DMP fixture loads via loadDmpPsg and produces ADSR values
// in the PsgPreset hardware ranges. The macro starts at silence (15),
// climbs to peak (0), plateaus at attenuation 4, and the tail returns to
// silence — so the loader should extract a non-zero attack and a non-zero
// sustain drop.
TEST (PatchLoaderDmpPsg, VolumeMacroProducesAdsrInRange)
{
    // Macro: ramp 15→0 in 3 steps, plateau at 4 for 4 ticks, fade to 12 then 15.
    const std::vector<std::int32_t> vol { 15, 10, 5, 0, 4, 4, 4, 4, 12, 15 };
    const fs::path tmp = writePsgDmpFixture (makeDmpV11PsgFixture (vol, {}),
                                             "genvst_dmp_psg_vol.dmp");
    const auto r = loadDmpPsg (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.preset.has_value()) << r.error;
    EXPECT_TRUE (r.warning.empty());     // no arp macro -> no warning

    // All three tone channels share the imported envelope (ADR-0026 step 7).
    for (int i = 0; i < PsgPreset::kNumTones; ++i)
    {
        SCOPED_TRACE (i);
        const auto& t = r.preset->tones[(std::size_t) i];
        EXPECT_GE (t.atk, 1);          // non-zero atk for a 3-step ramp
        EXPECT_LE (t.atk, 31);
        EXPECT_GE (t.dr1, 1);          // non-zero dr1 for the 4-step decay
        EXPECT_LE (t.dr1, 31);
        EXPECT_EQ (t.sus, 4);          // plateau attenuation - peak attenuation
        EXPECT_EQ (t.dr2, 0);
        EXPECT_GE (t.rr, 1);
        EXPECT_LE (t.rr, 15);
        EXPECT_FLOAT_EQ (t.vol, 1.0f);
        EXPECT_FLOAT_EQ (t.pan, 0.0f);
        EXPECT_EQ (t.detune, 0);
    }

    // Noise channel stays silent with the apvts defaults (ADR-0026: DMP PSG
    // does not carry a noise-mode field that maps cleanly to noise.type /
    // noise.rate).
    EXPECT_FLOAT_EQ (r.preset->noise.vol, 0.0f);
    EXPECT_EQ (r.preset->noiseType, "white");
    EXPECT_EQ (r.preset->noiseRate, "mid");
}

// A PSG DMP with a non-empty arpeggio macro emits the "arpeggio dropped"
// warning string for the toast surface.
TEST (PatchLoaderDmpPsg, ArpeggioMacroEmitsWarning)
{
    const std::vector<std::int32_t> vol { 15, 0, 0, 15 };
    const std::vector<std::int32_t> arp { 0, 12, 7, 0 };
    const fs::path tmp = writePsgDmpFixture (makeDmpV11PsgFixture (vol, arp),
                                             "genvst_dmp_psg_arp.dmp");
    const auto r = loadDmpPsg (tmp);
    fs::remove (tmp);

    ASSERT_TRUE (r.preset.has_value()) << r.error;
    EXPECT_FALSE (r.warning.empty());
    EXPECT_NE (r.warning.find ("arpeggio"), std::string::npos);
}

// An FM DMP (mode 1) routed to loadDmpPsg is rejected with a descriptive
// error — the PSG loader does not silently accept FM files.
TEST (PatchLoaderDmpPsg, FmModeDmpIsRejected)
{
    auto fixture = makeDmpV11Fixture();      // mode = 1 (FM)
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_psg_fm.dmp");
    const auto r = loadDmpPsg (tmp);
    fs::remove (tmp);

    EXPECT_FALSE (r.preset.has_value());
    EXPECT_FALSE (r.error.empty());
}

// An unknown mode byte (e.g. 0x03) is rejected with an error rather than
// silently parsed — both loadDMP (FM) and loadDmpPsg reject it.
TEST (PatchLoaderDmpPsg, UnknownModeByteIsRejected)
{
    auto fixture = makeDmpV11Fixture();
    fixture[2] = 0x03;                       // not FM (1) and not PSG (0)
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_psg_mode3.dmp");

    const auto rPsg = loadDmpPsg (tmp);
    EXPECT_FALSE (rPsg.preset.has_value());
    EXPECT_FALSE (rPsg.error.empty());

    const auto rFm  = loadDMP (tmp);
    fs::remove (tmp);
    EXPECT_FALSE (rFm.patch.has_value());
    EXPECT_FALSE (rFm.error.empty());
}

// A wrong-version PSG DMP is rejected (mirrors the FM loader's v11-only scope).
TEST (PatchLoaderDmpPsg, WrongVersionIsRejected)
{
    auto fixture = makeDmpV11PsgFixture ({ 15, 0, 15 }, {});
    fixture[0] = 0x08;                       // legacy v8 — out of scope (ADR-0012)
    const fs::path tmp = writeTempBinary (fixture, "genvst_dmp_psg_v8.dmp");

    const auto r = loadDmpPsg (tmp);
    fs::remove (tmp);
    EXPECT_FALSE (r.preset.has_value());
    EXPECT_FALSE (r.error.empty());
}

// A truncated body (volume macro promises 4 values but file only carries 1)
// fails gracefully rather than reading off the end.
TEST (PatchLoaderDmpPsg, TruncatedBodyIsRejected)
{
    std::vector<uint8_t> b { 0x0B, 0x02, 0x00,
                             /* volMacro.len */ 4,
                             /* only one value, not four */ 0x00, 0x00, 0x00, 0x00 };
    const fs::path tmp = writeTempBinary (b, "genvst_dmp_psg_trunc.dmp");

    const auto r = loadDmpPsg (tmp);
    fs::remove (tmp);
    EXPECT_FALSE (r.preset.has_value());
    EXPECT_FALSE (r.error.empty());
}

// tagFromFile peeks byte 2 of `.dmp` so the browser/dispatcher routes PSG
// DMPs through loadDmpPsg and FM DMPs through loadDMP (ADR-0026).
TEST (TagFromFile, DmpModeByteDispatch)
{
    const auto fmBytes = makeDmpV11Fixture();   // mode = 1
    const fs::path fmTmp = writeTempBinary (fmBytes, "genvst_tag_fm.dmp");
    EXPECT_EQ (tagFromFile (fmTmp), std::optional<Tag> (Tag::FM));
    fs::remove (fmTmp);

    const auto psgBytes = makeDmpV11PsgFixture ({ 15, 0, 15 }, {});
    const fs::path psgTmp = writeTempBinary (psgBytes, "genvst_tag_psg.dmp");
    EXPECT_EQ (tagFromFile (psgTmp), std::optional<Tag> (Tag::SQ));
    fs::remove (psgTmp);
}

// tagFromFile on a missing / unreadable `.dmp` falls back to FM so the FM
// loader's error path surfaces the failure (ADR-0026).
TEST (TagFromFile, DmpUnreadableFallsBackToFm)
{
    const fs::path missing = fs::temp_directory_path() / "genvst_tag_missing.dmp";
    fs::remove (missing);
    EXPECT_EQ (tagFromFile (missing), std::optional<Tag> (Tag::FM));
}

// Non-`.dmp` extensions still flow through tagFromExtension — `.psg` is SQ,
// `.tfi` is FM, an unknown extension is std::nullopt.
TEST (TagFromFile, NonDmpExtensionsDelegate)
{
    EXPECT_EQ (tagFromFile (fs::path { "x.psg" }), std::optional<Tag> (Tag::SQ));
    EXPECT_EQ (tagFromFile (fs::path { "x.tfi" }), std::optional<Tag> (Tag::FM));
    EXPECT_EQ (tagFromFile (fs::path { "x.unknown" }), std::nullopt);
}
