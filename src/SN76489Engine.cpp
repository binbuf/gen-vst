#include "SN76489Engine.h"

#include <algorithm>
#include <cmath>

#include "VgmLogger.h"

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

// --- PsgEnvelope --------------------------------------------------------------
// Software amplitude ADSR for one PSG channel. The SN76489 has no envelope
// hardware; this struct synthesises one in software and the engine multiplies
// its amplitude into the per-channel mix gain (Task 23). Stage durations come
// from the operator-panel rate values 0..maxRate (0 = instant transition,
// maxRate = ~2 seconds at 44.1 kHz).

void SN76489Engine::PsgEnvelope::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void SN76489Engine::PsgEnvelope::setSampleRate (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
}

void SN76489Engine::PsgEnvelope::reset() noexcept
{
    stage         = Stage::Idle;
    amplitude     = 0.0f;
    peakLevel     = 1.0f;
    sustainAmp    = 1.0f;
    stageDelta    = 0.0f;
    stageSamples  = 0;
    keyDown       = false;
}

void SN76489Engine::PsgEnvelope::setRates (int newAtk, int newDr1, int newSus,
                                           int newDr2, int newRr) noexcept
{
    atk = std::clamp (newAtk, 0, 31);
    dr1 = std::clamp (newDr1, 0, 31);
    sus = std::clamp (newSus, 0, 15);
    dr2 = std::clamp (newDr2, 0, 31);
    rr  = std::clamp (newRr,  0, 15);

    // Sustain level: SUS=0 -> peak (no decay), SUS=15 -> silence after decay.
    sustainAmp = peakLevel * (1.0f - static_cast<float> (sus) / 15.0f);
}

void SN76489Engine::PsgEnvelope::setVelocitySensitivity (float vel01) noexcept
{
    velSensitivity = juce::jlimit (0.0f, 1.0f, vel01);
}

void SN76489Engine::PsgEnvelope::noteOn (int velocity) noexcept
{
    // Velocity scaling: vel == 0 -> always full peak; vel == 1 -> peak scales
    // linearly with MIDI velocity. The chip's own attenuation register is
    // also velocity-scaled (writeToneVolume), so the audible curve is the
    // product of the two — the envelope just shapes the float multiplier.
    const float v = juce::jlimit (0.0f, 1.0f,
                                  static_cast<float> (std::clamp (velocity, 0, 127)) / 127.0f);
    peakLevel  = (1.0f - velSensitivity) + velSensitivity * v;
    sustainAmp = peakLevel * (1.0f - static_cast<float> (sus) / 15.0f);
    keyDown    = true;
    enterAttack();
}

void SN76489Engine::PsgEnvelope::noteOff() noexcept
{
    keyDown = false;
    enterRelease();
}

void SN76489Engine::PsgEnvelope::advance (int n) noexcept
{
    while (n > 0 && stage != Stage::Idle)
    {
        if (stageSamples < 0)
        {
            // Sustain hold — amplitude doesn't change until noteOff() flips
            // the stage. Consume the whole block here.
            amplitude += stageDelta * static_cast<float> (n);
            return;
        }
        const int step = std::min (n, stageSamples);
        amplitude     += stageDelta * static_cast<float> (step);
        stageSamples  -= step;
        n             -= step;
        if (stageSamples == 0)
            advanceToNextStage();
    }
}

int SN76489Engine::PsgEnvelope::stageSamplesFromRate (int rate, int maxRate) const noexcept
{
    // rate == 0 -> instant (0 samples); rate == maxRate -> ~2 seconds at the
    // current host sample rate. Linear in between — sufficient for the
    // operator-panel knob shape, and gives the unit tests a predictable
    // "long ATK ramps over expected sample count" relationship.
    if (rate <= 0 || maxRate <= 0) return 0;
    const double maxSamples = sampleRate * 2.0;
    return static_cast<int> (std::round (maxSamples * static_cast<double> (rate)
                                                    / static_cast<double> (maxRate)));
}

