#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "GenVstYmfmInterface.h"
#include "ymfm_opn.h"

// PCM sample channel — the **dedicated 17th `ymfm::ym2612` instance** reserved
// for DAC playback (ADR-0014, 07-feature-spec.md "DAC Mode Specification").
// The instance enables DACEN (0x2B = 0x80) on its own channel 6 and is fed
// 8-bit unsigned PCM via register 0x2A. It is **not** part of the 16-voice
// pool — FM polyphony is never reduced by DAC use.
//
// Phase-accurate timing: the DAC writes one byte every native-rate /
// dacRate samples, using an accumulator-based fractional clock
// (07-feature-spec.md). The chip runs at its native rate (~53,267 Hz) and is
// summed into the FM mix bus BEFORE the single resample pass (ADR-0011), so
// `renderAdd` produces native-rate samples.
class DACPlayer
{
public:
    enum class Mode { OneShot, Loop };

    DACPlayer();

    DACPlayer (const DACPlayer&)            = delete;
    DACPlayer& operator= (const DACPlayer&) = delete;

    // Allocate scratch and write the one-time chip setup (DACEN on channel 6).
    // hostSampleRate is informational; the DAC ticks at its native ymfm rate.
    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    // --- WAV loading (message thread) ----------------------------------------

    // Load a WAV via juce::AudioFormatManager / juce::AudioFormatReader, mix
    // to mono, convert to 8-bit unsigned PCM at the current dacRate, and
    // store. Returns true on success. Embedding the PCM in plugin state is
    // Task 16; this loader is enough for the dev/manual verification path.
    bool loadWav (const juce::File& file);

    // Clear the loaded PCM and stop any in-flight playback.
    void clearPcm();
    bool hasPcm() const noexcept;

    // --- Parameters ----------------------------------------------------------

    void setEnabled (bool on) noexcept     { enabled = on; }
    void setMode    (Mode m) noexcept      { mode = m; }
    void setLevel   (float gain01) noexcept { level = juce::jlimit (0.0f, 1.0f, gain01); }
    void setDacRate (int hz);   // 8000 / 11025 / 22050 — regenerates pcm[] from srcFloat[]

    bool  isEnabled() const noexcept       { return enabled; }
    Mode  getMode()   const noexcept       { return mode; }
    float getLevel()  const noexcept       { return level; }
    int   getDacRate() const noexcept      { return dacRate; }

    // --- MIDI triggers -------------------------------------------------------

    // Begin playback from the start of the loaded PCM. Velocity is currently
    // unused (the DAC has no per-sample volume — playback level is the
    // `dac_level` global parameter), but is accepted for API symmetry.
    void trigger (int midiNote, int velocity);

    // Stop playback immediately; the chip outputs whatever it last latched.
    void release();

    bool isPlaying() const noexcept { return playing; }

    // --- Render --------------------------------------------------------------

    // Generate `numNativeSamples` ymfm-native samples and ADD them to the
    // L/R buffers, applying the level/enable gating.
    void renderAdd (float* nativeL, float* nativeR, int numNativeSamples);

    std::uint32_t nativeSampleRate();

    // --- Static helpers (tests) ----------------------------------------------

    // Convert a normalized float sample [-1, +1] to the unsigned 8-bit value
    // the YM2612 DAC expects (0x80 = silence midpoint).
    static std::uint8_t floatTo8BitUnsigned (float s) noexcept;

    // 8000/11025/22050 -> a valid stored rate; falls back to 22050 on
    // unrecognised inputs.
    static int normaliseDacRate (int hz) noexcept;

private:
    void writeReg (std::uint8_t addr, std::uint8_t value);
    void regeneratePcmFromSource();
    std::uint8_t fetchNextSampleByte();

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    bool  enabled  = false;
    Mode  mode     = Mode::OneShot;
    float level    = 1.0f;
    int   dacRate  = 22050;

    // 8-bit unsigned PCM resampled to dacRate.
    std::vector<std::uint8_t> pcm;

    // Original mono float source plus its sample rate, retained so dacRate
    // changes can resample without forcing a WAV reload.
    std::vector<float> srcFloat;
    double             srcRate = 0.0;

    // Playback cursor (in pcm[] indices) and phase accumulator for the
    // per-native-sample DAC write timing.
    bool        playing               = false;
    std::size_t playPos               = 0;
    double      samplesPerWrite       = 1.0;   // native samples between DAC writes
    double      writeAccumulator      = 0.0;

    // Per-sample scaling factor for the chip output (matches Voice.cpp).
    static constexpr float kSampleScale = 0.5f / 32768.0f;
};
