#pragma once

#include <atomic>

// Lock-free audio-thread → message-thread telemetry for the v2 header meter
// bay. Scope buffer + 16-voice mask + sticky clip flag from v1 are gone (see
// 02-strip-v1 task notes); the remaining surface is L/R peak VU plus a single
// noteOn boolean the UI uses for activity indication.
class Telemetry
{
public:
    Telemetry() = default;

    // Recompute the VU envelope release coefficient for the new host rate.
    // Called from prepareToPlay (message thread). The audio thread sees the
    // updated coef only after the next block boundary — fine for a smoothing
    // constant.
    void prepare (double hostSampleRate) noexcept;

    // --- Audio-thread writes ------------------------------------------------

    // Accumulate per-block peak L/R for the VU envelope.
    void pushSamples (const float* L, const float* R, int numSamples) noexcept;

    // Finalise the block: step the VU release envelope, publish vuL/vuR.
    // Called once per processBlock.
    void finishBlock() noexcept;

    // Set / clear the "any voice currently sounding" flag. The UI uses this to
    // drive a single activity indicator; finer per-voice state is gone in v2.
    void setNoteOn (bool on) noexcept
    {
        noteOnFlag.store (on, std::memory_order_relaxed);
    }

    // --- Message-thread reads -----------------------------------------------

    float vuLeft()  const noexcept { return publishedVuL.load (std::memory_order_relaxed); }
    float vuRight() const noexcept { return publishedVuR.load (std::memory_order_relaxed); }
    bool  noteOn()  const noexcept { return noteOnFlag.load (std::memory_order_relaxed); }

private:
    // Block-level peak accumulators — audio thread only.
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;

    // VU envelope follower (fast attack, slow release). Audio thread only;
    // published values go out atomically. releaseCoef is recomputed in
    // prepare() so the release time stays ~300 ms regardless of host rate.
    float vuEnvL      = 0.0f;
    float vuEnvR      = 0.0f;
    float releaseCoef = 0.99f;

    std::atomic<float> publishedVuL { 0.0f };
    std::atomic<float> publishedVuR { 0.0f };
    std::atomic<bool>  noteOnFlag   { false };
};
