#include "SN76489Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double kNtscClock     = static_cast<double> (SN76489Wrapper::kClockHz);
    constexpr int    kMinDivider    = 2;
    constexpr int    kMaxDivider    = 1023;

    // MIDI note (with optional bend fraction) -> SN76489 10-bit divider N.
    // f = clock / (32 * N)  =>  N = clock / (32 * freq)
    // Clamped to the chip's representable range; N < 2 produces ultrasonic
    // garbage on hardware and is treated as silence by clamping up.
    int midiNoteToDivider (double midiNote) noexcept
    {
        const double freq = 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0);
        if (freq <= 0.0) return kMaxDivider;
        const double n = kNtscClock / (32.0 * freq);
        const int    rounded = static_cast<int> (std::lround (n));
        return std::clamp (rounded, kMinDivider, kMaxDivider);
    }
}

SN76489Engine::SN76489Engine()
{
    // Set up the 4-chip lockstep with per-chip mute mask: chip i unmutes only
    // channel i. The mute mask uses bit i = 1 to mute channel i, so the
    // single un-muted channel is the bit complement of (1 << i).
    for (int i = 0; i < kNumChannels; ++i)
        chips[static_cast<std::size_t> (i)].setMuteMask (~(1u << i) & 0x0Fu);
}

void SN76489Engine::prepare (double hostSampleRate, int maxBlockSize)
{
    for (auto& c : chips)
        c.prepare (hostSampleRate, maxBlockSize);

    chipScratch.assign (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f);

    reset();
}

void SN76489Engine::reset()
{
    for (auto& c : chips)
        c.reset();

    for (auto& state : ch)
    {
        state.note          = -1;
        state.velocity      = 0;
        state.active        = false;
        state.bendSemitones = 0.0;
        state.timestamp     = 0;
    }
    nextTimestamp = 0;

    // Initialise all channel volumes to silence (atten = 15) so the chip is
    // quiet until a note-on writes a real attenuation.
    for (int t = 0; t < kNumToneChs; ++t)
        writeToneVolume (t, 0x0F);
    writeNoiseVolume (0x0F);
    refreshNoiseControl();
}

// --- MIDI events -----------------------------------------------------------

void SN76489Engine::noteOnTone (int midiNote, int velocity)
{
    // Round-robin LRU: pick an idle channel if any, else steal the oldest
    // active channel (smallest timestamp).
    int target = -1;
    for (int i = 0; i < kNumToneChs; ++i)
        if (! ch[static_cast<std::size_t> (i)].active)
        {
            target = i;
            break;
        }

    if (target == -1)
    {
        std::uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < kNumToneChs; ++i)
        {
            const auto ts = ch[static_cast<std::size_t> (i)].timestamp;
            if (ts < oldest)
            {
                oldest = ts;
                target = i;
            }
        }
    }

    auto& state = ch[static_cast<std::size_t> (target)];
    state.note      = midiNote;
    state.velocity  = velocity;
    state.active    = true;
    state.timestamp = nextTimestamp++;

    writeToneFreq   (target, static_cast<double> (midiNote)
                              + (state.bendEnabled ? state.bendSemitones : 0.0));
    writeToneVolume (target, velocityToAttenuation (velocity));
}

void SN76489Engine::noteOnNoise (int midiNote, int velocity)
{
    auto& state = ch[kNoiseCh];
    state.note      = midiNote;
    state.velocity  = velocity;
    state.active    = true;
    state.timestamp = nextTimestamp++;

    refreshNoiseControl();   // auto-mode may consume midiNote
    writeNoiseVolume (velocityToAttenuation (velocity));
}

void SN76489Engine::noteOffTone (int midiNote)
{
    for (int i = 0; i < kNumToneChs; ++i)
    {
        auto& state = ch[static_cast<std::size_t> (i)];
        if (state.active && state.note == midiNote)
        {
            state.active = false;
            writeToneVolume (i, 0x0F);
            return;
        }
    }
}

void SN76489Engine::noteOffNoise (int midiNote)
{
    auto& state = ch[kNoiseCh];
    if (state.active && state.note == midiNote)
    {
        state.active = false;
        writeNoiseVolume (0x0F);
    }
}

void SN76489Engine::setPitchBendSemitones (int psgChannel, double semitones)
{
    if (psgChannel < 0 || psgChannel >= kNumChannels)
        return;

    auto& state = ch[static_cast<std::size_t> (psgChannel)];
    state.bendSemitones = semitones;

    // Tone channels: re-derive the divider if pitch bend is enabled and the
    // channel is sounding. Noise channel has no pitch.
    if (psgChannel < kNumToneChs && state.active && state.bendEnabled)
        writeToneFreq (psgChannel, static_cast<double> (state.note) + semitones);
}

// --- Parameter setters -----------------------------------------------------

void SN76489Engine::setChannelVolume (int psgChannel, float gain01) noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return;
    ch[static_cast<std::size_t> (psgChannel)].volumeGain =
        juce::jlimit (0.0f, 1.0f, gain01);
}

void SN76489Engine::setChannelPan (int psgChannel, float pan) noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return;
    auto& state = ch[static_cast<std::size_t> (psgChannel)];
    const float clamped = juce::jlimit (-1.0f, 1.0f, pan);
    // Equal-power-ish pan with a linear approximation (precise enough for
    // PSG soft pan; full equal-power is overkill for 8-bit synth aesthetics).
    state.panLeft  = std::min (1.0f, 1.0f - clamped);
    state.panRight = std::min (1.0f, 1.0f + clamped);
}

