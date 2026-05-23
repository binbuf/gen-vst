// VgmExtract unit tests — synthetic .vgm / .vgz byte buffers exercising every
// branch of the parser. The fixtures are hand-built inline so each test reads
// top-to-bottom without per-test file assets to maintain.
//
// Test coverage (Task 21 verification):
//   * SingleKeyOn       — one channel, populated register state, one key-on
//                         emits exactly one patch with the expected fields.
//   * RepeatedKeyOnDedup — identical key-on after no register changes does
//                          not duplicate the patch (content-hash dedup).
//   * MultiChannel      — distinct register state on channels 1/2/3 emits
//                          three patches in observation order.
//   * VgzRoundTrip      — gzipping the same byte buffer and re-extracting
//                          produces the same patch list.
//   * Malformed*        — wrong magic / unsupported version / missing YM2612
//                          chip clock fail with descriptive errors.
//   * NoKeyOns          — YM2612 register writes without a key-on event
//                          surface "no FM patches found".

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <juce_core/juce_core.h>

#include "PatchSystem.h"
#include "VgmExtract.h"

namespace fs = std::filesystem;

namespace
{
    // Minimal VGM-byte builder used by every test. Starts with a VGM 1.50
    // header that advertises a non-zero YM2612 clock and a data offset that
    // lands at 0x40 (the header end). Body commands are appended after the
    // header.
    struct VgmBuilder
    {
        std::vector<uint8_t> bytes;

        explicit VgmBuilder (uint32_t version = 0x00000150,
                             uint32_t ym2612Clock = 7670454u /* NTSC */)
        {
            // 64-byte VGM 1.50 header; every field defaults to zero except
            // the four we set explicitly.
            bytes.assign (0x40, 0);
            bytes[0] = 0x56;   // 'V'
            bytes[1] = 0x67;   // 'g'
            bytes[2] = 0x6D;   // 'm'
            bytes[3] = 0x20;   // ' '
            writeU32LE (0x08, version);
            writeU32LE (0x2C, ym2612Clock);
            writeU32LE (0x34, 0x0Cu);   // data starts at 0x34 + 0x0C = 0x40
        }

        void writeU32LE (std::size_t off, uint32_t v)
        {
            bytes[off]     = static_cast<uint8_t> (v & 0xFF);
            bytes[off + 1] = static_cast<uint8_t> ((v >> 8)  & 0xFF);
            bytes[off + 2] = static_cast<uint8_t> ((v >> 16) & 0xFF);
            bytes[off + 3] = static_cast<uint8_t> ((v >> 24) & 0xFF);
        }

        // Push a YM2612 register write (port 0 -> 0x52, port 1 -> 0x53).
        void ym2612Write (int port, uint8_t reg, uint8_t data)
        {
            bytes.push_back (port == 0 ? uint8_t {0x52} : uint8_t {0x53});
            bytes.push_back (reg);
            bytes.push_back (data);
        }

        // Push a 0x28 key-on for the given operator mask + channel selector.
        // `opMask` is the low nibble (bit 0 = OP1/S1, bit 1 = OP3/S3,
        // bit 2 = OP2/S2, bit 3 = OP4/S4 — matches the YM2612 hardware bit
        // layout). `channelSelector` is the 3-bit selector from the data byte:
        // 0..2 = ch1..3, 4..6 = ch4..6.
        void keyOn (uint8_t opMask, uint8_t channelSelector)
        {
            const uint8_t data = static_cast<uint8_t> (((opMask & 0x0F) << 4)
                                                       | (channelSelector & 0x07));
            ym2612Write (0, 0x28, data);
        }

        // 0x61 nn nn — wait N samples; the parser uses this only to advance
        // the cursor, so the operand value is opaque.
        void waitSamples (uint16_t samples)
        {
            bytes.push_back (0x61);
            bytes.push_back (static_cast<uint8_t> (samples & 0xFF));
            bytes.push_back (static_cast<uint8_t> ((samples >> 8) & 0xFF));
        }

