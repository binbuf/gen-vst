#include "DACPlayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

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
    stagingDacRate         = dacRate;
    stagingSamplesPerWrite = samplesPerWrite;
    mtDacRate              = dacRate;
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
    playing.store (false, std::memory_order_release);
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

    sampleName = file.getFileName();

    // Resample to the current dacRate, store the resulting bytes both as the
    // message-thread mirror (for state save / UI peaks) and as the staged
    // buffer the audio thread will swap in at the next renderAdd.
    auto newPcm = regenerateBytes (srcFloat, srcRate, mtDacRate);
    mtPcm = newPcm;
    stageSwap (std::move (newPcm), mtDacRate);
    return ! mtPcm.empty();
}

void DACPlayer::loadRawPcm (const std::uint8_t* bytes, std::size_t numBytes,
                            int dacRateHz, const juce::String& name)
{
    if (bytes == nullptr || numBytes == 0)
    {
        clearPcm();
        return;
    }

    mtDacRate = normaliseDacRate (dacRateHz);

    // Reconstruct a mono float source from the 8-bit PCM so the D-view's
    // waveform peaks render and a later DAC-rate change can resample without
    // needing the original WAV. The reconstruction is exact-ish to the 8-bit
    // granularity the chip actually plays — good enough for both purposes.
    srcRate = static_cast<double> (mtDacRate);
    srcFloat.assign (numBytes, 0.0f);
    for (std::size_t i = 0; i < numBytes; ++i)
        srcFloat[i] = (static_cast<int> (bytes[i]) - 128) / 127.0f;

    sampleName = name;

    // Mirror + stage. The audio thread will pick up the new buffer + reset
    // playPos/writeAccumulator on the next renderAdd.
    mtPcm.assign (bytes, bytes + numBytes);
    stageSwap (mtPcm, mtDacRate);

    writeReg (0x2A, 0x80);   // park the chip at silence
}

void DACPlayer::clearPcm()
{
    // 1) Park the player so any concurrent audio-thread fetchNextSampleByte
    //    returns silence instead of indexing the (about-to-be-swapped) buffer.
    playing.store (false, std::memory_order_release);

    // 2) Clear the message-thread mirror + source data.
    mtPcm.clear();
    srcFloat.clear();
    srcRate = 0.0;
    sampleName = juce::String();

    // 3) Stage an empty buffer at the current dacRate so the audio thread
    //    swaps in an empty pcm on the next renderAdd, allocator activity
    //    stays on the message thread.
    stageSwap ({}, mtDacRate);
}

double DACPlayer::getSampleLengthSeconds() const noexcept
{
    if (mtPcm.empty() || mtDacRate <= 0) return 0.0;
    return static_cast<double> (mtPcm.size()) / static_cast<double> (mtDacRate);
}

std::vector<float> DACPlayer::computePeaks (int numBuckets) const
{
    std::vector<float> out;
    if (srcFloat.empty() || numBuckets <= 0) return out;

    out.assign (static_cast<std::size_t> (numBuckets), 0.0f);

    const std::size_t total = srcFloat.size();
    // Bucket the source samples into `numBuckets` columns and record the
    // absolute peak in each. Using the original float source (not the 8-bit
    // PCM) gives a sharper visualisation that isn't affected by dacRate
    // changes; the strip is informational only, not what the chip plays.
    for (int b = 0; b < numBuckets; ++b)
    {
        const std::size_t lo = (static_cast<std::size_t> (b)     * total) /
                                static_cast<std::size_t> (numBuckets);
        const std::size_t hi = (static_cast<std::size_t> (b + 1) * total) /
                                static_cast<std::size_t> (numBuckets);
        float peak = 0.0f;
        for (std::size_t i = lo; i < hi; ++i)
            peak = std::max (peak, std::fabs (srcFloat[i]));
        out[static_cast<std::size_t> (b)] = std::min (1.0f, peak);
    }
    return out;
}

bool DACPlayer::hasPcm() const noexcept
{
    return ! mtPcm.empty();
}

void DACPlayer::setDacRate (int hz)
{
    const int newRate = normaliseDacRate (hz);
    if (newRate == mtDacRate)
        return;

    mtDacRate = newRate;
    auto newPcm = regenerateBytes (srcFloat, srcRate, mtDacRate);
    mtPcm = newPcm;
    stageSwap (std::move (newPcm), mtDacRate);
}

std::vector<std::uint8_t>
DACPlayer::regenerateBytes (const std::vector<float>& src,
                            double sourceRate, int destDacRate) const
{
    std::vector<std::uint8_t> out;
    if (src.empty() || sourceRate <= 0.0 || destDacRate <= 0)
        return out;

    const int srcCount = static_cast<int> (src.size());
    const double ratio = sourceRate / static_cast<double> (destDacRate);
    const int dstCount = std::max (1, static_cast<int> (std::ceil (srcCount / ratio)));

    out.assign (static_cast<std::size_t> (dstCount), 0x80);

    // Nearest-sample resampling — adequate for 8-bit DAC playback (the
    // chip already imposes severe quantisation and bandwidth limits). A
    // higher-quality resampler would be wasted on this output path.
    for (int i = 0; i < dstCount; ++i)
    {
        const double srcPos = i * ratio;
        const int    idx    = std::clamp (static_cast<int> (std::round (srcPos)),
                                          0, srcCount - 1);
        out[static_cast<std::size_t> (i)] =
            floatTo8BitUnsigned (src[static_cast<std::size_t> (idx)]);
    }
    return out;
}

