#include "Tuning.h"

#include <algorithm>
#include <cmath>

// ---- TuningTable ---------------------------------------------------------------

double TuningTable::lookupHz (double midiNote) const noexcept
{
    const int    note = std::clamp (static_cast<int> (std::lround (midiNote)), 0, 127);
    const double base = freq[static_cast<std::size_t> (note)];
    const double frac = midiNote - note;
    if (frac == 0.0) return base;
    return base * std::pow (2.0, frac / 12.0);
}

std::shared_ptr<TuningTable> TuningTable::makeDefault()
{
    auto t = std::make_shared<TuningTable>();
    t->description = "12-TET (equal temperament)";
    for (int i = 0; i < 128; ++i)
        t->freq[static_cast<std::size_t> (i)] = 440.0 * std::pow (2.0, (i - 69.0) / 12.0);
    return t;
}

// ---- parseScl ------------------------------------------------------------------

namespace
{
    // Convert one Scala degree token to cents.
    // Accepts: "N/M" (ratio), "C.cc" (cents), "N" (integer N/1).
    bool parseDegree (const juce::String& token, double& centsOut, juce::String& error)
    {
        // Strip inline comment and whitespace
        const auto trimmed = token.upToFirstOccurrenceOf ("!", false, false).trim();
        if (trimmed.isEmpty()) { error = "empty degree line"; return false; }

        if (trimmed.contains ("/"))
        {
            const int num = trimmed.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
            const int den = trimmed.fromFirstOccurrenceOf ("/", false, false).trim().getIntValue();
            if (num <= 0 || den <= 0) { error = "invalid ratio: " + trimmed; return false; }
            centsOut = 1200.0 * std::log2 (static_cast<double> (num) / den);
            return true;
        }

        if (trimmed.containsChar ('.'))
        {
            centsOut = trimmed.getDoubleValue();
            return true;
        }

        // Integer: treat as N/1
        const int num = trimmed.getIntValue();
        if (num <= 0) { error = "invalid integer degree: " + trimmed; return false; }
        centsOut = 1200.0 * std::log2 (static_cast<double> (num));
        return true;
    }
}

std::shared_ptr<TuningTable> parseScl (const juce::String& path, juce::String& error)
{
    juce::File file (path);
    if (! file.existsAsFile())
    {
        error = "file not found: " + path;
        return nullptr;
    }

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());

    // Collect non-comment, non-empty data lines
    juce::StringArray data;
    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (! trimmed.startsWith ("!") && trimmed.isNotEmpty())
            data.add (trimmed);
    }

    if (data.size() < 2)
    {
        error = "invalid .scl: too few non-comment lines";
        return nullptr;
    }

    const juce::String description = data[0];
    const int          degreeCount = data[1].trim().getIntValue();

    if (degreeCount != 12)
    {
        error = "only 12-degree scales are supported for MVP (found "
              + juce::String (degreeCount)
              + " degrees — full .kbm mapping is out of scope)";
        return nullptr;
    }

    if (data.size() < 2 + degreeCount)
    {
        error = "invalid .scl: expected " + juce::String (degreeCount)
              + " degree entries, got " + juce::String (data.size() - 2);
        return nullptr;
    }

    std::array<double, 12> cents {};
    for (int i = 0; i < degreeCount; ++i)
    {
        juce::String degErr;
        if (! parseDegree (data[2 + i], cents[static_cast<std::size_t> (i)], degErr))
        {
            error = "degree " + juce::String (i + 1) + ": " + degErr;
            return nullptr;
        }
    }

    // Last degree must be the octave (within 1 cent of 1200)
    if (std::abs (cents[11] - 1200.0) > 1.0)
    {
        error = "last degree must be the octave (1200 cents / 2/1); got "
              + juce::String (cents[11], 3) + " cents";
        return nullptr;
    }

    // Build 128-entry frequency table. Root: MIDI 69 = 440 Hz.
    auto table = std::make_shared<TuningTable>();
    table->description = description;

    for (int note = 0; note < 128; ++note)
    {
        const int    offset = note - 69;
        const int    octave = static_cast<int> (std::floor (offset / 12.0));
        const int    degree = ((offset % 12) + 12) % 12;   // 0-11, always non-negative
        const double octHz  = 440.0 * std::pow (2.0, static_cast<double> (octave));

        table->freq[static_cast<std::size_t> (note)] =
            (degree == 0)
                ? octHz
                : octHz * std::pow (2.0, cents[static_cast<std::size_t> (degree) - 1] / 1200.0);
    }

    return table;
}

// ---- Tuning singleton ----------------------------------------------------------

Tuning::Tuning() : table_ (TuningTable::makeDefault()) {}

Tuning& Tuning::instance() noexcept
{
    static Tuning t;
    return t;
}

double Tuning::lookupHz (double midiNote) const noexcept
{
    std::shared_ptr<TuningTable> t;
    {
        const juce::SpinLock::ScopedLockType lock (spinLock_);
        t = table_;
    }
    return t->lookupHz (midiNote);
}

void Tuning::setTable (std::shared_ptr<TuningTable> table,
                       const juce::String& sclPath) noexcept
{
    {
        const juce::SpinLock::ScopedLockType lock (spinLock_);
        table_ = std::move (table);
    }
    sclPath_ = sclPath;
}

void Tuning::resetToDefault() noexcept
{
    setTable (TuningTable::makeDefault());
}

juce::String Tuning::activeSclPath() const
{
    return sclPath_;
}
