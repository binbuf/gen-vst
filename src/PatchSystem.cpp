#include "PatchSystem.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    // Clamp a raw file byte into [0, maxValue]. An out-of-range byte saturates
    // to the range edge rather than wrapping, so corrupt data degrades quietly.
    uint8_t clampTo (uint8_t raw, int maxValue)
    {
        return static_cast<uint8_t> (std::clamp (static_cast<int> (raw), 0, maxValue));
    }

    // SSG-EG is valid only as 0 (off) or 8-15 (on + 3-bit shape); 1-7 are
    // invalid. Hardware register 0x90 uses bit 3 as the enable, so any value
    // with bit 3 clear is "off" — values 1-7 therefore collapse to 0.
    uint8_t clampSsg (uint8_t raw)
    {
        const uint8_t low4 = static_cast<uint8_t> (raw & 0x0F);
        return static_cast<uint8_t> ((low4 & 0x08) != 0 ? low4 : 0);
    }

    // DMP v11 header byte values that the Genesis FM loader accepts. Verified
    // against Furnace's DivEngine::loadDMP — see the note on kDmpV11FmFileSize.
    constexpr uint8_t kDmpVersion       = 0x0B;
    constexpr uint8_t kDmpSysGenesis    = 0x02;
    constexpr uint8_t kDmpSysGenesisExt = 0x42;
    constexpr uint8_t kDmpModeFm        = 0x01;  // Furnace: mode==1 → FM, mode==0 → STD/PSG

    // Read an entire binary file into a byte vector. Returns std::nullopt on
    // open failure (the caller turns that into a PatchLoadResult error).
    std::optional<std::vector<uint8_t>> readFileBytes (const std::filesystem::path& path)
    {
        std::ifstream file (path, std::ios::binary);
        if (! file)
            return std::nullopt;

        const std::istreambuf_iterator<char> first (file);
        const std::istreambuf_iterator<char> last;
        return std::vector<uint8_t> (first, last);
    }
}

PatchLoadResult loadTFI (const std::filesystem::path& path)
{
    std::ifstream file (path, std::ios::binary);
    if (! file)
        return { std::nullopt, "cannot open file: " + path.string() };

    const std::istreambuf_iterator<char> first (file);
    const std::istreambuf_iterator<char> last;
    const std::vector<uint8_t> bytes (first, last);

    if (bytes.size() != kTfiFileSize)
        return { std::nullopt,
                 "expected " + std::to_string (kTfiFileSize) + " bytes, got "
                     + std::to_string (bytes.size()) };

    Patch p {};
    p.alg = clampTo (bytes[0], 7);
    p.fb  = clampTo (bytes[1], 7);

    // Operators are stored sequentially OP1, OP2, OP3, OP4 — 10 bytes each.
    for (int op = 0; op < 4; ++op)
    {
        const int base = 2 + op * 10;
        p.mul[op] = clampTo (bytes[base + 0], 15);
        p.dt[op]  = clampTo (bytes[base + 1], 6);    // TFI DT range is 0-6
        p.tl[op]  = clampTo (bytes[base + 2], 127);
        p.ks[op]  = clampTo (bytes[base + 3], 3);
        p.ar[op]  = clampTo (bytes[base + 4], 31);
        p.dr[op]  = clampTo (bytes[base + 5], 31);
        p.sr[op]  = clampTo (bytes[base + 6], 31);
        p.rr[op]  = clampTo (bytes[base + 7], 15);
        p.sl[op]  = clampTo (bytes[base + 8], 15);
        p.ssg[op] = clampSsg (bytes[base + 9]);
    }

    // TFI carries no FMS/PMS, AMS, AMON or LFO data — those keep their zero
    // defaults. L/R keeps its default of 3 (both enabled) so the patch sounds.
    p.name = path.stem().string();

    return { std::move (p), {} };
}