        // 0x62 — wait 735 samples (one 60 Hz frame).
        void waitFrame60()  { bytes.push_back (0x62); }
        // 0x63 — wait 882 samples (one 50 Hz frame).
        void waitFrame50()  { bytes.push_back (0x63); }
        // 0x70..0x7F — wait (n+1) samples; pass `n` as 0..15.
        void waitShort (int n) { bytes.push_back (static_cast<uint8_t> (0x70 + (n & 0x0F))); }

        void endOfStream() { bytes.push_back (0x66); }
    };

    // Write `bytes` to a uniquely-named temp file with the given extension.
    // The caller is responsible for fs::remove; juce::Uuid keeps the names
    // collision-free even when several tests run concurrently.
    fs::path writeTempFile (const std::vector<uint8_t>& bytes,
                            const std::string&          suffix)
    {
        const auto unique = juce::Uuid().toDashedString().toStdString();
        auto path = fs::temp_directory_path() / ("genvst_test_" + unique + suffix);
        std::ofstream out (path, std::ios::binary);
        out.write (reinterpret_cast<const char*> (bytes.data()),
                   static_cast<std::streamsize> (bytes.size()));
        out.close();
        return path;
    }

    // Compress `vgmBytes` with the gzip wire format (windowBitsGZIP) and write
    // the result to a .vgz file. Mirrors the format real .vgz archives use, so
    // the test exercises the same juce::GZIPDecompressorInputStream code path
    // the production loader hits.
    fs::path writeGzippedTempFile (const std::vector<uint8_t>& vgmBytes)
    {
        const auto unique = juce::Uuid().toDashedString().toStdString();
        auto path = fs::temp_directory_path() / ("genvst_test_" + unique + ".vgz");

        juce::FileOutputStream* fileStream = new juce::FileOutputStream (
            juce::File (juce::String (path.string())));
        // `juce::FileOutputStream` is created in append mode by default —
        // explicitly truncate so a re-run of the same UUID would not concat.
        fileStream->setPosition (0);
        fileStream->truncate();

        {
            juce::GZIPCompressorOutputStream gz (
                fileStream,
                /* compressionLevel */ 6,
                /* deleteDestStreamWhenDestroyed */ true,
                juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gz.write (vgmBytes.data(), vgmBytes.size());
            gz.flush();
        }   // gz dtor closes the gzip stream and deletes fileStream

        return path;
    }

    // ------------------------------------------------------------------------
    // Fluent helpers for building a one-channel patch in the byte stream.
    // Each routine writes the seven per-operator registers for a hardware slot
    // and the two channel-level registers; the per-test setup picks values to
    // make assertions readable.
    // ------------------------------------------------------------------------

    // Per-Patch-OP index -> YM2612 hardware slot offset within port. Mirrors
    // FmRegisterMap::kOperatorRegOffset so a patch round-trips identically
    // through buildNoteOn after extraction.
    constexpr uint8_t kOpToSlotOffset[4] = { 0x00, 0x08, 0x04, 0x0C };

    struct OpRegs
    {
        uint8_t dt   = 0;   // TFI 0-6
        uint8_t mul  = 0;
        uint8_t tl   = 0;
        uint8_t ks   = 0;
        uint8_t ar   = 0;
        uint8_t amOn = 0;
        uint8_t dr   = 0;
        uint8_t sr   = 0;
        uint8_t sl   = 0;
        uint8_t rr   = 0;
        uint8_t ssg  = 0;
    };

    // Convert TFI detune (0-6) to the YM2612 hardware register value (0-7).
    // Inverse of VgmExtract.cpp's registerToTfiDetune; mirrors
    // FmRegisterMap::detuneToRegister.
    uint8_t tfiDetuneToHwRegister (uint8_t tfi)
    {
        return static_cast<uint8_t> (tfi < 4 ? tfi : tfi + 1);
    }

    void writeOperator (VgmBuilder& vgm, int port, int subChannel, int opIndex,
                        const OpRegs& r)
    {
        const uint8_t slotOff = kOpToSlotOffset[opIndex];
        const uint8_t lo = static_cast<uint8_t> (slotOff | (subChannel & 0x03));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x30 | lo),
                         static_cast<uint8_t> ((tfiDetuneToHwRegister (r.dt) << 4)
                                                | (r.mul & 0x0F)));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x40 | lo),
                         static_cast<uint8_t> (r.tl & 0x7F));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x50 | lo),
                         static_cast<uint8_t> (((r.ks & 0x03) << 6) | (r.ar & 0x1F)));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x60 | lo),
                         static_cast<uint8_t> (((r.amOn & 0x01) << 7) | (r.dr & 0x1F)));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x70 | lo),
                         static_cast<uint8_t> (r.sr & 0x1F));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x80 | lo),
                         static_cast<uint8_t> (((r.sl & 0x0F) << 4) | (r.rr & 0x0F)));
        vgm.ym2612Write (port, static_cast<uint8_t> (0x90 | lo),
                         static_cast<uint8_t> (r.ssg & 0x0F));
    }

    struct ChannelRegs
    {
        uint8_t alg = 0, fb = 0;
        uint8_t ams = 0, pms = 0, l = 1, r = 1;
        OpRegs op[4];
    };

    // Write every register for one channel + key-on. `channelSelector` is the
    // 3-bit value that lands in the 0x28 data byte's low nibble (0..2 = port 0
    // channels, 4..6 = port 1 channels). `subChannel` is the lower 2 bits of
    // `channelSelector` and `port` is the high bit.
    void writeChannelAndKeyOn (VgmBuilder& vgm, uint8_t channelSelector,
                               const ChannelRegs& c)
    {
        const int port      = (channelSelector & 0x04) ? 1 : 0;
        const int subCh     = channelSelector & 0x03;
        const uint8_t lo    = static_cast<uint8_t> (subCh & 0x03);

        // 0xB0 + subCh: FB | ALG
        vgm.ym2612Write (port, static_cast<uint8_t> (0xB0 | lo),
                         static_cast<uint8_t> (((c.fb & 0x07) << 3) | (c.alg & 0x07)));
        // 0xB4 + subCh: L | R | AMS | PMS
        vgm.ym2612Write (port, static_cast<uint8_t> (0xB4 | lo),
                         static_cast<uint8_t> (((c.l & 0x01) << 7)
                                                | ((c.r & 0x01) << 6)
                                                | ((c.ams & 0x03) << 4)
                                                | (c.pms & 0x07)));
        // Per-operator state.
        for (int op = 0; op < 4; ++op)
            writeOperator (vgm, port, subCh, op, c.op[op]);

        // Key-on: all four operators on, channel selector identifying the channel.
        vgm.keyOn (0x0F, channelSelector);
    }

    // Distinct register state per channel: each operator's MUL takes a unique
    // value so we can pinpoint which slot offset wrote which patch field.
    ChannelRegs distinctChannel (int seed)
    {
        ChannelRegs c {};
        c.alg = static_cast<uint8_t> ((2 + seed) & 0x07);
        c.fb  = static_cast<uint8_t> ((5 + seed) & 0x07);
        c.ams = static_cast<uint8_t> ((1 + seed) & 0x03);
        c.pms = static_cast<uint8_t> ((3 + seed) & 0x07);
        c.l   = 1;
        c.r   = 1;
        for (int op = 0; op < 4; ++op)
        {
            c.op[op].dt   = static_cast<uint8_t> ((op + 1) & 0x07);
            c.op[op].mul  = static_cast<uint8_t> (((seed * 4) + (op * 2) + 1) & 0x0F);
            c.op[op].tl   = static_cast<uint8_t> ((10 + (op * 5) + seed) & 0x7F);
            c.op[op].ks   = static_cast<uint8_t> ((op) & 0x03);
            c.op[op].ar   = static_cast<uint8_t> ((20 + op) & 0x1F);
            c.op[op].amOn = static_cast<uint8_t> ((seed + op) & 0x01);
            c.op[op].dr   = static_cast<uint8_t> ((18 + op) & 0x1F);
            c.op[op].sr   = static_cast<uint8_t> ((15 + op) & 0x1F);
            c.op[op].sl   = static_cast<uint8_t> ((8 + op) & 0x0F);
            c.op[op].rr   = static_cast<uint8_t> ((7 + op) & 0x0F);
            c.op[op].ssg  = static_cast<uint8_t> (op % 2 == 0 ? 0 : 0x0C);
        }
        return c;
    }

    // Compare the loaded patch against the channel-regs spec. SCOPED_TRACE
    // makes per-field failures point straight to the operator index.
    void expectPatchMatches (const Patch& p, const ChannelRegs& c)
    {
        EXPECT_EQ ((int) p.alg, (int) c.alg);
        EXPECT_EQ ((int) p.fb,  (int) c.fb);
        EXPECT_EQ ((int) p.ams, (int) c.ams);
        EXPECT_EQ ((int) p.pms, (int) c.pms);
        EXPECT_EQ ((int) p.lr,  (int) ((c.l << 1) | c.r));

        for (int op = 0; op < 4; ++op)
        {
            SCOPED_TRACE ("OP" + std::to_string (op + 1));
            EXPECT_EQ ((int) p.dt[op],   (int) c.op[op].dt);
            EXPECT_EQ ((int) p.mul[op],  (int) c.op[op].mul);
            EXPECT_EQ ((int) p.tl[op],   (int) c.op[op].tl);
            EXPECT_EQ ((int) p.ks[op],   (int) c.op[op].ks);
            EXPECT_EQ ((int) p.ar[op],   (int) c.op[op].ar);
            EXPECT_EQ ((int) p.amon[op], (int) c.op[op].amOn);
            EXPECT_EQ ((int) p.dr[op],   (int) c.op[op].dr);
            EXPECT_EQ ((int) p.sr[op],   (int) c.op[op].sr);
            EXPECT_EQ ((int) p.sl[op],   (int) c.op[op].sl);
            EXPECT_EQ ((int) p.rr[op],   (int) c.op[op].rr);
            EXPECT_EQ ((int) p.ssg[op],  (int) c.op[op].ssg);
        }
    }
}

