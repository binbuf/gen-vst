#include "DACPlayer.h"

#include <algorithm>
#include <cmath>

#include "DACKit.h"

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
                      / static_cast<double> (kDefaultDacRate);
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
    activeCellIdx    = -1;
    pendingCellIdx   = -1;
    pendingCellSet   = false;
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

void DACPlayer::recomputeSamplesPerWrite (int cellRate) noexcept
{
    const int safeRate = DACKit::normaliseDacRate (cellRate);
    samplesPerWrite = static_cast<double> (nativeSampleRate())
                      / static_cast<double> (safeRate);
}

// --- MIDI triggers ---------------------------------------------------------

void DACPlayer::trigger (int midiNote, int velocity)
{
    juce::ignoreUnused (velocity);

    // Audio thread. Look the cell up by note via the kit. Out-of-grid notes
    // and empty cells short-circuit to silence (per task spec: notes outside
    // C-3..G-4 are dropped, empty cells stay silent).
    if (! enabled.load (std::memory_order_acquire) || dacKit == nullptr)
        return;

    const int idx = DACKit::cellIndexForNote (midiNote);
    if (idx < 0) return;

    // Delegate the "is this cell armable" check to DACKit::cellForNote so
    // both sides use the same pendingSwap-aware predicate (a cell whose
    // stagingPcm holds residual bytes from a prior audio-thread swap looks
    // non-empty but is inert until restaged — only pendingSwap=true bytes
    // count as armable).
    if (dacKit->cellForNote (midiNote) == nullptr) return;

    pendingCellIdx = idx;
    pendingCellSet = true;
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

    // Step 1 — pick up trigger arming. The audio thread set
    // pendingCellSet=true in trigger(); switching to a new cell resets the
    // play cursor (hardware-authentic: no crossfade, the bytestream restarts).
    if (pendingCellSet)
    {
        activeCellIdx    = pendingCellIdx;
        playPos          = 0;
        writeAccumulator = 0.0;
        pendingCellSet   = false;
        if (dacKit != nullptr && activeCellIdx >= 0)
        {
            if (auto* c = dacKit->cellPtr (activeCellIdx))
                recomputeSamplesPerWrite (c->rate);
        }
        playing.store (true, std::memory_order_release);
    }

    // Step 2 — drain pendingSwap on every cell. Non-active cells get a swap
    // too so a load-without-trigger doesn't leave pendingSwap stuck (which
    // would cost a 1 s wait on the next message-thread mutation of that
    // cell). Each non-active swap is two vector::swap + a rate copy — cheap
    // enough to do per block. The OLD pcm ends up in stagingPcm and is
    // freed by the next message-thread mutation (allocator activity stays
    // off the audio thread).
    if (dacKit != nullptr)
    {
        for (int i = 0; i < DACKit::kNumCells; ++i)
        {
            auto* c = dacKit->cellPtr (i);
            if (c == nullptr) continue;
            if (! c->pendingSwap.load (std::memory_order_acquire)) continue;

            c->pcm.swap (c->stagingPcm);
            c->rate = c->stagingRate;
            if (i == activeCellIdx)
            {
                // Mid-playback reload of the active cell restarts the
                // bytestream — same "switching samples mid-playback
                // restarts" semantics the spec calls out for cell changes.
                recomputeSamplesPerWrite (c->rate);
                playPos          = 0;
                writeAccumulator = 0.0;
            }
            c->pendingSwap.store (false, std::memory_order_release);
        }
    }

    ymfm::ym2612::output_data sample;
    const bool  isEnabledNow = enabled.load (std::memory_order_acquire);
    const float levelNow     = level.load   (std::memory_order_acquire);

    for (int i = 0; i < numNativeSamples; ++i)
    {
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
    if (! playing.load (std::memory_order_acquire))
        return 0x80;
    if (dacKit == nullptr || activeCellIdx < 0)
        return 0x80;

    auto* c = dacKit->cellPtr (activeCellIdx);
    if (c == nullptr || c->pcm.empty())
        return 0x80;

    const std::uint8_t byte = c->pcm[playPos];

    if (++playPos >= c->pcm.size())
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
    return DACKit::floatTo8BitUnsigned (s);
}

int DACPlayer::normaliseDacRate (int hz) noexcept
{
    return DACKit::normaliseDacRate (hz);
}
