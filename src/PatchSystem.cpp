#include "PatchSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
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

    // Convert a 3-bit YM2612/YM2151 detune register value (0-7) to the patch
    // model's TFI 0-6 encoding. Inverse of FmRegisterMap::detuneToRegister:
    // HW values 0-3 are positive detunes that pass through, 4 is the hardware's
    // "second zero" (no detune) that maps to TFI 0, and 5-7 are negative
    // detunes that shift down by one. Shared by loadY12 (Y12 stores the raw
    // hardware register) and loadOPM (OPM DT1 is also raw register encoding).
    // See ADR-0020 for the rationale.
    uint8_t registerToDetune (uint8_t hw)
    {
        const uint8_t v = static_cast<uint8_t> (hw & 0x07);
        if (v < 4)          return v;
        if (v == 4)         return 0;
        return static_cast<uint8_t> (v - 1);
    }

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

bool isSupportedPatchExtension (std::string_view ext)
{
    if (ext.empty())
        return false;
    std::string lower (ext.size(), '\0');
    std::transform (ext.begin(), ext.end(), lower.begin(),
                    [] (char c)
                    { return static_cast<char> (std::tolower (static_cast<unsigned char> (c))); });
    for (const auto& supported : kSupportedPatchExtensions)
        if (lower == supported)
            return true;
    return false;
}

