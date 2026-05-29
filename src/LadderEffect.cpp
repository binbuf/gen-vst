#include "LadderEffect.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Build the ladder lookup at process start. Computed once, then frozen.
    //
    // Mirrors ymfm's `dac_discontinuity` (third_party/ymfm/src/ymfm_opn.h:766)
    // in normalized -1..+1 space:
    //
    //   code  c < 0  : out = (c - 3) / 256
    //   code  c ≥ 0  : out = (c + 4) / 256
    //
    // Table layout:
    //   indices  0..255  → DAC codes -256..-1 → (code - 3) / 256
    //   indices  256..511→ DAC codes   0..255 → (code + 4) / 256
    //
    // The gap between code -1's output (-4/256) and code 0's output (+4/256)
    // is 8/256 ≈ 0.0313, vs the normal linear step of 1/256 — the YM2612's
    // documented 8× zero-crossing gap. Peaks are ±259/256 ≈ ±1.012 (the
    // downstream master-volume softClip handles the small overshoot).
    std::array<float, LadderEffect::kTableSize> buildTable() noexcept
    {
        std::array<float, LadderEffect::kTableSize> t {};

        constexpr float kInv256 = 1.0f / 256.0f;

        // Negative codes [-256, -1] — shifted down by 3 in 9-bit space.
        for (int i = 0; i < 256; ++i)
        {
            const int code = i - 256;             // -256..-1
            t[(std::size_t) i] = static_cast<float> (code - 3) * kInv256;
        }

        // Non-negative codes [0, +255] — shifted up by 4 in 9-bit space.
        for (int i = 256; i < LadderEffect::kTableSize; ++i)
        {
            const int code = i - 256;             // 0..+255
            t[(std::size_t) i] = static_cast<float> (code + 4) * kInv256;
        }

        return t;
    }

    const std::array<float, LadderEffect::kTableSize>& ladderTable() noexcept
    {
        static const auto t = buildTable();
        return t;
    }
}

void LadderEffect::prepare (double /*sampleRate*/) noexcept
{
    // Force-init the lookup so it's ready before the first audio block.
    (void) ladderTable();
}

const std::array<float, LadderEffect::kTableSize>& LadderEffect::table() noexcept
{
    return ladderTable();
}

float LadderEffect::lookup (float sample) noexcept
{
    const int code = std::clamp (static_cast<int> (std::round (sample * 256.0f)), -256, 255);
    return ladderTable()[(std::size_t) (code + 256)];
}

void LadderEffect::process (juce::AudioBuffer<float>& buffer, bool enabled) noexcept
{
    if (! enabled) return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    const auto& t = ladderTable();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const int code = std::clamp (
                static_cast<int> (std::round (data[i] * 256.0f)), -256, 255);
            data[i] = t[(std::size_t) (code + 256)];
        }
    }
}
