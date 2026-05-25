#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

// Sample-and-hold + 8-bit quantiser driven by a single 0..1 prescaler control.
// Used by D mode (input bitcrush) and — wired in Task 05 — by FM mode's
// `fm_dac_prescaler` knob (02-fm-synthesis.md *DAC Prescaler (FM mode)*).
//
// `prescaler01 = 0.0` keeps every sample (identity, host rate). `prescaler01 =
// 1.0` holds each sampled value for 16 host-rate samples and 8-bit quantises
// it. Mapping: `holdSamples = max(1, round(1 + 15 * prescaler01))` — monotonic
// over 1..16.
class DspDecimator
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, int maxBlockSize) noexcept;

    // Per-channel sample-and-hold + 8-bit quantise, in place. `prescaler01` is
    // clamped to [0, 1]; out-of-range values are silently corrected.
    void process (juce::AudioBuffer<float>& buffer, float prescaler01) noexcept;

    void reset() noexcept;

    static int holdSamples (float prescaler01) noexcept;

private:
    std::array<float, kMaxChannels> heldSample        { 0.0f, 0.0f };
    std::array<int,   kMaxChannels> samplesUntilNext  { 0,    0    };
};
