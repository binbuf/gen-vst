#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

// YM2612 "ladder DAC" stepwise nonlinearity per ADR-0024 and
// 02-fm-synthesis.md *Ladder Effect DSP*. Used in D mode only — applied to
// the wet path after the DspDecimator 8-bit quantiser. FM mode handles the
// ladder per-voice via ymfm chip-variant dispatch (ym2612 vs ym3438 — see
// Voice::renderAdd), since ymfm already implements the same +4/-3
// discontinuity per-channel before summing. SQ mode is unaffected (the
// SN76489 has its own output pin and doesn't pass through the YM2612 DAC).
//
// Implemented as a 512-entry lookup over the 9-bit DAC code space:
// `idx = clamp(round(s * 256), -256, 255) + 256` then `out = table[idx]`.
//
// Curve matches ymfm's `dac_discontinuity` (see third_party/ymfm/src/
// ymfm_opn.h:766): non-negative codes get +4, negative codes get -3, then
// normalise by 256. This produces the YM2612's ~8× gap at the zero crossing
// (gap = 8 codes between -1's output of -4/256 and 0's output of +4/256, vs
// a normal linear step of 1/256).
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
