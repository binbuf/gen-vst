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

    // Set / clear the active state of a specific MIDI pitch (0-127). Used to
    // drive lit keys on the on-screen keyboard. Safe to call from the audio thread.
    void setNoteActive (int pitch, bool on) noexcept
    {
        if (pitch < 0 || pitch > 127) return;
        if (pitch < 64)
        {
            const uint64_t bit = uint64_t (1) << pitch;
            if (on) activeNotesMask0.fetch_or  (bit, std::memory_order_relaxed);
            else    activeNotesMask0.fetch_and (~bit, std::memory_order_relaxed);
        }
        else
        {
            const uint64_t bit = uint64_t (1) << (pitch - 64);
            if (on) activeNotesMask1.fetch_or  (bit, std::memory_order_relaxed);
            else    activeNotesMask1.fetch_and (~bit, std::memory_order_relaxed);
        }
    }

    // Clear all active-note bits (e.g. on mode switch or all-notes-off).
    void clearAllNotes() noexcept
    {
        activeNotesMask0.store (0, std::memory_order_relaxed);
        activeNotesMask1.store (0, std::memory_order_relaxed);
    }

    // --- Message-thread reads -----------------------------------------------

    float    vuLeft()  const noexcept { return publishedVuL.load (std::memory_order_relaxed); }
    float    vuRight() const noexcept { return publishedVuR.load (std::memory_order_relaxed); }
    bool     noteOn()  const noexcept { return noteOnFlag.load (std::memory_order_relaxed); }
    uint64_t activeNotesLow()  const noexcept { return activeNotesMask0.load (std::memory_order_relaxed); }
    uint64_t activeNotesHigh() const noexcept { return activeNotesMask1.load (std::memory_order_relaxed); }

private:
    // VU envelope follower (fast attack, slow release). Audio thread only;
    // published values go out atomically. releaseCoef is recomputed in
    // prepare() so the per-sample release time stays ~300 ms regardless of
    // host rate — applied inside pushSamples on every input sample.
    float vuEnvL      = 0.0f;
    float vuEnvR      = 0.0f;
    float releaseCoef = 0.99f;

    std::atomic<float>    publishedVuL    { 0.0f };
    std::atomic<float>    publishedVuR    { 0.0f };
    std::atomic<bool>     noteOnFlag      { false };
    std::atomic<uint64_t> activeNotesMask0 { 0 };  // pitches 0-63
    std::atomic<uint64_t> activeNotesMask1 { 0 };  // pitches 64-127
};