void DACPlayer::stageSwap (std::vector<std::uint8_t> newBytes, int newDacRate)
{
    // Wait for the audio thread to consume any previous pending swap before
    // we overwrite the staging area. In normal playback this returns within
    // a single audio block (~10 ms). Cap the wait at 1 second so a host that
    // has stopped calling processBlock (or pluginval's threaded state-restore
    // stress test) cannot deadlock the message thread; after the timeout we
    // proceed and write staging directly. The race window is narrow (the
    // audio thread's swap is three pointer exchanges) and bounded to one
    // block, after which subsequent swaps stabilise.
    const auto t0 = std::chrono::steady_clock::now();
    while (pendingSwap.load (std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() - t0
              > std::chrono::milliseconds (1000))
            break;
        std::this_thread::yield();
    }

    stagingPcm = std::move (newBytes);
    stagingDacRate = normaliseDacRate (newDacRate);
    stagingSamplesPerWrite = static_cast<double> (nativeSampleRate())
                              / static_cast<double> (stagingDacRate);
    pendingSwap.store (true, std::memory_order_release);
}

// --- MIDI triggers ---------------------------------------------------------

void DACPlayer::trigger (int midiNote, int velocity)
{
    juce::ignoreUnused (midiNote, velocity);

    // trigger() runs on the message thread (or wherever MIDI events are
    // delivered to the editor's note path). Reading mtPcm here, not the
    // audio-thread live pcm, is the safe choice — the two only differ for
    // one block at most, and arming a playback on a buffer that's about to
    // become non-empty is harmless (the audio thread will see it next block).
    if (! enabled.load (std::memory_order_acquire) || mtPcm.empty())
        return;

    // Setting playing AFTER playPos/writeAccumulator ensures the audio thread
    // never observes "playing == true" with a stale cursor. The audio thread
    // owns playPos/writeAccumulator and resets them on swap, so the message
    // thread shouldn't write them at all in normal operation — but trigger
    // wants playback to start at 0. We rely on the pendingSwap path having
    // run already (or the buffer being empty) so playPos == 0 is current.
    playPos          = 0;
    writeAccumulator = 0.0;
    playing.store (true, std::memory_order_release);
}

void DACPlayer::release()
{
    playing.store (false, std::memory_order_release);
    writeReg (0x2A, 0x80);   // park the latched output at silence
}

// --- Render ----------------------------------------------------------------

void DACPlayer::renderAdd (float* nativeL, float* nativeR, int numNativeSamples)
{
    if (numNativeSamples <= 0)
        return;

    // Pick up any pending buffer swap published by the message thread before
    // we read pcm in this block. The swap exchanges the staging vector with
    // the live pcm vector — allocator activity stays on the message thread
    // (the OLD pcm ends up in stagingPcm, where the next message-thread
    // stageSwap call deallocates it).
    if (pendingSwap.load (std::memory_order_acquire))
    {
        pcm.swap (stagingPcm);
        dacRate         = stagingDacRate;
        samplesPerWrite = stagingSamplesPerWrite;
        playPos          = 0;
        writeAccumulator = 0.0;
        pendingSwap.store (false, std::memory_order_release);
    }

    ymfm::ym2612::output_data sample;
    const bool  isEnabledNow = enabled.load (std::memory_order_acquire);
    const float levelNow     = level.load   (std::memory_order_acquire);

    for (int i = 0; i < numNativeSamples; ++i)
    {
        // Phase-accurate write timing (07-feature-spec.md): write a fresh PCM
        // byte every `samplesPerWrite` native samples. The accumulator can
        // exceed `samplesPerWrite` by more than one when very low DAC rates
        // are selected, but the inner while-loop catches catch-up writes.
        if (playing.load (std::memory_order_acquire))
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

        if (isEnabledNow)
        {
            const float l = static_cast<float> (sample.data[0]) * kSampleScale * levelNow;
            const float r = static_cast<float> (sample.data[1]) * kSampleScale * levelNow;
            nativeL[i] += l;
            nativeR[i] += r;
        }
    }
}

std::uint8_t DACPlayer::fetchNextSampleByte()
{
    // Belt-and-braces: if playing was cleared between the renderAdd top-level
    // check and this call, return silence. Also covers the (rare) case of
    // entering this function with playing=true but pcm just having been
    // swapped to empty by clearPcm — pendingSwap is consumed at top of
    // renderAdd, so by the time we get here pcm is the post-swap content.
    if (! playing.load (std::memory_order_acquire))
        return 0x80;
    if (pcm.empty())
        return 0x80;

    const std::uint8_t byte = pcm[playPos];

    if (++playPos >= pcm.size())
    {
        if ((Mode) modeInt.load (std::memory_order_acquire) == Mode::Loop)
            playPos = 0;
        else
            playing.store (false, std::memory_order_release);
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
