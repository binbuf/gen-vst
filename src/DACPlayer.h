#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "GenVstYmfmInterface.h"
#include "ymfm_opn.h"

class DACKit;

// PCM sample channel — the **dedicated 17th `ymfm::ym2612` instance** reserved
// for DAC playback (ADR-0014, 07-feature-spec.md "DAC Mode Specification").
// The instance enables DACEN (0x2B = 0x80) on its own channel 6 and is fed
// 8-bit unsigned PCM via register 0x2A. It is **not** part of the 16-voice
// pool — FM polyphony is never reduced by DAC use.
//
// Task 31 multi-sample upgrade: the player no longer owns the single PCM
// buffer. Sample bytes live in DACKit cells (one per grid note, C-3..G-4);
// trigger() looks the cell up by MIDI note and arms it for the next render
// block. The phase-accurate write cadence (07-feature-spec.md) is unchanged
// — only the *source* of the byte stream is multi-sample.
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

    // Bind the multi-sample kit. The kit's lifetime must outlive the
    // DACPlayer; it's owned by the processor.
    void setKit (DACKit* kit) noexcept { dacKit = kit; }

    // --- Parameters ----------------------------------------------------------

    void setEnabled (bool on) noexcept     { enabled.store (on, std::memory_order_release); }
    void setMode    (Mode m) noexcept      { modeInt.store ((int) m, std::memory_order_release); }
    void setLevel   (float gain01) noexcept { level.store (juce::jlimit (0.0f, 1.0f, gain01),
                                                            std::memory_order_release); }

    bool  isEnabled() const noexcept       { return enabled.load (std::memory_order_acquire); }
    Mode  getMode()   const noexcept       { return (Mode) modeInt.load (std::memory_order_acquire); }
    float getLevel()  const noexcept       { return level.load (std::memory_order_acquire); }

    // --- MIDI triggers -------------------------------------------------------

    // Audio-thread call. Looks up the kit cell for `midiNote`; if the cell
    // has PCM, arm playback so the next renderAdd starts streaming its bytes
    // into 0x2A. Velocity is currently unused (the DAC has no per-sample
    // volume) but is accepted for API symmetry with the FM / PSG triggers.
    // Out-of-grid notes or empty cells are silent.
    void trigger (int midiNote, int velocity);

    // Stop playback immediately; the chip outputs whatever it last latched.
    void release();

    bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }

    // The cell currently driving playback. -1 = none. Read on the audio
    // thread; exposed for tests + the JS UI's "which cell is sounding" hook.
    int activeCellIndex() const noexcept { return activeCellIdx; }

    // --- Render --------------------------------------------------------------

    // Generate `numNativeSamples` ymfm-native samples and ADD them to the
    // L/R buffers, applying the level/enable gating.
    void renderAdd (float* nativeL, float* nativeR, int numNativeSamples);

    std::uint32_t nativeSampleRate();

    // --- Static helpers (tests) ----------------------------------------------
    static std::uint8_t floatTo8BitUnsigned (float s) noexcept;
    static int          normaliseDacRate    (int hz)  noexcept;

private:
    void          writeReg (std::uint8_t addr, std::uint8_t value);
    std::uint8_t  fetchNextSampleByte();
    void          recomputeSamplesPerWrite (int cellRate) noexcept;

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    // --- Atomic player state (touched by both threads) -----------------------
    std::atomic<bool>  enabled { false };
    std::atomic<int>   modeInt { (int) Mode::OneShot };
    std::atomic<float> level   { 1.0f };
    std::atomic<bool>  playing { false };

    // Non-owning pointer to the kit owned by GenVstAudioProcessor. The
    // audio thread reads cell bytes through this pointer; the message thread
    // mutates kit cells via DACKit's stage-swap protocol.
    DACKit* dacKit = nullptr;

    // --- Audio-thread playback state -----------------------------------------
    // activeCellIdx is the cell currently streaming (-1 = none). Pending
    // arming is set by trigger() and consumed at the top of the next
    // renderAdd, both on the audio thread — plain ints suffice.
    int    activeCellIdx    = -1;
    int    pendingCellIdx   = -1;
    bool   pendingCellSet   = false;

    std::size_t playPos          = 0;
    double      writeAccumulator = 0.0;
    double      samplesPerWrite  = 1.0;

    // Per-sample scaling factor for the chip output (matches Voice.cpp).
    static constexpr float kSampleScale = 0.5f / 32768.0f;
};
