#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace juce { class AudioProcessorValueTreeState; }

// `.psg` — v2 SQ-mode JSON preset (04-patch-system.md *.psg Format*, ADR-0025).
//
// One preset holds per-channel envelope + volume + pan for 3 tone channels +
// the noise channel, plus the noise type/rate. Schema version 1 is the only
// supported version. Out-of-range and missing fields clamp to defaults; an
// unparseable file returns an error string for the notification toast and
// does not load.

struct PsgPresetChannel
{
    // ADSR — semantics match the SN76489Engine::PsgEnvelope (Task 23). Stored
    // verbatim from the JSON, then clamped to the SQ apvts ranges by the
    // loader.
    int   atk = 0;          // 0-31
    int   dr1 = 0;          // 0-31
    int   sus = 0;          // 0-15
    int   dr2 = 0;          // 0-31
    int   rr  = 0;          // 0-15

    float vol = 1.0f;       // 0.0 .. 1.0
    float pan = 0.0f;       // -1.0 .. +1.0

    // Tone-only: semitone detune (-100 .. +100). Ignored for the noise
    // channel (where it isn't present in the schema).
    int   detune = 0;
};

struct PsgPreset
{
    static constexpr int kNumTones = 3;

    int                                  version = 1;
    std::string                          name;
    std::array<PsgPresetChannel, kNumTones> tones {};   // tone1 / tone2 / tone3
    PsgPresetChannel                     noise {};

    // Noise-only fields (string enums in the JSON; clamped to apvts choice
    // index on apply). Defaults match the apvts defaults.
    std::string noiseType = "white";       // "white" | "periodic"
    std::string noiseRate = "mid";         // "low" | "mid" | "high" | "ch2"
};

struct PsgPresetLoadResult
{
    std::optional<PsgPreset> preset;
    std::string              error;
};

// Parse a `.psg` JSON file. Message thread only. Missing or out-of-range
// fields clamp to defaults; an unparseable or non-JSON file returns an
// error and no preset.
PsgPresetLoadResult loadPsgPreset (const std::filesystem::path& path);

// Write `preset` to disk as JSON. Returns empty on success or a descriptive
// error string on failure. Message thread only.
std::string savePsgPreset (const PsgPreset& preset, const std::filesystem::path& path);

// Apply every field of `preset` into the matching `psg_*` apvts params via
// setValueNotifyingHost (so the host's automation graph + UI both update).
// Message thread only. `apvts` is the live processor apvts.
void applyPsgPresetToApvts (const PsgPreset& preset,
                            juce::AudioProcessorValueTreeState& apvts);

// Read every `psg_*` apvts param into a fresh PsgPreset. Used by the
// save / export paths. Message thread only. `name` populates PsgPreset::name.
PsgPreset readPsgPresetFromApvts (const juce::AudioProcessorValueTreeState& apvts,
                                  const std::string& name);
