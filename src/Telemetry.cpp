#include "Telemetry.h"

#include <algorithm>
#include <cmath>

void Telemetry::prepare (double hostSampleRate) noexcept
{
    // Release time ~300 ms: a per-sample release coefficient r such that
    // r^(0.3 * hostSampleRate) ~= 0.01 (decay to 1% over 300 ms). That gives
    // r = exp(ln(0.01) / (0.3 * sr)).
    const double samples = std::max (1.0, 0.3 * hostSampleRate);
    releaseCoef = static_cast<float> (std::exp (std::log (0.01) / samples));
}

void Telemetry::pushSamples (const float* L, const float* R, int numSamples,
                             bool clipDetectedThisRange) noexcept
{
    if (numSamples <= 0 || L == nullptr) return;

    const std::uint64_t start = writeIdx.load (std::memory_order_relaxed);
    constexpr std::uint64_t mask = static_cast<std::uint64_t> (kScopeBufferSize - 1);

    if (R != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = L[i];
            const float r = R[i];
            scope[(start + static_cast<std::uint64_t> (i)) & mask] = 0.5f * (l + r);

            const float absL = std::fabs (l);
            const float absR = std::fabs (r);
            if (absL > blockPeakL) blockPeakL = absL;
            if (absR > blockPeakR) blockPeakR = absR;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = L[i];
            scope[(start + static_cast<std::uint64_t> (i)) & mask] = l;
            const float absL = std::fabs (l);
            if (absL > blockPeakL) blockPeakL = absL;
            if (absL > blockPeakR) blockPeakR = absL;
        }
    }

    // Release: writers in pushSamples are paired with the reader's acquire
    // load of writeIdx, so the reader sees the matching scope writes.
    writeIdx.store (start + static_cast<std::uint64_t> (numSamples),
                    std::memory_order_release);

    if (clipDetectedThisRange)
        blockClipped = true;
}

void Telemetry::finishBlock (std::uint32_t voiceMask) noexcept
{
    // VU envelope: instant attack to the block peak, exponential release per
    // block. The block-rate release is coarse but matches the editor's 30 Hz
    // read cadence — a finer per-sample envelope would be invisible at the UI.
    if (blockPeakL > vuEnvL) vuEnvL = blockPeakL;
    else                     vuEnvL *= releaseCoef;
    if (blockPeakR > vuEnvR) vuEnvR = blockPeakR;
    else                     vuEnvR *= releaseCoef;

    publishedVuL.store (std::min (1.0f, vuEnvL), std::memory_order_relaxed);
    publishedVuR.store (std::min (1.0f, vuEnvR), std::memory_order_relaxed);

    if (blockClipped)
        clipFlag.store (true, std::memory_order_relaxed);

    mask.store (voiceMask, std::memory_order_relaxed);

    blockPeakL   = 0.0f;
    blockPeakR   = 0.0f;
    blockClipped = false;
}

int Telemetry::readScope (float* dest, int destSize) const noexcept
{
    if (dest == nullptr || destSize <= 0) return 0;

    const std::uint64_t w = writeIdx.load (std::memory_order_acquire);
    const int available   = static_cast<int> (std::min<std::uint64_t> (w, kScopeBufferSize));
    const int wantLen     = std::min (destSize, available);
    if (wantLen <= 0) return 0;

    constexpr std::uint64_t ring = static_cast<std::uint64_t> (kScopeBufferSize - 1);
    const std::uint64_t startIdx = w - static_cast<std::uint64_t> (wantLen);
    for (int i = 0; i < wantLen; ++i)
        dest[i] = scope[(startIdx + static_cast<std::uint64_t> (i)) & ring];

    return wantLen;
}
