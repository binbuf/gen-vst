#include "OutputFilter.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Hand-tuned approximation of the Sega Model-1 audio stage character
    // (02-fm-synthesis.md *Output Filtering DSP*): -3 dB LPF knee at ~3.4 kHz
    // plus a light +0.5 dB high-shelf at ~5 kHz, Q ~0.6. Fixed for v2 — these
    // are the intentional v2 coefficients, not a placeholder, and the spectrum
    // test in OutputFilterTests.cpp pins them.
    constexpr float kLpfHz       = 3400.0f;
    constexpr float kShelfHz     = 5000.0f;
    constexpr float kShelfGainDb = 0.5f;
    constexpr float kShelfQ      = 0.6f;
}

void OutputFilter::prepare (double sampleRate) noexcept
{
    const double fs = std::max (1.0, sampleRate);

    // One-pole RC LPF — y[n] = (1-a) x[n] + a y[n-1]; a = exp(-2π fc / fs).
    lpfA     = static_cast<float> (std::exp (-2.0 * juce::MathConstants<double>::pi * kLpfHz / fs));
    lpfOneMa = 1.0f - lpfA;

    // High-shelf biquad — RBJ cookbook coefficients.
    const double A     = std::pow (10.0, kShelfGainDb / 40.0);
    const double w0    = 2.0 * juce::MathConstants<double>::pi * kShelfHz / fs;
    const double cosW0 = std::cos (w0);
    const double sinW0 = std::sin (w0);
    const double alpha = sinW0 / (2.0 * kShelfQ);
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

    const double b0 =      A * ((A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAalpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
    const double b2 =      A * ((A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAalpha);
    const double a0 =           (A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAalpha;
    const double a1 =  2.0     * ((A - 1.0) - (A + 1.0) * cosW0);
    const double a2 =           (A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAalpha;

    shelfB0 = static_cast<float> (b0 / a0);
    shelfB1 = static_cast<float> (b1 / a0);
    shelfB2 = static_cast<float> (b2 / a0);
    shelfA1 = static_cast<float> (a1 / a0);
    shelfA2 = static_cast<float> (a2 / a0);

    reset();
}

void OutputFilter::process (juce::AudioBuffer<float>& buffer, bool enabled) noexcept
{
    if (! enabled) return;

    const int numChannels = std::min (buffer.getNumChannels(), kMaxChannels);
    const int numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        float lpf  = lpfState[(std::size_t) ch];
        auto& bq   = shelfState[(std::size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];

            // One-pole LPF
            lpf = lpfOneMa * x + lpfA * lpf;

            // High-shelf biquad (direct-form-I)
            const float y = shelfB0 * lpf
                          + shelfB1 * bq.x1
                          + shelfB2 * bq.x2
                          - shelfA1 * bq.y1
                          - shelfA2 * bq.y2;

            bq.x2 = bq.x1; bq.x1 = lpf;
            bq.y2 = bq.y1; bq.y1 = y;

            data[i] = y;
        }

        lpfState[(std::size_t) ch] = lpf;
    }
}

void OutputFilter::reset() noexcept
{
    lpfState.fill (0.0f);
    for (auto& s : shelfState)
        s = {};
}
