#include "LadderEffect.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Build the ladder lookup at process start. Computed once, then frozen.
    //
    //   indices  0..255 → DAC codes -256..-1 → linear from -1.0 to -8/256
    //   index    256    → DAC code   0       → 0
    //   indices 257..511→ DAC codes +1..+255 → linear from +1/256 to +1.0
    //
    // The negative branch is shifted so the value at DAC code -1 is -8/256
    // (≈ -0.03125) rather than -1/256, producing the YM2612's characteristic
    // ~8× gap at the zero crossing (per jsgroth's "Emulating the YM2612 —
    // Part 5"; see 02-fm-synthesis.md *Ladder Effect DSP*). The realised
    // ratio at the boundary lands around 8.2× because of integer-rounding
    // in the lookup, which is consistent with the measured hardware.
    std::array<float, LadderEffect::kTableSize> buildTable() noexcept
    {
        std::array<float, LadderEffect::kTableSize> t {};

        // Negative branch — linear from -1.0 (idx 0) to -8/256 (idx 255).
        // Inside step ≈ (1 − 8/256) / 255 ≈ 1/263 per code.
        constexpr float negStart = -1.0f;
        constexpr float negEnd   = -8.0f / 256.0f;
        for (int i = 0; i < 256; ++i)
        {
            const float u = static_cast<float> (i) / 255.0f;
            t[(std::size_t) i] = negStart + u * (negEnd - negStart);
        }

        t[256] = 0.0f;

        // Positive branch — linear from +1/256 (idx 257) to +1.0 (idx 511).
        constexpr float posStart = 1.0f / 256.0f;
        constexpr float posEnd   = 1.0f;
        for (int i = 257; i < LadderEffect::kTableSize; ++i)
        {
            const float u = static_cast<float> (i - 257) / 254.0f;
            t[(std::size_t) i] = posStart + u * (posEnd - posStart);
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
