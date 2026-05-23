#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Lock-free audio-thread → message-thread telemetry for the header meter bay
// (05-ui-ux.md "C++ → JS telemetry push"). The audio thread writes the
// post-master-gain post-soft-clip output into a circular scope buffer and
// updates VU/clip/voice-mask scalars; the editor's ~30 Hz juce::Timer reads
// snapshots on the message thread and emits a combined "meterData" event.
//
// No allocation, no locks on the audio thread. The reader sees a lossy
// "most-recent-N-samples" view of the scope (one writer maintains the index;
// the reader only loads it), which is exactly what an oscilloscope needs.
class Telemetry
{
public:
    static constexpr int kScopeBufferSize = 4096;   // ~85 ms at 48 kHz
    static_assert ((kScopeBufferSize & (kScopeBufferSize - 1)) == 0,
                   "scope buffer size must be a power of two for the index mask");

    Telemetry() = default;

    // Recompute the VU envelope release coefficient for the new host rate.
    // Called from prepareToPlay (message thread). The audio thread sees the
    // updated coef only after the next block boundary — fine for a smoothing
    // constant.
    void prepare (double hostSampleRate) noexcept;

    // --- Audio-thread writes ------------------------------------------------

    // Append `numSamples` of post-clip stereo output to the scope ring,
    // accumulate per-block peak L/R for the VU envelope and OR the
    // `clipDetectedThisRange` flag into the block accumulator.
    void pushSamples (const float* L, const float* R, int numSamples,
                      bool clipDetectedThisRange) noexcept;

    // Finalise the block: step the VU release envelope, publish vuL/vuR/clip,
    // store the voice-activity mask. Called once per processBlock.
    void finishBlock (std::uint32_t voiceMask) noexcept;

    // --- Message-thread reads -----------------------------------------------

    // Copy the most recent `destSize` scope samples into `dest`. Returns the
    // count actually written (capped at min(destSize, kScopeBufferSize, total
    // samples produced so far)). The data is mono — the audio thread averages
    // L+R on the way in.
    int readScope (float* dest, int destSize) const noexcept;

    float vuLeft()  const noexcept { return publishedVuL.load (std::memory_order_relaxed); }
    float vuRight() const noexcept { return publishedVuR.load (std::memory_order_relaxed); }

    // Read-and-clear the clip flag. Sticky between reads so a transient clip
    // never gets missed by a delayed timer callback; the UI converts a "true"
    // read into a 1 s decay animation.
    bool consumeClip() noexcept
    {
        return clipFlag.exchange (false, std::memory_order_relaxed);
    }

    std::uint32_t voiceMask() const noexcept { return mask.load (std::memory_order_relaxed); }

private:
    // Lossy SPSC scope ring. Only the audio thread advances writeIdx; the
    // reader takes a snapshot of writeIdx and copies the N samples preceding
    // it. A racing writer that advances mid-copy can corrupt at most the
    // oldest few sample positions of the snapshot — visible as a single-pixel
    // jitter at the scope's left edge, never a crash or buffer overrun.
    std::array<float, kScopeBufferSize> scope {};
    std::atomic<std::uint64_t> writeIdx { 0 };   // monotonically increasing

    // Block-level peak accumulators — audio thread only.
    float blockPeakL   = 0.0f;
    float blockPeakR   = 0.0f;
    bool  blockClipped = false;

    // VU envelope follower (fast attack, slow release). Audio thread only;
    // published values go out atomically. releaseCoef is recomputed in
    // prepare() so the release time stays ~300 ms regardless of host rate.
    float vuEnvL      = 0.0f;
    float vuEnvR      = 0.0f;
    float releaseCoef = 0.99f;

    std::atomic<float>         publishedVuL { 0.0f };
    std::atomic<float>         publishedVuR { 0.0f };
    std::atomic<bool>          clipFlag     { false };
    std::atomic<std::uint32_t> mask         { 0 };
};
