#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

// One SN76489 chip instance, configured for the Sega VDP variant
// (NTSC 3,579,545 Hz clock; feedback polynomial 0x0009; 16-bit shift register;
// initial LFSR state 0x8000). The wrapper is the **only** code that touches
// libvgm directly — every other module talks to chips through this interface
// (03-psg-synthesis.md "Wrapper Interface", ADR-0009).
//
// The chip generates samples at its native rate (clock/2/8 = ~223 kHz) and
// resamples internally to the host rate via juce::LagrangeInterpolator
// (ADR-0011): the PSG path does NOT pass through the FM mix-bus resampler.
//
// Per-channel panning needs per-channel signal access. The hardware sums all
// four channels into one mono output, so to recover individual channel
// signals SN76489Engine owns four wrappers (one per channel), each with a
// mute mask isolating its own channel. All four chips receive identical
// register writes, so their internal state stays in lockstep.
class SN76489Wrapper
{
public:
    static constexpr std::uint32_t kClockHz = 3579545;   // NTSC master / 15

    SN76489Wrapper();
    ~SN76489Wrapper();

    SN76489Wrapper (const SN76489Wrapper&)            = delete;
    SN76489Wrapper& operator= (const SN76489Wrapper&) = delete;

    // Allocate the resampler scratch buffer for up to maxBlockSize host
    // samples per generate() call. hostSampleRate is the rate generate()
    // produces; the native rate is fixed (clock/2/8).
    void prepare (double hostSampleRate, int maxBlockSize);

    // Silence the chip and reset the resampler. Register state goes back to
    // power-on defaults (volumes muted, LFSR seeded with 0x8000).
    void reset();

    // Mute mask: bit i = 1 mutes channel i (0..2 tone, 3 noise). This affects
    // only output mixing; the chip's internal state still advances normally
    // so the four-chip lockstep stays valid.
    void setMuteMask (std::uint32_t mask);

    // Write one byte to the SN76489 data port. The byte is interpreted by
    // the chip's LATCH/DATA wire protocol (03-psg-synthesis.md "Register
    // Protocol"). Writes are queued in libvgm's register file; the effect
    // is heard in the next generate() call.
    void write (std::uint8_t data);

    // Produce numSamples host-rate mono samples normalized to [-1, +1] and
    // OVERWRITE monoOut[0..numSamples-1] (does not accumulate).
    void generate (float* monoOut, int numSamples);

private:
    struct State;
    std::unique_ptr<State> state;

    double hostRate   = 44100.0;
    double nativeRate = 0.0;
    double speedRatio = 1.0;     // nativeRate / hostRate
    int    carry      = 0;       // unconsumed native samples held over

    // Native-rate scratch (libvgm produces stereo INT32; for the non-stereo
    // configuration L == R, so we keep only L). Host-rate floats fed to the
    // Lagrange resampler.
    std::vector<std::int32_t> nativeL;
    std::vector<std::int32_t> nativeR;
    std::vector<float>        nativeFloat;

    juce::LagrangeInterpolator resampler;
};