// =============================================================================
// Single key-on
// =============================================================================

TEST (VgmExtract, SingleKeyOnEmitsOnePatch)
{
    VgmBuilder vgm;
    // Global LFO state: enabled at rate 4.
    vgm.ym2612Write (0, 0x22, static_cast<uint8_t> ((1 << 3) | 4));

    const ChannelRegs c = distinctChannel (0);
    writeChannelAndKeyOn (vgm, /* selector ch1 */ 0, c);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    ASSERT_EQ (patches.size(), 1u) << "error=" << error;
    EXPECT_TRUE (error.empty());

    const auto& p = patches[0];
    expectPatchMatches (p, c);
    EXPECT_EQ ((int) p.lfo_enable, 1);
    EXPECT_EQ ((int) p.lfo_rate, 4);
    EXPECT_NE (p.name.find ("#1"), std::string::npos);
}

// =============================================================================
// Dedup: repeated key-on with no register changes -> still one patch
// =============================================================================

TEST (VgmExtract, RepeatedKeyOnDedupes)
{
    VgmBuilder vgm;
    const ChannelRegs c = distinctChannel (1);
    writeChannelAndKeyOn (vgm, 0, c);
    // Second key-on on the same channel with identical state — must not
    // produce a second patch entry.
    vgm.waitSamples (1024);
    vgm.keyOn (0x0F, 0);
    // Third one with the same state.
    vgm.keyOn (0x0F, 0);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    ASSERT_EQ (patches.size(), 1u) << "error=" << error;
    expectPatchMatches (patches[0], c);
}

