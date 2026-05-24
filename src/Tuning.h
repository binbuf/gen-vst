#pragma once

#include <array>
#include <memory>

#include <juce_core/juce_core.h>

// Pre-computed per-note frequency table (MIDI notes 0–127, Hz).
// Built on the message thread; shared with the audio thread via atomic swap.
struct TuningTable
{
    std::array<double, 128> freq {};
    juce::String            description;

    // Hz for a (possibly fractional) MIDI note.
    // The fractional part (pitch bend) uses 12-TET semitone spacing on top of
    // the tuned base, which is the conventional expectation for all tuning systems.
    double lookupHz (double midiNote) const noexcept;

    static std::shared_ptr<TuningTable> makeDefault();   // 12-TET, A4 = 440 Hz
};

// Parse a Scala .scl file at `path`.
// Returns the table on success; nullptr + sets `error` on failure.
// Only 12-degree scales are supported for MVP — full .kbm mapping is out of scope.
std::shared_ptr<TuningTable> parseScl (const juce::String& path, juce::String& error);

// Tuning singleton — message thread sets the table, audio thread reads it.
// The active table is copied under a brief SpinLock; the table data itself is
// read outside the lock, so the audio thread is never blocked during lookup.
class Tuning
{
public:
    static Tuning& instance() noexcept;

    // Audio-thread safe: Hz for a (possibly fractional) MIDI note.
    double lookupHz (double midiNote) const noexcept;

    // Message thread: replace the active table and record its source path.
    // `sclPath` empty = built-in default (12-TET).
    void setTable (std::shared_ptr<TuningTable> table,
                   const juce::String& sclPath = {}) noexcept;

    // Message thread: reset to 12-TET and clear the stored path.
    void resetToDefault() noexcept;

    // Message thread: path of the active .scl file, or empty for 12-TET.
    juce::String activeSclPath() const;

private:
    Tuning();

    mutable juce::SpinLock        spinLock_;
    std::shared_ptr<TuningTable>  table_;    // guarded by spinLock_
    juce::String                  sclPath_;  // message thread only
};