void SN76489Engine::PsgEnvelope::enterAttack() noexcept
{
    stage         = Stage::Attack;
    stageSamples  = stageSamplesFromRate (atk, 31);
    if (stageSamples <= 0)
    {
        amplitude = peakLevel;
        advanceToNextStage();
        return;
    }
    stageDelta = (peakLevel - amplitude) / static_cast<float> (stageSamples);
}

void SN76489Engine::PsgEnvelope::enterDecay1() noexcept
{
    stage         = Stage::Decay1;
    stageSamples  = stageSamplesFromRate (dr1, 31);
    if (stageSamples <= 0)
    {
        amplitude = sustainAmp;
        advanceToNextStage();
        return;
    }
    stageDelta = (sustainAmp - amplitude) / static_cast<float> (stageSamples);
}

void SN76489Engine::PsgEnvelope::enterSustain() noexcept
{
    stage        = Stage::Sustain;
    amplitude    = sustainAmp;
    stageSamples = -1;          // hold until noteOff()
    stageDelta   = 0.0f;
}

void SN76489Engine::PsgEnvelope::enterDecay2() noexcept
{
    stage         = Stage::Decay2;
    stageSamples  = stageSamplesFromRate (dr2, 31);
    if (stageSamples <= 0)
    {
        // DR2 == 0 in the user model means "hold at sustain" — sit in Sustain
        // instead of decaying further. Note-off still flips us to Release.
        enterSustain();
        return;
    }
    stageDelta = (0.0f - amplitude) / static_cast<float> (stageSamples);
}

void SN76489Engine::PsgEnvelope::enterRelease() noexcept
{
    stage         = Stage::Release;
    stageSamples  = stageSamplesFromRate (rr, 15);
    if (stageSamples <= 0)
    {
        amplitude = 0.0f;
        stage     = Stage::Idle;
        stageDelta = 0.0f;
        return;
    }
    stageDelta = (0.0f - amplitude) / static_cast<float> (stageSamples);
}