// =============================================================================
// Multi-channel: distinct patches on channels 1/2/3 -> three patches in order
// =============================================================================

TEST (VgmExtract, MultiChannelEmitsOnePerChannel)
{
    VgmBuilder vgm;
    const ChannelRegs c1 = distinctChannel (0);
    const ChannelRegs c2 = distinctChannel (1);
    const ChannelRegs c3 = distinctChannel (2);

    writeChannelAndKeyOn (vgm, 0, c1);   // ch1
    writeChannelAndKeyOn (vgm, 1, c2);   // ch2
    writeChannelAndKeyOn (vgm, 2, c3);   // ch3
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    ASSERT_EQ (patches.size(), 3u) << "error=" << error;
    expectPatchMatches (patches[0], c1);
    expectPatchMatches (patches[1], c2);
    expectPatchMatches (patches[2], c3);
    EXPECT_NE (patches[0].name.find ("#1"), std::string::npos);
    EXPECT_NE (patches[1].name.find ("#2"), std::string::npos);
    EXPECT_NE (patches[2].name.find ("#3"), std::string::npos);
}

// =============================================================================
// Multi-port (channels 4-6 via 0x53 / port 1)
// =============================================================================

TEST (VgmExtract, KeyOnPort1ChannelsMapToCorrectIndices)
{
    VgmBuilder vgm;
    const ChannelRegs c5 = distinctChannel (3);
    // Channel selector 5 = port 1 (bit 2 set) + sub-channel 1 → ch5.
    writeChannelAndKeyOn (vgm, /* selector ch5 */ 5, c5);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    ASSERT_EQ (patches.size(), 1u) << "error=" << error;
    expectPatchMatches (patches[0], c5);
}

