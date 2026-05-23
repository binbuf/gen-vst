#include "VgmExtract.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <unordered_set>

#include <juce_core/juce_core.h>

namespace
{
    // YM2612 native sample-rate clock at NTSC. Used only as a sanity check —
    // the parser does not synthesize audio, so the actual clock value is not
    // referenced after we've confirmed it's non-zero.
    constexpr uint32_t kVgm150 = 0x00000150;
    constexpr uint32_t kVgm160 = 0x00000160;

    // Shadow register state for one FM channel. Defaults match the Patch
    // model's defaults so a key-on without any prior writes still produces a
    // deterministic patch — important when a VGM file starts mid-state.
    struct ChannelState
    {
        uint8_t alg = 0, fb = 0;
        uint8_t ams = 0, pms = 0, lr = 3;
        uint8_t lfo_enable = 0, lfo_rate = 0;
        uint8_t mul[4]  {};
        uint8_t dt[4]   {};
        uint8_t tl[4]   {};
        uint8_t ks[4]   {};
        uint8_t ar[4]   {};
        uint8_t dr[4]   {};
        uint8_t sr[4]   {};
        uint8_t rr[4]   {};
        uint8_t sl[4]   {};
        uint8_t ssg[4]  {};
        uint8_t amon[4] {};
    };

    // YM2612 per-operator register block layout (within 0x30..0x9F): the lower
    // 2 bits select the sub-channel (0..2 within a port), the next 2 bits
    // select the hardware slot (S1 +0x00, S3 +0x04, S2 +0x08, S4 +0x0C).
    // Hardware slot order does NOT match the patch's OP1..OP4 numbering — S2
    // and S3 are swapped relative to their numbers. Mirror of
    // FmRegisterMap::kOperatorRegOffset so the parser produces patches that
    // round-trip identically through buildNoteOn.
    int slotOffsetToOpIndex (int slotOffset) noexcept
    {
        switch (slotOffset)
        {
            case 0x00: return 0;   // S1 -> OP1
            case 0x04: return 2;   // S3 -> OP3
            case 0x08: return 1;   // S2 -> OP2
            case 0x0C: return 3;   // S4 -> OP4
            default:   return -1;  // 0x03 / 0x07 / 0x0B / 0x0F are not valid slots
        }
    }

    // Convert the YM2612 hardware DT register field (0-7) into the Patch
    // model's TFI 0-6 encoding. Inverse of FmRegisterMap::detuneToRegister:
    // hardware 0-3 pass through, hardware 4 is the chip's "second zero" that
    // collapses to TFI 0, and hardware 5-7 shift down by one to fit the
    // 3-bit field. Identical to PatchSystem.cpp's `registerToDetune`; kept
    // local so VgmExtract doesn't expose that helper to the whole project.
    uint8_t registerToTfiDetune (uint8_t hw) noexcept
    {
        const uint8_t v = static_cast<uint8_t> (hw & 0x07);
        if (v < 4)  return v;
        if (v == 4) return 0;
        return static_cast<uint8_t> (v - 1);
    }