void SN76489Engine::setChannelBendEnabled (int psgChannel, bool on) noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return;
    auto& state = ch[static_cast<std::size_t> (psgChannel)];
    const bool wasOn = state.bendEnabled;
    state.bendEnabled = on;
    // Turning bend off mid-note: snap back to the un-bent frequency.
    if (psgChannel < kNumToneChs && state.active && wasOn && ! on)
        writeToneFreq (psgChannel, static_cast<double> (state.note));
}

void SN76489Engine::setNoiseType (int periodicOrWhite) noexcept
{
    noiseType = juce::jlimit (0, 1, periodicOrWhite);
    refreshNoiseControl();
}

void SN76489Engine::setNoiseShiftRate (int rate0to3) noexcept
{
    noiseRate = juce::jlimit (0, 3, rate0to3);
    refreshNoiseControl();
}

void SN76489Engine::setNoiseAutoMode (bool on) noexcept
{
    noiseAuto = on;
    refreshNoiseControl();
}

void SN76489Engine::setMixLevel (float mix01) noexcept
{
    mixLevel = juce::jlimit (0.0f, 1.0f, mix01);
}

// --- Per-block render ------------------------------------------------------

void SN76489Engine::renderAdd (float* outL, float* outR, int numSamples)
{
    if (numSamples <= 0)
        return;

    if (static_cast<int> (chipScratch.size()) < numSamples)
        chipScratch.assign (static_cast<std::size_t> (numSamples), 0.0f);

    for (int chIdx = 0; chIdx < kNumChannels; ++chIdx)
    {
        chips[static_cast<std::size_t> (chIdx)].generate (chipScratch.data(), numSamples);

        const auto& state  = ch[static_cast<std::size_t> (chIdx)];
        const float gainL  = state.panLeft  * state.volumeGain * mixLevel;
        const float gainR  = state.panRight * state.volumeGain * mixLevel;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = chipScratch[static_cast<std::size_t> (i)];
            outL[i] += sample * gainL;
            outR[i] += sample * gainR;
        }
    }
}

// --- Introspection ---------------------------------------------------------

bool SN76489Engine::isToneChannelActive (int psgChannel) const noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumToneChs) return false;
    return ch[static_cast<std::size_t> (psgChannel)].active;
}

int SN76489Engine::toneChannelNote (int psgChannel) const noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumToneChs) return -1;
    return ch[static_cast<std::size_t> (psgChannel)].note;
}

bool SN76489Engine::isNoiseChannelActive() const noexcept
{
    return ch[kNoiseCh].active;
}

int SN76489Engine::noiseChannelNote() const noexcept
{
    return ch[kNoiseCh].note;
}

// --- Register protocol -----------------------------------------------------

void SN76489Engine::writeAllChips (std::uint8_t byte)
{
    for (auto& c : chips)
        c.write (byte);
}

void SN76489Engine::writeToneFreq (int toneChannel, double midiNoteWithBend)
{
    const int n     = midiNoteToDivider (midiNoteWithBend);
    const auto low4 = static_cast<std::uint8_t> (n & 0x0F);
    const auto hi6  = static_cast<std::uint8_t> ((n >> 4) & 0x3F);

    // LATCH: channel `toneChannel`, register=frequency (bit4=0), low 4 bits.
    const auto latch = static_cast<std::uint8_t> (
        0x80 | ((toneChannel & 0x03) << 5) | (0 << 4) | low4);
    writeAllChips (latch);
    writeAllChips (hi6);
}

void SN76489Engine::writeToneVolume (int toneChannel, std::uint8_t attenuation)
{
    const auto byte = static_cast<std::uint8_t> (
        0x80 | ((toneChannel & 0x03) << 5) | (1 << 4) | (attenuation & 0x0F));
    writeAllChips (byte);
}

void SN76489Engine::writeNoiseVolume (std::uint8_t attenuation)
{
    const auto byte = static_cast<std::uint8_t> (
        0x80 | (3 << 5) | (1 << 4) | (attenuation & 0x0F));
    writeAllChips (byte);
}

void SN76489Engine::refreshNoiseControl()
{
    int rate = noiseRate;

    // Optional auto-mode (off by default): map the noise channel's current
    // MIDI note to a shift rate (03-psg-synthesis.md table). Direct
    // `noiseRate` is overridden whenever auto-mode is on AND a note is sounding.
    if (noiseAuto && ch[kNoiseCh].active && ch[kNoiseCh].note >= 0)
        rate = midiNoteToShiftRate (ch[kNoiseCh].note);

    // Noise-control LATCH: ch3 (11), register=frequency (bit4=0),
    // bit3=type (0=periodic, 1=white), bits2:1=shift rate, bit0=unused.
    const auto byte = static_cast<std::uint8_t> (
        0x80 | (3 << 5) | (0 << 4)
             | ((noiseType & 0x01) << 3)
             | ((rate & 0x03) << 1));
    writeAllChips (byte);
}

// --- Helpers ---------------------------------------------------------------

std::uint8_t SN76489Engine::velocityToAttenuation (int velocity) noexcept
{
    // (1 - vel/127) * 15, rounded. v=127 -> atten 0 (loudest); v=0 -> atten 15
    // (silent). 03-psg-synthesis.md "Volume Register".
    const int clamped = std::clamp (velocity, 0, 127);
    const int atten   = static_cast<int> (std::lround (
        (1.0 - clamped / 127.0) * 15.0));
    return static_cast<std::uint8_t> (std::clamp (atten, 0, 15));
}

int SN76489Engine::midiNoteToShiftRate (int midiNote) noexcept
{
    // 03-psg-synthesis.md "Noise Control — Direct UI Parameters", auto-mode
    // table: 0-37 -> 10 (low), 38-73 -> 01 (mid), 74+ -> 00 (high).
    if (midiNote <= 37) return 0b10;
    if (midiNote <= 73) return 0b01;
    return 0b00;
}