std::string buildPatchExtensionFilter()
{
    std::string out;
    for (const auto& ext : kSupportedPatchExtensions)
    {
        if (! out.empty())
            out.push_back (';');
        out.push_back ('*');
        out.append (ext);
    }
    return out;
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

PatchLoadResult loadY12 (const std::filesystem::path& path)
{
    const auto maybeBytes = readFileBytes (path);
    if (! maybeBytes.has_value())
        return { std::nullopt, "cannot open file: " + path.string() };

    const std::vector<uint8_t>& bytes = *maybeBytes;
    if (bytes.size() != kY12FileSize)
        return { std::nullopt,
                 "expected " + std::to_string (kY12FileSize) + " bytes, got "
                     + std::to_string (bytes.size()) };

    // Per-operator byte layout verified against Furnace's DivEngine::loadY12
    // in third_party/furnace/src/engine/fileOpsIns.cpp. Each 16-byte operator
    // block packs the YM2612 hardware register state for the operator: the
    // first 7 bytes mirror the chip's per-operator register order 0x30, 0x40,
    // 0x50, 0x60, 0x70, 0x80, 0x90; the trailing 9 bytes are padding.
    //
    //   byte +0 : DT[6:4] | MUL[3:0]   (YM2612 reg 0x30+off)
    //   byte +1 : TL[6:0]              (reg 0x40+off; bit 7 unused)
    //   byte +2 : KS[7:6] | AR[4:0]    (reg 0x50+off)
    //   byte +3 : AM[7]   | DR[4:0]    (reg 0x60+off)
    //   byte +4 : SR[4:0]              (reg 0x70+off; AKA D2R)
    //   byte +5 : SL[7:4] | RR[3:0]    (reg 0x80+off)
    //   byte +6 : SSG-EG[3:0]          (reg 0x90+off)
    //   bytes +7..+15: padding
    //
    // DT is stored as the raw hardware register (0-7) and converted to TFI
    // 0-6 by registerToDetune; Furnace's loader uses (3 + hw) & 7 marked
    // `// ???`, which we don't replicate — the inverse of detuneToRegister is
    // the project's canonical encoding (see FmRegisterMap.cpp and ADR-0020).
    Patch p {};
    for (int op = 0; op < 4; ++op)
    {
        const std::size_t base = static_cast<std::size_t> (op) * 16;
        const uint8_t dtMul = bytes[base + 0];
        p.mul[op]  = static_cast<uint8_t> (dtMul & 0x0F);
        p.dt[op]   = registerToDetune (static_cast<uint8_t> ((dtMul >> 4) & 0x07));
        p.tl[op]   = static_cast<uint8_t> (bytes[base + 1] & 0x7F);

        const uint8_t ksAr = bytes[base + 2];
        p.ks[op]   = static_cast<uint8_t> ((ksAr >> 6) & 0x03);
        p.ar[op]   = static_cast<uint8_t> (ksAr & 0x1F);

        const uint8_t amDr = bytes[base + 3];
        p.amon[op] = static_cast<uint8_t> ((amDr >> 7) & 0x01);
        p.dr[op]   = static_cast<uint8_t> (amDr & 0x1F);

        p.sr[op]   = static_cast<uint8_t> (bytes[base + 4] & 0x1F);

        const uint8_t slRr = bytes[base + 5];
        p.sl[op]   = static_cast<uint8_t> ((slRr >> 4) & 0x0F);
        p.rr[op]   = static_cast<uint8_t> (slRr & 0x0F);

        p.ssg[op]  = clampSsg (bytes[base + 6]);
    }

    // Channel-level bytes follow 04-patch-system.md's Y12 section. Furnace's
    // verified loader only reads ALG (0x40) and FB (0x41); AMS/PMS/LFO at
    // 0x42/0x43/0x45 are taken from the design doc's spec table. clampTo
    // keeps stray bits out of the patch model if the file leaves these as
    // garbage padding.
    //
    // AMON is intentionally NOT re-read from 0x44 — the per-operator bit at
    // byte +3 of each operator block is the authoritative source (matches the
    // YM2612 hardware register 0x60+off bit 7 and Furnace's reference).
    p.alg = clampTo (bytes[0x40], 7);
    p.fb  = clampTo (bytes[0x41], 7);
    p.ams = clampTo (bytes[0x42], 3);
    p.pms = clampTo (bytes[0x43], 7);

    // 0x45: bit 3 = LFO enable, bits 0-2 = LFO rate.
    const uint8_t lfo = bytes[0x45];
    p.lfo_enable = static_cast<uint8_t> ((lfo >> 3) & 0x01);
    p.lfo_rate   = static_cast<uint8_t> (lfo & 0x07);

    // Y12 carries no L/R; default to 3 (both enabled) so the patch sounds,
    // matching loadTFI/loadVGI.
    p.lr   = 3;
    p.name = path.stem().string();
    return { std::move (p), {} };
}

namespace
{
    // Strip leading/trailing ASCII whitespace from a string_view in-place.
    std::string_view trimWhitespace (std::string_view s)
    {
        const auto isWs = [] (unsigned char c)
        { return std::isspace (c) != 0; };
        while (! s.empty() && isWs (static_cast<unsigned char> (s.front())))
            s.remove_prefix (1);
        while (! s.empty() && isWs (static_cast<unsigned char> (s.back())))
            s.remove_suffix (1);
        return s;
    }

    // Parse a base-10 integer from the next whitespace-separated token in `s`,
    // advancing `pos` past it. Returns false if no token is left or the token
    // is not a valid integer; on success writes to `out`.
    bool readNextInt (std::string_view s, std::size_t& pos, int& out)
    {
        const auto isWs = [] (unsigned char c)
        { return std::isspace (c) != 0; };
        while (pos < s.size() && isWs (static_cast<unsigned char> (s[pos])))
            ++pos;
        if (pos >= s.size())
            return false;

        const std::size_t start = pos;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+'))
            ++pos;
        while (pos < s.size() && std::isdigit (static_cast<unsigned char> (s[pos])))
            ++pos;
        if (pos == start)
            return false;

        const auto begin = s.data() + start;
        const auto end   = s.data() + pos;
        int value = 0;
        const auto [ptr, ec] = std::from_chars (begin, end, value);
        if (ec != std::errc{} || ptr != end)
            return false;
        out = value;
        return true;
    }

    // YM2151 OPM operator line (`M1:`/`C1:`/`M2:`/`C2:`) carries 11 integers:
    //   AR D1R D2R RR D1L TL KS MUL DT1 DT2 AMS-EN
    // Returns false on a malformed line; on success writes the operator's
    // fields into `p.<field>[op]` (DT2 is silently dropped per ADR-0019).
    bool parseOpmOperatorLine (std::string_view operands, int op, Patch& p)
    {
        std::size_t pos = 0;
        int values[11];
        for (int i = 0; i < 11; ++i)
            if (! readNextInt (operands, pos, values[i]))
                return false;

        p.ar[op]  = clampTo (static_cast<uint8_t> (values[0]), 31);
        p.dr[op]  = clampTo (static_cast<uint8_t> (values[1]), 31);
        p.sr[op]  = clampTo (static_cast<uint8_t> (values[2]), 31);   // D2R
        p.rr[op]  = clampTo (static_cast<uint8_t> (values[3]), 15);
        p.sl[op]  = clampTo (static_cast<uint8_t> (values[4]), 15);   // D1L
        p.tl[op]  = clampTo (static_cast<uint8_t> (values[5]), 127);
        p.ks[op]  = clampTo (static_cast<uint8_t> (values[6]), 3);
        p.mul[op] = clampTo (static_cast<uint8_t> (values[7]), 15);
        // DT1 is the YM2151 detune register value (0-7); convert to the
        // patch model's TFI 0-6 encoding so FmRegisterMap::detuneToRegister
        // round-trips it correctly. Storing the raw 0-7 here would push the
        // value through detuneToRegister to register value 8, which doesn't
        // fit in the 3-bit DT field. Diverges from 04-patch-system.md's
        // earlier "take 0-7 directly" wording per ADR-0020.
        p.dt[op]  = registerToDetune (static_cast<uint8_t> (values[8] & 0x07));
        // values[9] = DT2 — dropped (ADR-0019: YM2612 has no DT2 register).
        p.amon[op] = (values[10] > 0) ? uint8_t {1} : uint8_t {0};
        return true;
    }
}

PatchLoadResult loadOPM (const std::filesystem::path& path)
{
    std::ifstream file (path);
    if (! file)
        return { std::nullopt, "cannot open file: " + path.string() };

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    // Patch p{} default-initialises every field to zero — SSG-EG[0..3] = 0
    // satisfies ADR-0019's "OPM has no SSG-EG; default to off" without an
    // explicit zero. lr defaults to both-enabled to keep OPM-loaded patches
    // audible.
    Patch p {};
    p.lr = 3;

    bool sawHeader = false, sawLfo = false, sawCh = false;
    bool sawM1 = false, sawC1 = false, sawM2 = false, sawC2 = false;
    bool sawAnyLfoNonZero = false;

    std::size_t lineStart = 0;
    while (lineStart <= text.size())
    {
        std::size_t lineEnd = text.find ('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();

        const std::string_view rawLine (text.data() + lineStart,
                                        lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const std::string_view line = trimWhitespace (rawLine);
        if (line.empty() || (line.size() >= 2 && line.substr (0, 2) == "//"))
            continue;

        // OPM headers are `@:<num> <name>`; everything else is `KEY:operands`.
        if (line.front() == '@')
        {
            if (sawHeader)
                break;       // Multi-instrument: take the first block only (ADR-0019).
            sawHeader = true;

            const auto colon = line.find (':');
            std::string_view body = (colon == std::string_view::npos)
                                        ? std::string_view{}
                                        : trimWhitespace (line.substr (colon + 1));
            // Drop the patch-number token; the rest is the name.
            std::size_t nameStart = 0;
            while (nameStart < body.size()
                   && ! std::isspace (static_cast<unsigned char> (body[nameStart])))
                ++nameStart;
            while (nameStart < body.size()
                   && std::isspace (static_cast<unsigned char> (body[nameStart])))
                ++nameStart;
            p.name = std::string (trimWhitespace (body.substr (nameStart)));
            continue;
        }

        const auto colon = line.find (':');
        if (colon == std::string_view::npos)
            continue;   // Unknown / informational line — skip per VOPM convention.

        const std::string_view key      = trimWhitespace (line.substr (0, colon));
        const std::string_view operands = trimWhitespace (line.substr (colon + 1));

        // Once a non-@ parameter line appears before the first header, the
        // file is malformed for our purposes.
        if (! sawHeader)
            return { std::nullopt, "OPM file has no @: header before parameter lines" };

        if (key == "LFO")
        {
            std::array<int, 5> v {};   // LFRQ AMD PMD WF NFRQ — clamping limit picked per field below.
            std::size_t pos = 0;
            for (int i = 0; i < 5; ++i)
                if (! readNextInt (operands, pos, v[i]))
                    return { std::nullopt, "OPM LFO line: expected 5 integers" };
            // Only LFRQ/AMD/PMD drive `lfo_enable`; WF/NFRQ are OPM-specific.
            p.lfo_rate = clampTo (static_cast<uint8_t> (v[0]), 7);
            sawAnyLfoNonZero = (v[0] != 0) || (v[1] != 0) || (v[2] != 0);
            sawLfo = true;
        }
        else if (key == "CH")
        {
            std::array<int, 7> v {};   // PAN FL CON AMS PMS SLOT NE
            std::size_t pos = 0;
            for (int i = 0; i < 7; ++i)
                if (! readNextInt (operands, pos, v[i]))
                    return { std::nullopt, "OPM CH line: expected 7 integers" };
            // PAN/SLOT/NE are OPM-only and ignored.
            p.fb  = clampTo (static_cast<uint8_t> (v[1]), 7);
            p.alg = clampTo (static_cast<uint8_t> (v[2]), 7);
            p.ams = clampTo (static_cast<uint8_t> (v[3]), 3);
            p.pms = clampTo (static_cast<uint8_t> (v[4]), 7);
            sawCh = true;
        }
        else if (key == "M1" || key == "C1" || key == "M2" || key == "C2")
        {
            // Operator mapping per 04-patch-system.md: M1→OP1 (idx 0), C1→OP2
            // (idx 1), M2→OP3 (idx 2), C2→OP4 (idx 3).
            int op = 0;
            bool* sawFlag = nullptr;
            if      (key == "M1") { op = 0; sawFlag = &sawM1; }
            else if (key == "C1") { op = 1; sawFlag = &sawC1; }
            else if (key == "M2") { op = 2; sawFlag = &sawM2; }
            else                  { op = 3; sawFlag = &sawC2; }

            if (! parseOpmOperatorLine (operands, op, p))
                return { std::nullopt,
                         "OPM " + std::string (key) + " line: expected 11 integers" };
            *sawFlag = true;
        }
        // Any other key (e.g. blank lines or vendor-specific extensions) is
        // ignored quietly per the VOPM convention.
    }

    if (! sawHeader) return { std::nullopt, "OPM file has no @: header" };
    if (! sawLfo)    return { std::nullopt, "OPM file is missing LFO: line" };
    if (! sawCh)     return { std::nullopt, "OPM file is missing CH: line" };
    if (! sawM1)     return { std::nullopt, "OPM file is missing M1: line" };
    if (! sawC1)     return { std::nullopt, "OPM file is missing C1: line" };
    if (! sawM2)     return { std::nullopt, "OPM file is missing M2: line" };
    if (! sawC2)     return { std::nullopt, "OPM file is missing C2: line" };

    // OPM has no explicit LFO enable bit — derive it from any non-zero
    // amplitude/pitch/frequency setting (ADR-0019).
    p.lfo_enable = sawAnyLfoNonZero ? uint8_t {1} : uint8_t {0};

    if (p.name.empty())
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