PatchLoadResult loadVGI (const std::filesystem::path& path)
{
    const auto maybeBytes = readFileBytes (path);
    if (! maybeBytes.has_value())
        return { std::nullopt, "cannot open file: " + path.string() };

    const std::vector<uint8_t>& bytes = *maybeBytes;
    if (bytes.size() != kVgiFileSize)
        return { std::nullopt,
                 "expected " + std::to_string (kVgiFileSize) + " bytes, got "
                     + std::to_string (bytes.size()) };

    Patch p {};
    p.alg = clampTo (bytes[0], 7);
    p.fb  = clampTo (bytes[1], 7);

    // Byte 0x02 is AMD/FMD packed as 0b00AA0FFF: bits 5:4 = AMS (0-3),
    // bits 2:0 = FMS/PMS (0-7).
    p.ams = static_cast<uint8_t> ((bytes[2] >> 4) & 0x03);
    p.pms = static_cast<uint8_t> (bytes[2] & 0x07);

    // Operators are stored sequentially OP1..OP4, 10 bytes each, shifted one
    // byte vs TFI to make room for AMS/FMS. The DR byte packs AMON in bit 7
    // and the 5-bit DR value in bits 4:0.
    //
    // TL range: plutiedev.com/format-tfi (verified 2026-05) treats VGI as
    // "almost identical to TFI except an extra byte after feedback" — no
    // per-operator TL difference, so all four operators clamp to 0..127
    // (matching TFI). Resolves the "VGI TL range" open question in 02/07.
    for (int op = 0; op < 4; ++op)
    {
        const int base = 3 + op * 10;
        p.mul[op]  = clampTo (bytes[base + 0], 15);
        p.dt[op]   = clampTo (bytes[base + 1], 6);
        p.tl[op]   = clampTo (bytes[base + 2], 127);
        p.ks[op]   = clampTo (bytes[base + 3], 3);
        p.ar[op]   = clampTo (bytes[base + 4], 31);

        const uint8_t drByte = bytes[base + 5];
        p.amon[op] = static_cast<uint8_t> ((drByte >> 7) & 0x01);
        p.dr[op]   = static_cast<uint8_t> (drByte & 0x1F);

        p.sr[op]   = clampTo (bytes[base + 6], 31);
        p.rr[op]   = clampTo (bytes[base + 7], 15);
        p.sl[op]   = clampTo (bytes[base + 8], 15);
        p.ssg[op]  = clampSsg (bytes[base + 9]);
    }

    p.name = path.stem().string();
    return { std::move (p), {} };
}

PatchLoadResult loadDMP (const std::filesystem::path& path)
{
    const auto maybeBytes = readFileBytes (path);
    if (! maybeBytes.has_value())
        return { std::nullopt, "cannot open file: " + path.string() };

    const std::vector<uint8_t>& bytes = *maybeBytes;

    // Need at least version/system/mode to decide whether to accept the file.
    if (bytes.size() < 3)
        return { std::nullopt,
                 "DMP file too short (" + std::to_string (bytes.size())
                     + " bytes); need at least 3 for header" };

    const uint8_t version = bytes[0];
    if (version != kDmpVersion)
        return { std::nullopt,
                 "DMP version " + std::to_string (static_cast<int> (version))
                     + " not supported; only version 11 is accepted (ADR-0012)" };

    const uint8_t sys = bytes[1];
    if (sys != kDmpSysGenesis && sys != kDmpSysGenesisExt)
        return { std::nullopt,
                 "DMP system byte " + std::to_string (static_cast<int> (sys))
                     + " not supported; expected 0x02 or 0x42 (Genesis)" };

    // Furnace stores the FM/STD selector as a single mode byte where 1 = FM
    // and 0 = STD/PSG. The design doc's table inverts these — the Furnace
    // source is authoritative for real-world files (see header comment on
    // kDmpV11FmFileSize). Reject PSG-type instruments outright.
    const uint8_t mode = bytes[2];
    if (mode != kDmpModeFm)
        return { std::nullopt, "DMP is a PSG/STD instrument; only FM is supported" };

    if (bytes.size() != kDmpV11FmFileSize)
        return { std::nullopt,
                 "DMP v11 Genesis FM expected " + std::to_string (kDmpV11FmFileSize)
                     + " bytes, got " + std::to_string (bytes.size()) };

    // Header (post-mode): FMS, FB, ALG, AMS — same order Furnace reads.
    Patch p {};
    p.pms = clampTo (bytes[3], 7);   // FMS in DefleMask = PMS on YM2612
    p.fb  = clampTo (bytes[4], 7);
    p.alg = clampTo (bytes[5], 7);
    p.ams = clampTo (bytes[6], 3);

    // Per-operator block, 11 bytes each, in Furnace's read order:
    //   mult, tl, ar, dr, sl, rr, am, rs, dt(+dt2 hi-nibble), d2r, ssgEnv.
    // dt2 is OPM-only and discarded; d2r maps to our second-decay (sr).
    for (int op = 0; op < 4; ++op)
    {
        const int base = 7 + op * 11;
        p.mul[op]  = clampTo (bytes[base + 0],  15);
        p.tl[op]   = clampTo (bytes[base + 1], 127);
        p.ar[op]   = clampTo (bytes[base + 2],  31);
        p.dr[op]   = clampTo (bytes[base + 3],  31);
        p.sl[op]   = clampTo (bytes[base + 4],  15);
        p.rr[op]   = clampTo (bytes[base + 5],  15);
        p.amon[op] = clampTo (bytes[base + 6],   1);
        p.ks[op]   = clampTo (bytes[base + 7],   3);   // RS
        p.dt[op]   = clampTo (static_cast<uint8_t> (bytes[base + 8] & 0x0F), 6);
        p.sr[op]   = clampTo (bytes[base + 9],  31);   // D2R
        p.ssg[op]  = clampSsg (bytes[base + 10]);
    }

    p.name = path.stem().string();
    return { std::move (p), {} };
}