// =============================================================================
// VGZ round-trip: gzipped VGM produces the same patch list
// =============================================================================

TEST (VgmExtract, VgzRoundTripsToSamePatches)
{
    VgmBuilder vgm;
    const ChannelRegs c1 = distinctChannel (0);
    const ChannelRegs c2 = distinctChannel (1);
    writeChannelAndKeyOn (vgm, 0, c1);
    writeChannelAndKeyOn (vgm, 1, c2);
    vgm.endOfStream();

    // Baseline: uncompressed .vgm.
    const auto vgmPath = writeTempFile (vgm.bytes, ".vgm");
    std::string vgmErr;
    const auto vgmPatches = extractFmPatches (vgmPath, vgmErr);
    fs::remove (vgmPath);
    ASSERT_EQ (vgmPatches.size(), 2u) << "error=" << vgmErr;

    // Gzipped equivalent.
    const auto vgzPath = writeGzippedTempFile (vgm.bytes);
    std::string vgzErr;
    const auto vgzPatches = extractFmPatches (vgzPath, vgzErr);
    fs::remove (vgzPath);
    ASSERT_EQ (vgzPatches.size(), 2u) << "error=" << vgzErr;

    // Same field values; names share the same #1/#2 suffix because both runs
    // observe identical key-on events in the same order, but the file stem
    // (and therefore the patch name prefix) differs by temp file. Compare the
    // numeric suffix shape rather than the full string.
    for (std::size_t i = 0; i < 2; ++i)
    {
        SCOPED_TRACE ("patch " + std::to_string (i));
        EXPECT_EQ ((int) vgzPatches[i].alg, (int) vgmPatches[i].alg);
        EXPECT_EQ ((int) vgzPatches[i].fb,  (int) vgmPatches[i].fb);
        for (int op = 0; op < 4; ++op)
        {
            EXPECT_EQ ((int) vgzPatches[i].mul[op], (int) vgmPatches[i].mul[op]);
            EXPECT_EQ ((int) vgzPatches[i].tl[op],  (int) vgmPatches[i].tl[op]);
            EXPECT_EQ ((int) vgzPatches[i].ar[op],  (int) vgmPatches[i].ar[op]);
            EXPECT_EQ ((int) vgzPatches[i].dr[op],  (int) vgmPatches[i].dr[op]);
        }
    }
}

// =============================================================================
// Malformed inputs
// =============================================================================

TEST (VgmExtract, MalformedHeaderMagicIsRejected)
{
    VgmBuilder vgm;
    vgm.bytes[0] = 'X';   // break the 'V' of "Vgm "
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    EXPECT_TRUE (patches.empty());
    EXPECT_FALSE (error.empty());
    EXPECT_NE (error.find ("magic"), std::string::npos)
        << "got error: " << error;
}

TEST (VgmExtract, MalformedHeaderUnsupportedVersionIsRejected)
{
    VgmBuilder vgm (/* version */ 0x00000100);   // VGM 1.00 — too old
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    EXPECT_TRUE (patches.empty());
    EXPECT_FALSE (error.empty());
    EXPECT_NE (error.find ("version"), std::string::npos)
        << "got error: " << error;
}

