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

    // VU envelope follower: instant attack to the input magnitude, exponential
    // release at `releaseCoef` *per sample*. `releaseCoef` is calibrated for
    // per-sample application in prepare(); applying it once per block (the
    // previous behaviour) inflated the effective release time by the block
    // size, leaving the meter sitting near its peak for tens of seconds —
    // "remains static" was the user-visible symptom.
    if (R != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::fabs (L[i]);
            const float absR = std::fabs (R[i]);
            vuEnvL = absL > vuEnvL ? absL : vuEnvL * releaseCoef;
            vuEnvR = absR > vuEnvR ? absR : vuEnvR * releaseCoef;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::fabs (L[i]);
            vuEnvL = absL > vuEnvL ? absL : vuEnvL * releaseCoef;
            vuEnvR = vuEnvL;       // mono path mirrors L into R for the UI.
        }
    }
}

void Telemetry::finishBlock() noexcept
{
    // Publish the per-sample envelope state; no per-block release here — that
    // double-counted against the per-sample release above.
    publishedVuL.store (std::min (1.0f, vuEnvL), std::memory_order_relaxed);
    publishedVuR.store (std::min (1.0f, vuEnvR), std::memory_order_relaxed);
}