std::string exportTFI (const Patch& p, const std::filesystem::path& path)
{
    std::array<uint8_t, kTfiFileSize> bytes {};
    bytes[0] = static_cast<uint8_t> (p.alg & 0x07);
    bytes[1] = static_cast<uint8_t> (p.fb  & 0x07);

    for (int op = 0; op < 4; ++op)
    {
        const std::size_t base = 2 + static_cast<std::size_t> (op) * 10;
        bytes[base + 0] = static_cast<uint8_t> (p.mul[op] & 0x0F);
        bytes[base + 1] = static_cast<uint8_t> (p.dt[op]  & 0x07);
        bytes[base + 2] = static_cast<uint8_t> (p.tl[op]  & 0x7F);
        bytes[base + 3] = static_cast<uint8_t> (p.ks[op]  & 0x03);
        bytes[base + 4] = static_cast<uint8_t> (p.ar[op]  & 0x1F);
        bytes[base + 5] = static_cast<uint8_t> (p.dr[op]  & 0x1F);
        bytes[base + 6] = static_cast<uint8_t> (p.sr[op]  & 0x1F);
        bytes[base + 7] = static_cast<uint8_t> (p.rr[op]  & 0x0F);
        bytes[base + 8] = static_cast<uint8_t> (p.sl[op]  & 0x0F);
        bytes[base + 9] = p.ssg[op];
    }

    std::ofstream file (path, std::ios::binary | std::ios::trunc);
    if (! file)
        return "cannot open file for write: " + path.string();
    file.write (reinterpret_cast<const char*> (bytes.data()),
                static_cast<std::streamsize> (bytes.size()));
    if (! file)
        return "write failed: " + path.string();
    return {};
}

std::string exportVGI (const Patch& p, const std::filesystem::path& path)
{
    std::array<uint8_t, kVgiFileSize> bytes {};
    bytes[0] = static_cast<uint8_t> (p.alg & 0x07);
    bytes[1] = static_cast<uint8_t> (p.fb  & 0x07);
    // 0b00AA0FFF: AMS in bits 5:4, PMS/FMS in bits 2:0.
    bytes[2] = static_cast<uint8_t> (((p.ams & 0x03) << 4) | (p.pms & 0x07));

    for (int op = 0; op < 4; ++op)
    {
        const std::size_t base = 3 + static_cast<std::size_t> (op) * 10;
        bytes[base + 0] = static_cast<uint8_t> (p.mul[op] & 0x0F);
        bytes[base + 1] = static_cast<uint8_t> (p.dt[op]  & 0x07);
        bytes[base + 2] = static_cast<uint8_t> (p.tl[op]  & 0x7F);
        bytes[base + 3] = static_cast<uint8_t> (p.ks[op]  & 0x03);
        bytes[base + 4] = static_cast<uint8_t> (p.ar[op]  & 0x1F);
        // DR byte: bit 7 = AMON, bits 4:0 = DR. Bits 6:5 are unused.
        bytes[base + 5] = static_cast<uint8_t> (((p.amon[op] & 0x01) << 7)
                                                | (p.dr[op]   & 0x1F));
        bytes[base + 6] = static_cast<uint8_t> (p.sr[op]  & 0x1F);
        bytes[base + 7] = static_cast<uint8_t> (p.rr[op]  & 0x0F);
        bytes[base + 8] = static_cast<uint8_t> (p.sl[op]  & 0x0F);
        bytes[base + 9] = p.ssg[op];
    }

    std::ofstream file (path, std::ios::binary | std::ios::trunc);
    if (! file)
        return "cannot open file for write: " + path.string();
    file.write (reinterpret_cast<const char*> (bytes.data()),
                static_cast<std::streamsize> (bytes.size()));
    if (! file)
        return "write failed: " + path.string();
    return {};
}