TEST (VgmExtract, MalformedHeaderMissingYm2612IsRejected)
{
    VgmBuilder vgm (/* version */ 0x00000150, /* ym2612Clock */ 0u);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    EXPECT_TRUE (patches.empty());
    EXPECT_FALSE (error.empty());
    EXPECT_NE (error.find ("YM2612"), std::string::npos)
        << "got error: " << error;
}

// =============================================================================
// No key-ons: YM2612 register writes but no 0x28 key-on event
// =============================================================================

TEST (VgmExtract, NoKeyOnsSurfacesDescriptiveError)
{
    VgmBuilder vgm;
    // Set up some FM register state but never key-on. Walk every wait
    // command shape too so this test also confirms the parser walks them
    // forward correctly without ever finding a patch.
    vgm.ym2612Write (0, 0xB0, 0x2A);    // ALG=2, FB=5 on ch1
    vgm.waitSamples (1024);
    vgm.waitFrame60();
    vgm.waitFrame50();
    vgm.waitShort (15);
    // 0x28 write with all-zero operator mask — that's a key-OFF, not a key-on.
    vgm.ym2612Write (0, 0x28, 0x00);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    EXPECT_TRUE (patches.empty());
    EXPECT_FALSE (error.empty());
    EXPECT_NE (error.find ("no FM patches"), std::string::npos)
        << "got error: " << error;
}

// =============================================================================
// Non-existent file: open failure surfaces a descriptive error
// =============================================================================

TEST (VgmExtract, NonExistentFileSurfacesOpenError)
{
    const auto path = fs::temp_directory_path() / "genvst_test_does_not_exist.vgm";
    fs::remove (path);   // make sure
    std::string error;
    const auto patches = extractFmPatches (path, error);

    EXPECT_TRUE (patches.empty());
    EXPECT_FALSE (error.empty());
}

// =============================================================================
// Skip unknown commands: the parser walks past a byte stream containing every
// command-length category without crashing or losing the trailing key-on.
// =============================================================================

TEST (VgmExtract, SkipsUnknownCommandsCorrectly)
{
    VgmBuilder vgm;
    // 0x50 (PSG write, 1 operand)
    vgm.bytes.push_back (0x50); vgm.bytes.push_back (0x9F);
    // 0xA0..0xBF (2 operands) — pick 0xA0 (AY8910 write)
    vgm.bytes.push_back (0xA0); vgm.bytes.push_back (0x07); vgm.bytes.push_back (0x3F);
    // 0xC0..0xDF (3 operands)
    vgm.bytes.push_back (0xC0); vgm.bytes.push_back (0x11); vgm.bytes.push_back (0x22); vgm.bytes.push_back (0x33);
    // 0xE0..0xFF (4 operands)
    vgm.bytes.push_back (0xE0); vgm.bytes.push_back (0x44); vgm.bytes.push_back (0x55); vgm.bytes.push_back (0x66); vgm.bytes.push_back (0x77);
    // 0x67 data block — 4 bytes of payload after the 6-byte header.
    vgm.bytes.push_back (0x67); vgm.bytes.push_back (0x66); vgm.bytes.push_back (0x00);
    vgm.bytes.push_back (0x04); vgm.bytes.push_back (0x00); vgm.bytes.push_back (0x00); vgm.bytes.push_back (0x00);
    vgm.bytes.push_back (0xDE); vgm.bytes.push_back (0xAD); vgm.bytes.push_back (0xBE); vgm.bytes.push_back (0xEF);
    // 0x68 PCM RAM write — 11 operand bytes.
    vgm.bytes.push_back (0x68);
    for (int i = 0; i < 11; ++i) vgm.bytes.push_back (0x00);
    // Then a normal channel + key-on so the test still sees a patch.
    const ChannelRegs c = distinctChannel (0);
    writeChannelAndKeyOn (vgm, 0, c);
    vgm.endOfStream();

    const auto path = writeTempFile (vgm.bytes, ".vgm");
    std::string error;
    const auto patches = extractFmPatches (path, error);
    fs::remove (path);

    ASSERT_EQ (patches.size(), 1u) << "error=" << error;
    expectPatchMatches (patches[0], c);
}
