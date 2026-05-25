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

void Telemetry::pushSamples (const float* L, const float* R, int numSamples) noexcept
{
    if (numSamples <= 0 || L == nullptr) return;

    if (R != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::fabs (L[i]);
            const float absR = std::fabs (R[i]);
            if (absL > blockPeakL) blockPeakL = absL;
            if (absR > blockPeakR) blockPeakR = absR;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::fabs (L[i]);
            if (absL > blockPeakL) blockPeakL = absL;
            if (absL > blockPeakR) blockPeakR = absL;
        }
    }
}

void Telemetry::finishBlock() noexcept
{
    // VU envelope: instant attack to the block peak, exponential release per
    // block.
    if (blockPeakL > vuEnvL) vuEnvL = blockPeakL;
    else                     vuEnvL *= releaseCoef;
    if (blockPeakR > vuEnvR) vuEnvR = blockPeakR;
    else                     vuEnvR *= releaseCoef;

    publishedVuL.store (std::min (1.0f, vuEnvL), std::memory_order_relaxed);
    publishedVuR.store (std::min (1.0f, vuEnvR), std::memory_order_relaxed);

    blockPeakL = 0.0f;
    blockPeakR = 0.0f;
}
