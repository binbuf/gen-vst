#include "DspDecimator.h"

#include <algorithm>
#include <cmath>

void DspDecimator::prepare (double /*sampleRate*/, int /*maxBlockSize*/) noexcept
{
    reset();
}

int DspDecimator::holdSamples (float prescaler01) noexcept
{
    const float p = std::clamp (prescaler01, 0.0f, 1.0f);
    return std::max (1, static_cast<int> (std::round (1.0f + 15.0f * p)));
}

void DspDecimator::process (juce::AudioBuffer<float>& buffer, float prescaler01) noexcept
{
    const int numChannels = std::min (buffer.getNumChannels(), kMaxChannels);
    const int numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0) return;

    const int hold = holdSamples (prescaler01);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data    = buffer.getWritePointer (ch);
        float  held    = heldSample[(std::size_t) ch];
        int    counter = samplesUntilNext[(std::size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            if (counter <= 0)
            {
                const float raw    = data[i];
                const float scaled = std::clamp (std::round (raw * 128.0f), -128.0f, 127.0f);
                held    = scaled / 128.0f;
                counter = hold;
            }
            data[i] = held;
            --counter;
        }

        heldSample[(std::size_t) ch]       = held;
        samplesUntilNext[(std::size_t) ch] = counter;
    }
}

void DspDecimator::reset() noexcept
{
    heldSample.fill (0.0f);
    samplesUntilNext.fill (0);
}
