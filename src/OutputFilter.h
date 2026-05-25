#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

// Model-1 console output stage emulation per ADR-0024 and 02-fm-synthesis.md
// *Output Filtering DSP* — a one-pole RC low-pass at ~3.4 kHz followed by a
// gentle high-shelf taming the upper midrange. Fixed coefficients; the user
// only sees an on/off toggle.
//
// Applied to the mix bus in all three modes (FM, SQ, D) when `output_filter`
// is true. Early-returns when bypassed — no DSP runs.
class OutputFilter
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate) noexcept;

    // In-place per-channel filter. `enabled = false` early-returns (no DSP).
    void process (juce::AudioBuffer<float>& buffer, bool enabled) noexcept;

    void reset() noexcept;

private:
    // One-pole RC low-pass: y[n] = (1-a) * x[n] + a * y[n-1], where
    // a = exp(-2π * fc / fs).
    float lpfA      = 0.0f;
    float lpfOneMa  = 1.0f;
    std::array<float, kMaxChannels> lpfState { 0.0f, 0.0f };

    // High-shelf biquad — direct-form-I state per channel.
    // Coefficients computed in prepare(); not user-tunable.
    float shelfB0 = 1.0f, shelfB1 = 0.0f, shelfB2 = 0.0f;
    float shelfA1 = 0.0f, shelfA2 = 0.0f;
    struct BiquadState { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };
    std::array<BiquadState, kMaxChannels> shelfState {};
};