    // Apply one YM2612 register write to the shadow state. `port` is 0 for
    // 0x52 writes (channels 1-3 + global registers) or 1 for 0x53 writes
    // (channels 4-6). Key-on (0x28) is handled by the caller — this routine
    // never touches the key-on register because the snapshot+dedupe loop owns
    // the cross-channel logic.
    void applyRegisterWrite (std::array<ChannelState, 6>& channels,
                             int port,
                             uint8_t reg,
                             uint8_t data)
    {
        // 0x22 (LFO) is global to the chip; mirror it into every channel's
        // shadow so a snapshot picks up the current chip-wide LFO setting.
        // 0x27 (ch3 mode / timer) and 0x2A/0x2B (DAC) do not affect timbre.
        if (port == 0 && reg == 0x22)
        {
            const uint8_t enable = static_cast<uint8_t> ((data >> 3) & 0x01);
            const uint8_t rate   = static_cast<uint8_t> (data & 0x07);
            for (auto& ch : channels)
            {
                ch.lfo_enable = enable;
                ch.lfo_rate   = rate;
            }
            return;
        }
        if (port == 0 && (reg == 0x27 || reg == 0x2A || reg == 0x2B))
            return;

        // Per-operator registers 0x30..0x9F.
        if (reg >= 0x30 && reg <= 0x9F)
        {
            const int subCh   = reg & 0x03;
            const int slotOff = reg & 0x0C;
            if (subCh == 3) return;
            const int opIndex = slotOffsetToOpIndex (slotOff);
            if (opIndex < 0) return;
            const int channel = port * 3 + subCh;
            ChannelState& s   = channels[(std::size_t) channel];

            switch (reg & 0xF0)
            {
                case 0x30:   // DT (hardware 0-7 in bits 6:4) | MUL (bits 3:0)
                {
                    const uint8_t hwDt = static_cast<uint8_t> ((data >> 4) & 0x07);
                    s.dt[opIndex]  = registerToTfiDetune (hwDt);
                    s.mul[opIndex] = static_cast<uint8_t> (data & 0x0F);
                    break;
                }
                case 0x40:   // TL (bits 6:0)
                    s.tl[opIndex] = static_cast<uint8_t> (data & 0x7F);
                    break;
                case 0x50:   // KS (bits 7:6) | AR (bits 4:0)
                    s.ks[opIndex] = static_cast<uint8_t> ((data >> 6) & 0x03);
                    s.ar[opIndex] = static_cast<uint8_t> (data & 0x1F);
                    break;
                case 0x60:   // AM (bit 7) | DR (bits 4:0)
                    s.amon[opIndex] = static_cast<uint8_t> ((data >> 7) & 0x01);
                    s.dr[opIndex]   = static_cast<uint8_t> (data & 0x1F);
                    break;
                case 0x70:   // D2R / SR (bits 4:0)
                    s.sr[opIndex] = static_cast<uint8_t> (data & 0x1F);
                    break;
                case 0x80:   // SL (bits 7:4) | RR (bits 3:0)
                    s.sl[opIndex] = static_cast<uint8_t> ((data >> 4) & 0x0F);
                    s.rr[opIndex] = static_cast<uint8_t> (data & 0x0F);
                    break;
                case 0x90:   // SSG-EG (bits 3:0)
                    s.ssg[opIndex] = static_cast<uint8_t> (data & 0x0F);
                    break;
                default: break;
            }
            return;
        }

        // Per-channel registers 0xA0..0xB6.
        if (reg >= 0xA0 && reg <= 0xB6)
        {
            const int subCh = reg & 0x03;
            if (subCh > 2) return;
            const int channel = port * 3 + subCh;
            ChannelState& s   = channels[(std::size_t) channel];

            switch (reg & 0xFC)
            {
                case 0xB0:   // FB (bits 5:3) | ALG (bits 2:0)
                    s.fb  = static_cast<uint8_t> ((data >> 3) & 0x07);
                    s.alg = static_cast<uint8_t> (data & 0x07);
                    break;
                case 0xB4:   // L (bit 7) | R (bit 6) | AMS (5:4) | PMS (2:0)
                {
                    const uint8_t left  = static_cast<uint8_t> ((data >> 7) & 0x01);
                    const uint8_t right = static_cast<uint8_t> ((data >> 6) & 0x01);
                    s.lr  = static_cast<uint8_t> ((left << 1) | right);
                    s.ams = static_cast<uint8_t> ((data >> 4) & 0x03);
                    s.pms = static_cast<uint8_t> (data & 0x07);
                    break;
                }
                // 0xA0/0xA4 (frequency low/high) — pitch is not part of the
                // timbre patch, intentionally ignored.
                default: break;
            }
        }
    }

