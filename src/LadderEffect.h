#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

// YM2612 "ladder DAC" stepwise nonlinearity per ADR-0024 and
// 02-fm-synthesis.md *Ladder Effect DSP*. Applied in FM mode (per-voice sum)
// and D mode (after the DspDecimator 8-bit quantiser). Greyed out in SQ mode
// (the SN76489 has its own output pin and doesn't pass through the YM2612
// ladder DAC on real hardware).
//
// Implemented as a 512-entry lookup over the 9-bit DAC code space:
// `idx = clamp(round(s * 256), -256, 255) + 256` then `out = table[idx]`.
//
// The curve is piecewise linear over both halves, with the negative branch
// shifted so DAC code -1 sits at -8/256 instead of -1/256. This realises the
// YM2612's ~8× gap at the zero crossing per jsgroth's measurements.
class LadderEffect
{
public:
    static constexpr int kTableSize = 512;

    void prepare (double sampleRate) noexcept;

    // In-place per-channel lookup. `enabled = false` early-returns (no DSP).
    void process (juce::AudioBuffer<float>& buffer, bool enabled) noexcept;

    void reset() noexcept {}

    // Public so unit tests can sample the curve at specific pinch points.
    static float lookup (float sample) noexcept;

    static const std::array<float, kTableSize>& table() noexcept;
};
