#include "PatchSystem.h"

#include <algorithm>
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