void SN76489Engine::PsgEnvelope::advanceToNextStage() noexcept
{
    switch (stage)
    {
        case Stage::Attack:  enterDecay1(); break;
        case Stage::Decay1:  enterDecay2(); break;   // SR=0 path collapses to Sustain hold
        case Stage::Sustain: /* held — only noteOff() leaves */ break;
        case Stage::Decay2:  amplitude = 0.0f; stage = Stage::Idle; stageDelta = 0.0f; stageSamples = 0; break;
        case Stage::Release: amplitude = 0.0f; stage = Stage::Idle; stageDelta = 0.0f; stageSamples = 0; break;
        case Stage::Idle:    break;
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

void SN76489Engine::prepare (double sr, int maxBlockSize)
{
    hostSampleRate = sr > 0.0 ? sr : 44100.0;
    for (auto& c : chips)
        c.prepare (hostSampleRate, maxBlockSize);

    chipScratch.assign (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f);

    for (auto& state : ch)
        state.envelope.prepare (hostSampleRate);

    reset();
}

void SN76489Engine::reset()
{
    for (auto& c : chips)
        c.reset();

    for (auto& state : ch)
    {
        state.note                    = -1;
        state.velocity                = 0;
        state.active                  = false;
        state.bendSemitones           = 0.0;
        state.timestamp               = 0;
        state.glideCurrentMidi        = 0.0;
        state.glideTargetMidi         = 0.0;
        state.glideRateNotesPerSample = 0.0;
        state.envelope.reset();
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
    const bool wasActive = state.active;

    // Task 28 — start a glide when the channel was already sounding and the
    // user has dialled a non-zero glide time. A fresh allocation (channel was
    // idle / released) snaps to the new note: there is no previous pitch to
    // slide from, and the envelope's attack already shapes the onset.
    if (wasActive && state.glideTimeMs > 0.0
        && std::abs (static_cast<double> (midiNote) - state.glideCurrentMidi) > 1e-9)
    {
        const double samples = state.glideTimeMs * 0.001 * hostSampleRate;
        state.glideTargetMidi         = static_cast<double> (midiNote);
        state.glideRateNotesPerSample =
            (state.glideTargetMidi - state.glideCurrentMidi) / samples;
        // Don't write the freq register here — renderAdd will advance the
        // glide and write the divider every block until the target is hit.
    }
    else
    {
        state.glideCurrentMidi        = static_cast<double> (midiNote);
        state.glideTargetMidi         = state.glideCurrentMidi;
        state.glideRateNotesPerSample = 0.0;
        writeToneFreq (target, static_cast<double> (midiNote)
                                + (state.bendEnabled ? state.bendSemitones : 0.0));
    }

    state.note      = midiNote;
    state.velocity  = velocity;
    state.active    = true;
    state.timestamp = nextTimestamp++;

    // The chip is held at full output (atten 0); the software envelope (and
    // its velocity-sensitivity scalar) does all the volume work. Without this
    // the chip's own 4-bit velocity attenuation would double-count against
    // the envelope's velocity-scaled peakLevel (Task 23).
    writeToneVolume (target, 0x00);
    state.envelope.noteOn (velocity);
}

void SN76489Engine::noteOnNoise (int midiNote, int velocity)
{
    auto& state = ch[kNoiseCh];
    state.note      = midiNote;
    state.velocity  = velocity;
    state.active    = true;
    state.timestamp = nextTimestamp++;

    refreshNoiseControl();   // auto-mode may consume midiNote
    // Same software-envelope path as tones: chip held at full output, envelope
    // does the velocity + ADSR shaping.
    writeNoiseVolume (0x00);
    state.envelope.noteOn (velocity);
}

void SN76489Engine::noteOffTone (int midiNote)
{
    for (int i = 0; i < kNumToneChs; ++i)
    {
        auto& state = ch[static_cast<std::size_t> (i)];
        if (state.active && state.note == midiNote)
        {
            state.active = false;
            // Hand the volume off to the software envelope: keep the chip
            // generating samples at the velocity-attenuation level so the
            // release ramp has signal to multiply, and let the envelope's
            // amplitude tail to zero. With RR == 0 the envelope advances to
            // Idle on the first render block, matching the legacy step-off.
            state.envelope.noteOff();
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
        state.envelope.noteOff();
    }
}

void SN76489Engine::setPitchBendSemitones (int psgChannel, double semitones)
{
    if (psgChannel < 0 || psgChannel >= kNumChannels)
        return;

    auto& state = ch[static_cast<std::size_t> (psgChannel)];
    state.bendSemitones = semitones;

    // Tone channels: re-derive the divider if pitch bend is enabled and the
    // channel is sounding. Noise channel has no pitch. While a glide is in
    // progress the current interpolated pitch is the base — bend rides on top
    // and renderAdd keeps re-writing the divider as the glide advances.
    if (psgChannel < kNumToneChs && state.active && state.bendEnabled)
        writeToneFreq (psgChannel, state.glideCurrentMidi + semitones);
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

void SN76489Engine::setEnvelopeRates (int psgChannel, int atk, int dr1, int sus,
                                      int dr2, int rr) noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return;
    ch[static_cast<std::size_t> (psgChannel)].envelope.setRates (atk, dr1, sus, dr2, rr);
}

void SN76489Engine::setEnvelopeVel (int psgChannel, float vel01) noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return;
    ch[static_cast<std::size_t> (psgChannel)].envelope.setVelocitySensitivity (vel01);
}

void SN76489Engine::setGlideTimeMs (int psgChannel, double ms) noexcept
{
    // Noise has no pitch, so any glide setting is silently dropped.
    if (psgChannel < 0 || psgChannel >= kNumToneChs) return;
    ch[static_cast<std::size_t> (psgChannel)].glideTimeMs = ms < 0.0 ? 0.0 : ms;
}

// --- Per-block render ------------------------------------------------------

void SN76489Engine::renderAdd (float* outL, float* outR, int numSamples)
{
    if (numSamples <= 0)
        return;

    if (static_cast<int> (chipScratch.size()) < numSamples)
        chipScratch.assign (static_cast<std::size_t> (numSamples), 0.0f);

    // Task 28 — advance per-tone-channel portamento before generating samples.
    // The divider write happens once per block at the new interpolated pitch;
    // the chip resamples internally, so per-sample writes aren't necessary
    // for an audible slide at typical block sizes (07-feature-spec.md
    // "Portamento", 03-psg-synthesis.md).
    for (int t = 0; t < kNumToneChs; ++t)
    {
        auto& state = ch[static_cast<std::size_t> (t)];
        if (state.glideRateNotesPerSample == 0.0)
            continue;

        state.glideCurrentMidi +=
            state.glideRateNotesPerSample * static_cast<double> (numSamples);

        if ((state.glideRateNotesPerSample > 0.0
             && state.glideCurrentMidi >= state.glideTargetMidi)
            || (state.glideRateNotesPerSample < 0.0
                && state.glideCurrentMidi <= state.glideTargetMidi))
        {
            state.glideCurrentMidi        = state.glideTargetMidi;
            state.glideRateNotesPerSample = 0.0;
        }

        writeToneFreq (t, state.glideCurrentMidi
                            + (state.bendEnabled ? state.bendSemitones : 0.0));
    }

    for (int chIdx = 0; chIdx < kNumChannels; ++chIdx)
    {
        chips[static_cast<std::size_t> (chIdx)].generate (chipScratch.data(), numSamples);

        auto& state = ch[static_cast<std::size_t> (chIdx)];

        // Task 23: snapshot envelope amplitude at block start, advance the
        // envelope across the block, snapshot again at block end, and apply
        // a per-sample linear interpolation between the two — smooths the
        // 4-bit-quantised chip output enough that slow attack ramps don't
        // zipper. Cheap (one mul + one add per sample).
        const float envStart = state.envelope.amplitude;
        state.envelope.advance (numSamples);
        const float envEnd   = state.envelope.amplitude;

        const float gainL = state.panLeft  * state.volumeGain * mixLevel;
        const float gainR = state.panRight * state.volumeGain * mixLevel;

        const float invDen = numSamples > 1
                                 ? 1.0f / static_cast<float> (numSamples - 1)
                                 : 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float t      = static_cast<float> (i) * invDen;
            const float envAmp = envStart + (envEnd - envStart) * t;
            const float sample = chipScratch[static_cast<std::size_t> (i)] * envAmp;
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

float SN76489Engine::channelAmplitude (int psgChannel) const noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels) return 0.0f;
    return ch[static_cast<std::size_t> (psgChannel)].envelope.amplitude;
}

SN76489Engine::PsgEnvelope::Stage SN76489Engine::channelStage (int psgChannel) const noexcept
{
    if (psgChannel < 0 || psgChannel >= kNumChannels)
        return PsgEnvelope::Stage::Idle;
    return ch[static_cast<std::size_t> (psgChannel)].envelope.stage;
}

// --- Register protocol -----------------------------------------------------

void SN76489Engine::writeAllChips (std::uint8_t byte)
{
    for (auto& c : chips)
        c.write (byte);

    // Task 29 — mirror to the VGM logger once per logical write. The four
    // shadow chips are an implementation detail of soft-pan and stay invisible
    // here so the captured stream has the same single-chip event count a real
    // SN76489 would produce.
    if (vgmLogger != nullptr && vgmLogger->isActive())
        vgmLogger->recordPsgWrite (byte);
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
