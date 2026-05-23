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
    // store. Returns true on success.
    bool loadWav (const juce::File& file);

    // Restore PCM directly from raw 8-bit unsigned bytes at the given dacRate
    // (the Task 16 state-restore path — bypasses the WAV decoder so a project
    // is self-contained without an external WAV file). `name` is the display
    // filename the D-view (08-ui-views.md view 3) shows.
    void loadRawPcm (const std::uint8_t* bytes, std::size_t numBytes,
                     int dacRateHz, const juce::String& name);

    // Clear the loaded PCM and stop any in-flight playback.
    void clearPcm();
    bool hasPcm() const noexcept;

    // --- Raw PCM accessors (Task 16 state save) ------------------------------
    // The stored 8-bit unsigned PCM (resampled to dacRate). Empty when no
    // sample is loaded. Pointer is invalidated by clearPcm / loadWav /
    // loadRawPcm / setDacRate — read on the message thread only.
    //
    // We expose the *message-thread mirror* (mtPcm), not the audio-thread
    // live buffer (pcm). The two have identical content modulo one block of
    // audio-thread lag while the swap is pending — for state save and the
    // D-view's UI peaks that lag is invisible. Reading the audio-thread pcm
    // from the message thread would be a data race during a pending swap.
    const std::uint8_t* getRawPcmData() const noexcept
    {
        return mtPcm.empty() ? nullptr : mtPcm.data();
    }
    std::size_t         getRawPcmSize() const noexcept { return mtPcm.size(); }

    // --- Sample-info accessors (Task 13 D-view) ------------------------------

    // Filename of the most-recently loaded WAV (sans path). Empty if no
    // sample is loaded.
    juce::String getSampleName() const noexcept { return sampleName; }

    // Length of the loaded sample in seconds (computed at the current
    // dacRate). 0.0 when nothing is loaded.
    double getSampleLengthSeconds() const noexcept;

    // The DAC always stores 8-bit unsigned PCM (07-feature-spec.md). Returned
    // as a constant for parity with the JS DAC-info contract.
    int getSampleBitDepth() const noexcept { return 8; }

    // Compute `numBuckets` peak magnitudes (each in [0, 1]) for the loaded
    // PCM, used by the waveform-display widget. Returns an empty vector when
    // no sample is loaded.
    std::vector<float> computePeaks (int numBuckets) const;

    // --- Parameters ----------------------------------------------------------

    void setEnabled (bool on) noexcept     { enabled.store (on, std::memory_order_release); }
    void setMode    (Mode m) noexcept      { modeInt.store ((int) m, std::memory_order_release); }
    void setLevel   (float gain01) noexcept { level.store (juce::jlimit (0.0f, 1.0f, gain01),
                                                            std::memory_order_release); }
    void setDacRate (int hz);   // 8000 / 11025 / 22050 — regenerates pcm[] from srcFloat[]

    bool  isEnabled() const noexcept       { return enabled.load (std::memory_order_acquire); }
    Mode  getMode()   const noexcept       { return (Mode) modeInt.load (std::memory_order_acquire); }
    float getLevel()  const noexcept       { return level.load (std::memory_order_acquire); }
    int   getDacRate() const noexcept      { return mtDacRate; }

    // --- MIDI triggers -------------------------------------------------------

    // Begin playback from the start of the loaded PCM. Velocity is currently
    // unused (the DAC has no per-sample volume — playback level is the
    // `dac_level` global parameter), but is accepted for API symmetry.
    void trigger (int midiNote, int velocity);

    // Stop playback immediately; the chip outputs whatever it last latched.
    void release();

    bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }

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
    std::vector<std::uint8_t> regenerateBytes (const std::vector<float>& src,
                                               double sourceRate, int destDacRate) const;
    void stageSwap (std::vector<std::uint8_t> newBytes, int newDacRate);
    std::uint8_t fetchNextSampleByte();

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    // --- Atomic player state (touched by both threads) -----------------------
    // enabled / mode / level: written from the message thread (apvts setters)
    // and read on the audio thread inside renderAdd. Plain scalars are not
    // safe for that cross-thread access pattern; atomics with release/acquire
    // make the writes promptly visible to the audio thread without locks.
    std::atomic<bool>  enabled { false };
    std::atomic<int>   modeInt { (int) Mode::OneShot };   // Mode-as-int for atomic<>
    std::atomic<float> level   { 1.0f };

    // playing: written from the audio thread (on loop end / one-shot done) AND
    // from the message thread (trigger / release / clearPcm). Atomic boolean
    // is the right primitive for both write directions.
    std::atomic<bool>  playing { false };

    // --- Audio-thread-owned live buffer --------------------------------------
    // pcm / dacRate / samplesPerWrite / playPos / writeAccumulator are read in
    // renderAdd. They are NEVER written directly from the message thread; new
    // content is published via the staging area below and swapped in at the
    // top of renderAdd. Within a single renderAdd call these are stable.
    std::vector<std::uint8_t> pcm;
    int                       dacRate         = 22050;
    double                    samplesPerWrite = 1.0;
    std::size_t               playPos         = 0;
    double                    writeAccumulator = 0.0;

    // --- Message-thread mirrors (state save + UI display) --------------------
    // mtPcm/mtDacRate mirror what was most-recently staged; getRawPcmData() /
    // getSampleLengthSeconds() / hasPcm() read from here so the message
    // thread never races with the audio thread on pcm. srcFloat and srcRate
    // back the resampler in setDacRate() and the waveform peaks in
    // computePeaks(); the audio thread never touches them.
    std::vector<std::uint8_t> mtPcm;
    int                       mtDacRate  = 22050;
    std::vector<float>        srcFloat;
    double                    srcRate    = 0.0;
    juce::String              sampleName;

    // --- Staging area (lock-free swap protocol) ------------------------------
    // The message thread fills stagingPcm + stagingDacRate + stagingSamplesPerWrite
    // and then sets pendingSwap.store(true, release). The audio thread checks
    // pendingSwap at the top of renderAdd; on observe-true it pcm.swap(staging)
    // + copies the scalar fields + resets playPos/writeAccumulator. The OLD
    // pcm ends up in stagingPcm, where the next message-thread stageSwap call
    // overwrites it (allocator activity stays off the audio thread).
    //
    // stageSwap busy-waits on pendingSwap before writing staging — guarantees
    // we never overwrite a staging buffer that the audio thread is in the
    // middle of swapping. The wait is < 1 audio block in normal operation.
    std::vector<std::uint8_t> stagingPcm;
    int                       stagingDacRate         = 22050;
    double                    stagingSamplesPerWrite = 1.0;
    std::atomic<bool>         pendingSwap { false };

    // Per-sample scaling factor for the chip output (matches Voice.cpp).
    static constexpr float kSampleScale = 0.5f / 32768.0f;
};
