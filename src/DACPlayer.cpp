#include "DACPlayer.h"

#include <algorithm>
#include <cmath>

namespace
{
    // YM2612 NTSC clock — matches Voice.cpp so the DAC sums into the FM mix
    // bus at exactly the same native rate.
    constexpr std::uint32_t kNtscClock = 7670454;

    constexpr int kDefaultDacRate = 22050;
}

DACPlayer::DACPlayer()
{
    samplesPerWrite = static_cast<double> (nativeSampleRate())
                      / static_cast<double> (dacRate);
}

void DACPlayer::prepare (double hostSampleRate, int maxBlockSize)
{
    juce::ignoreUnused (hostSampleRate, maxBlockSize);

    reset();

    // One-time chip setup: enable DACEN on channel 6 so writes to 0x2A drive
    // the chip's PCM output path (07-feature-spec.md "DAC Mode Specification").
    writeReg (0x2B, 0x80);
}

void DACPlayer::reset()
{
    chip.reset();
    playing          = false;
    playPos          = 0;
    writeAccumulator = 0.0;

    // Re-enable DACEN after the hard reset (it clears the chip's regs).
    writeReg (0x2B, 0x80);

    // Latch silence (0x80 = midpoint) so a held DAC output value doesn't
    // ring through the next playback's first sample.
    writeReg (0x2A, 0x80);
}

void DACPlayer::writeReg (std::uint8_t addr, std::uint8_t value)
{
    chip.write (0, addr);
    chip.write (1, value);
}

// --- WAV loading -----------------------------------------------------------

bool DACPlayer::loadWav (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    const auto numChans   = static_cast<int> (reader->numChannels);
    if (numSamples <= 0 || numChans <= 0)
        return false;

    // Read the WAV into a stereo (or mono) float buffer, then mix to mono by
    // averaging channels.
    juce::AudioBuffer<float> raw (numChans, numSamples);
    if (! reader->read (&raw, 0, numSamples, 0, true, numChans > 1))
        return false;

    srcRate = reader->sampleRate;
    srcFloat.assign (static_cast<std::size_t> (numSamples), 0.0f);

    const float invChans = 1.0f / static_cast<float> (numChans);
    for (int c = 0; c < numChans; ++c)
    {
        const float* src = raw.getReadPointer (c);
        for (int i = 0; i < numSamples; ++i)
            srcFloat[static_cast<std::size_t> (i)] += src[i] * invChans;
    }

    regeneratePcmFromSource();
    return ! pcm.empty();
}

void DACPlayer::clearPcm()
{
    pcm.clear();
    srcFloat.clear();
    srcRate = 0.0;
    playing = false;
    playPos = 0;
}

bool DACPlayer::hasPcm() const noexcept
{
    return ! pcm.empty();
}

void DACPlayer::setDacRate (int hz)
{
    const int newRate = normaliseDacRate (hz);
    if (newRate == dacRate)
        return;

    dacRate = newRate;
    samplesPerWrite = static_cast<double> (nativeSampleRate())
                      / static_cast<double> (dacRate);
    regeneratePcmFromSource();
}

void DACPlayer::regeneratePcmFromSource()
{
    if (srcFloat.empty() || srcRate <= 0.0)
    {
        pcm.clear();
        return;
    }

    const int srcCount = static_cast<int> (srcFloat.size());
    const double ratio = srcRate / static_cast<double> (dacRate);
    const int dstCount = std::max (1, static_cast<int> (std::ceil (srcCount / ratio)));

    pcm.assign (static_cast<std::size_t> (dstCount), 0x80);

    // Nearest-sample resampling — adequate for 8-bit DAC playback (the
    // chip already imposes severe quantisation and bandwidth limits). A
    // higher-quality resampler would be wasted on this output path.
    for (int i = 0; i < dstCount; ++i)
    {
        const double srcPos = i * ratio;
        const int    idx    = std::clamp (static_cast<int> (std::round (srcPos)),
                                          0, srcCount - 1);
        pcm[static_cast<std::size_t> (i)] =
            floatTo8BitUnsigned (srcFloat[static_cast<std::size_t> (idx)]);
    }
}

// --- MIDI triggers ---------------------------------------------------------

void DACPlayer::trigger (int midiNote, int velocity)
{
    juce::ignoreUnused (midiNote, velocity);

    if (! enabled || pcm.empty())
        return;

    playing          = true;
    playPos          = 0;
    writeAccumulator = 0.0;
}

void DACPlayer::release()
{
    playing = false;
    writeReg (0x2A, 0x80);   // park the latched output at silence
}

// --- Render ----------------------------------------------------------------

void DACPlayer::renderAdd (float* nativeL, float* nativeR, int numNativeSamples)
{
    if (numNativeSamples <= 0)
        return;

    ymfm::ym2612::output_data sample;

    for (int i = 0; i < numNativeSamples; ++i)
    {
        // Phase-accurate write timing (07-feature-spec.md): write a fresh PCM
        // byte every `samplesPerWrite` native samples. The accumulator can
        // exceed `samplesPerWrite` by more than one when very low DAC rates
        // are selected, but the inner while-loop catches catch-up writes.
        if (playing)
        {
            writeAccumulator += 1.0;
            while (writeAccumulator >= samplesPerWrite)
            {
                writeAccumulator -= samplesPerWrite;
                const std::uint8_t byte = fetchNextSampleByte();
                writeReg (0x2A, byte);
            }
        }

        chip.generate (&sample, 1);

        if (enabled)
        {
            const float l = static_cast<float> (sample.data[0]) * kSampleScale * level;
            const float r = static_cast<float> (sample.data[1]) * kSampleScale * level;
            nativeL[i] += l;
            nativeR[i] += r;
        }
    }
}

std::uint8_t DACPlayer::fetchNextSampleByte()
{
    if (pcm.empty())
        return 0x80;

    const std::uint8_t byte = pcm[playPos];

    if (++playPos >= pcm.size())
    {
        if (mode == Mode::Loop)
            playPos = 0;
        else
            playing = false;
    }
    return byte;
}

std::uint32_t DACPlayer::nativeSampleRate()
{
    return chip.sample_rate (kNtscClock);
}

// --- Static helpers --------------------------------------------------------

std::uint8_t DACPlayer::floatTo8BitUnsigned (float s) noexcept
{
    const float clamped = juce::jlimit (-1.0f, 1.0f, s);
    const int   signed8 = static_cast<int> (std::lround (clamped * 127.0f));
    return static_cast<std::uint8_t> (std::clamp (signed8 + 128, 0, 255));
}

int DACPlayer::normaliseDacRate (int hz) noexcept
{
    if (hz == 8000 || hz == 11025 || hz == 22050)
        return hz;
    return kDefaultDacRate;
}