    Patch toPatch (const ChannelState& s)
    {
        Patch p {};
        p.alg = s.alg; p.fb  = s.fb;
        p.lr  = s.lr;  p.ams = s.ams; p.pms = s.pms;
        p.lfo_enable = s.lfo_enable;
        p.lfo_rate   = s.lfo_rate;
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op]  = s.mul[op];  p.dt[op]  = s.dt[op];  p.tl[op]  = s.tl[op];
            p.ks[op]   = s.ks[op];   p.ar[op]  = s.ar[op];  p.dr[op]  = s.dr[op];
            p.sr[op]   = s.sr[op];   p.rr[op]  = s.rr[op];  p.sl[op]  = s.sl[op];
            p.ssg[op]  = s.ssg[op];  p.amon[op] = s.amon[op];
        }
        return p;
    }

    // Pack the patch's timbre fields (everything except `name`) into a flat
    // byte string. Used as the dedupe key via std::unordered_set<std::string>;
    // exact equality, no hash-collision risk, and the unordered_set computes
    // its own hash internally. ADR-0019 calls out the dedupe-by-content-hash
    // contract but leaves the exact key implementation open.
    std::string packedPatchKey (const Patch& p)
    {
        std::string k;
        k.reserve (51);
        const auto put = [&k] (uint8_t b) { k.push_back (static_cast<char> (b)); };
        put (p.alg); put (p.fb); put (p.lr); put (p.ams); put (p.pms);
        put (p.lfo_enable); put (p.lfo_rate);
        for (int op = 0; op < 4; ++op)
        {
            put (p.mul[op]); put (p.dt[op]);  put (p.tl[op]); put (p.ks[op]);
            put (p.ar[op]);  put (p.dr[op]);  put (p.sr[op]); put (p.rr[op]);
            put (p.sl[op]);  put (p.ssg[op]); put (p.amon[op]);
        }
        return k;
    }

    // Per-command operand count (bytes after the command byte). Negative
    // sentinel values flag commands with a variable-length payload — handled
    // inline by the parser (0x67 data block, 0x68 PCM RAM write).
    //
    // VGM 1.60+ widened the 0x40..0x4E reserved range from 1-byte to 2-byte
    // operands; the parser passes its detected `isV160OrLater` so older files
    // still walk forward correctly.
    int operandBytes (uint8_t cmd, bool isV160OrLater) noexcept
    {
        if (cmd <= 0x2F) return 0;                          // reserved single-byte
        if (cmd >= 0x30 && cmd <= 0x3F) return 1;
        if (cmd >= 0x40 && cmd <= 0x4E) return isV160OrLater ? 2 : 1;
        if (cmd == 0x4F || cmd == 0x50) return 1;
        if (cmd >= 0x51 && cmd <= 0x5F) return 2;
        if (cmd == 0x60) return 0;
        if (cmd == 0x61) return 2;
        if (cmd == 0x62 || cmd == 0x63) return 0;
        if (cmd == 0x64) return 3;                          // override wait length
        if (cmd == 0x65) return 0;
        if (cmd == 0x66) return 0;                          // end (caller handles)
        if (cmd == 0x67) return -1;                         // data block — caller handles
        if (cmd == 0x68) return -2;                         // PCM RAM write — caller handles
        if (cmd >= 0x69 && cmd <= 0x6F) return 0;
        if (cmd >= 0x70 && cmd <= 0x8F) return 0;
        if (cmd == 0x90) return 4;
        if (cmd == 0x91) return 4;
        if (cmd == 0x92) return 5;
        if (cmd == 0x93) return 10;
        if (cmd == 0x94) return 1;
        if (cmd == 0x95) return 4;
        if (cmd >= 0x96 && cmd <= 0x9F) return 0;
        if (cmd >= 0xA0 && cmd <= 0xBF) return 2;
        if (cmd >= 0xC0 && cmd <= 0xDF) return 3;
        return 4;                                            // 0xE0..0xFF
    }

    uint32_t readU32LE (const std::vector<uint8_t>& bytes, std::size_t off) noexcept
    {
        return static_cast<uint32_t> (bytes[off])
             | (static_cast<uint32_t> (bytes[off + 1]) <<  8)
             | (static_cast<uint32_t> (bytes[off + 2]) << 16)
             | (static_cast<uint32_t> (bytes[off + 3]) << 24);
    }

    // Read the raw bytes of a .vgm or .vgz file. .vgz is detected by extension
    // and decompressed in memory via juce::GZIPDecompressorInputStream
    // (ADR-0019: no new third-party gzip dependency).
    bool readVgmBytes (const std::filesystem::path& path,
                       std::vector<uint8_t>&        out,
                       std::string&                 error)
    {
        std::ifstream file (path, std::ios::binary);
        if (! file)
        {
            error = "cannot open file: " + path.string();
            return false;
        }
        const std::istreambuf_iterator<char> first (file);
        const std::istreambuf_iterator<char> last;
        std::vector<uint8_t> raw (first, last);

        std::string extLower = path.extension().string();
        std::transform (extLower.begin(), extLower.end(), extLower.begin(),
                        [] (char c)
                        { return static_cast<char> (std::tolower (static_cast<unsigned char> (c))); });

        if (extLower == ".vgz")
        {
            juce::MemoryInputStream src (raw.data(),
                                         static_cast<std::size_t> (raw.size()),
                                         false);
            juce::GZIPDecompressorInputStream gz (
                &src,
                false,
                juce::GZIPDecompressorInputStream::gzipFormat);

            juce::MemoryOutputStream dest;
            if (! dest.writeFromInputStream (gz, -1) || dest.getDataSize() == 0)
            {
                error = "VGZ decompression failed";
                return false;
            }
            const auto* data = static_cast<const uint8_t*> (dest.getData());
            out.assign (data, data + dest.getDataSize());
        }
        else
        {
            out = std::move (raw);
        }
        return true;
    }

    // Walk the command stream, snapshot on every key-on event, dedupe, and
    // name each unique patch in observation order. Returns the patch list and
    // sets `error` on header / parser failure or when no key-ons were found.
    std::vector<Patch> parseVgmStream (const std::vector<uint8_t>& bytes,
                                       const std::string&          fileStem,
                                       std::string&                error)
    {
        constexpr std::size_t kMinHeaderSize = 0x40;
        if (bytes.size() < kMinHeaderSize)
        {
            error = "VGM file too short (need at least a 64-byte header)";
            return {};
        }
        if (! (bytes[0] == 0x56 && bytes[1] == 0x67
               && bytes[2] == 0x6D && bytes[3] == 0x20))
        {
            error = "not a VGM file (missing 'Vgm ' magic)";
            return {};
        }

        const uint32_t version = readU32LE (bytes, 0x08);
        if (version < kVgm150)
        {
            error = "VGM version too old; need 1.50 or newer";
            return {};
        }
        const uint32_t ym2612Clock = readU32LE (bytes, 0x2C);
        if (ym2612Clock == 0)
        {
            error = "VGM file has no YM2612 (no FM patches to extract)";
            return {};
        }

        // VGM 1.50+ stores the data offset at 0x34 as a value relative to the
        // field's own position. A value of 0 falls back to the 0x40 default.
        const uint32_t    dataRel   = readU32LE (bytes, 0x34);
        const std::size_t dataStart = (dataRel == 0)
                                         ? std::size_t { 0x40 }
                                         : static_cast<std::size_t> (0x34u + dataRel);
        if (dataStart > bytes.size())
        {
            error = "VGM data offset points past end of file";
            return {};
        }

        const bool isV160OrLater = (version >= kVgm160);

        std::array<ChannelState, 6>     channels {};
        std::vector<Patch>              result;
        std::unordered_set<std::string> seenKeys;

        std::size_t pos = dataStart;
        while (pos < bytes.size())
        {
            const uint8_t cmd = bytes[pos++];

            if (cmd == 0x66)
                break;                              // end of sound data

            if (cmd == 0x52 || cmd == 0x53)
            {
                if (pos + 2 > bytes.size())
                {
                    error = "truncated YM2612 register write near end of stream";
                    return {};
                }
                const uint8_t reg  = bytes[pos++];
                const uint8_t data = bytes[pos++];
                const int     port = (cmd == 0x52) ? 0 : 1;

                // Key-on/off lives on port 0 (the chip routes 0x53 0x28
                // writes to the same key-on register in practice — we
                // accept both for permissiveness against synthetic files).
                if (reg == 0x28)
                {
                    if ((data & 0xF0) != 0)         // any operator bit set = key-on
                    {
                        const uint8_t lo3 = static_cast<uint8_t> (data & 0x07);
                        if (lo3 != 0x03 && lo3 != 0x07)
                        {
                            // bit 2 = port flag; bits 1:0 = sub-channel.
                            const int channel = (lo3 & 0x04)
                                                    ? 3 + (lo3 & 0x03)
                                                    : (lo3 & 0x03);

                            Patch p = toPatch (channels[(std::size_t) channel]);
                            const auto key = packedPatchKey (p);
                            if (seenKeys.insert (key).second)
                            {
                                p.name = fileStem + " #"
                                        + std::to_string (static_cast<int> (result.size()) + 1);
                                result.push_back (std::move (p));
                            }
                        }
                    }
                    continue;
                }

                applyRegisterWrite (channels, port, reg, data);
                continue;
            }

            if (cmd == 0x61)                        // wait N samples
            {
                if (pos + 2 > bytes.size())
                {
                    error = "truncated wait command near end of stream";
                    return {};
                }
                pos += 2;
                continue;
            }
            if (cmd == 0x62 || cmd == 0x63)         // wait 735 / 882 samples
                continue;
            if (cmd >= 0x70 && cmd <= 0x8F)         // wait n+1 / DAC sample + wait
                continue;

            if (cmd == 0x67)
            {
                // Data block: 0x67 0x66 tt ss ss ss ss <data> — read the
                // 32-bit little-endian size at offset +2 then skip past it.
                if (pos + 6 > bytes.size())
                {
                    error = "truncated data block header";
                    return {};
                }
                const uint32_t blockSize = readU32LE (bytes, pos + 2);
                const std::size_t total  = static_cast<std::size_t> (6) + blockSize;
                if (pos + total > bytes.size())
                {
                    error = "data block size overruns file";
                    return {};
                }
                pos += total;
                continue;
            }
            if (cmd == 0x68)
            {
                // PCM RAM write: 12 bytes total (1 command + 11 operands).
                if (pos + 11 > bytes.size())
                {
                    error = "truncated PCM RAM write";
                    return {};
                }
                pos += 11;
                continue;
            }

            const int operands = operandBytes (cmd, isV160OrLater);
            if (operands < 0)
            {
                // Should not happen — 0x67 and 0x68 are the only sentinel
                // values and both are handled above. Keep the guard for safety.
                error = "internal parser error: unhandled variable-length command";
                return {};
            }
            if (pos + static_cast<std::size_t> (operands) > bytes.size())
            {
                error = "truncated command near end of stream";
                return {};
            }
            pos += static_cast<std::size_t> (operands);
        }

        if (result.empty())
        {
            error = "no FM patches found in VGM (no YM2612 key-on events)";
            return {};
        }

        error.clear();
        return result;
    }
}

std::vector<Patch> extractFmPatches (const std::filesystem::path& vgmPath,
                                     std::string&                 error)
{
    std::vector<uint8_t> bytes;
    if (! readVgmBytes (vgmPath, bytes, error))
        return {};

    const std::string stem = vgmPath.stem().string();
    return parseVgmStream (bytes, stem, error);
}
