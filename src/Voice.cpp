#include "Voice.h"

#include <cmath>

#include "FmRegisterMap.h"
#include "VgmLogger.h"

namespace
{
    // YM2612 NTSC input clock (02-fm-synthesis.md). chip.sample_rate() turns it
    // into the ~53267 Hz native output rate.
    constexpr std::uint32_t kNtscClock = 7670454;

    // Per-voice output scale. ymfm yields roughly +/-32768 per voice; summing up
    // to 16 voices needs headroom before the master soft-clip guard. This is a
    // deliberate starting point — final headroom tuning is a later task
    // (01-architecture.md "Render Pipeline").
    constexpr float kSampleScale = 0.5f / 32768.0f;
}

Voice::Voice()
{
    reset();
}

void Voice::reset()
{
    chip.reset();
    shadow.fill (-1);
    voiceState              = State::Idle;
    partIndex               = -1;
    midiNote                = -1;
    noteVelocity            = 127;
    bendSemitones           = 0.0;
    voiceDetune             = 0.0;
    sustained               = false;
    lastNoteOnTime          = 0;
    glideCurrentMidi        = 0.0;
    glideTargetMidi         = 0.0;
    glideRateNotesPerSample = 0.0;
}

std::uint32_t Voice::nativeSampleRate()
{
    return chip.sample_rate (kNtscClock);
}

void Voice::writeReg (std::uint8_t reg, std::uint8_t value)
{
    // Channel 0 lives in Bank 0: address port 0, data port 1.
    chip.write (0, reg);
    chip.write (1, value);

    // Task 29 — mirror every chip write into the VGM logger when active. The
    // logger remaps chip-channel-0 to the VGM channel matching partIndex so
    // the captured file produces the same channel layout the listener heard.
    if (vgmLogger != nullptr && vgmLogger->isActive())
        vgmLogger->recordYm2612VoiceWrite (partIndex, reg, value);
}

void Voice::noteOn (int part, int note, int velocity, double bend,
                    bool velToTl, const Patch& patch, std::uint64_t timestamp,
                    double voiceDetuneSemitones)
{
    // Apply the full note-on sequence unconditionally — a stolen voice's chip
    // still holds the previous patch — and seed the shadow with every param
    // register so later dirty-diffs have a baseline. The two 0x28 key writes
    // are never shadowed: they are events, not state.
    const FmRegisterMap::NoteParams np { velocity, velToTl, bend + voiceDetuneSemitones };
    for (const auto& w : FmRegisterMap::buildNoteOn (patch, note, np))
    {
        writeReg (w.reg, w.value);
        if (w.reg != 0x28)
            shadow[w.reg] = w.value;
    }

    voiceState              = State::Active;
    partIndex               = part;
    midiNote                = note;
    noteVelocity            = velocity;
    bendSemitones           = bend;
    voiceDetune             = voiceDetuneSemitones;
    sustained               = false;
    lastNoteOnTime          = timestamp;
    // A fresh note-on always snaps the glide tracker to the new note — there
    // is no previous pitch to slide from when the voice was Idle / Released.
    glideCurrentMidi        = static_cast<double> (note);
    glideTargetMidi         = glideCurrentMidi;
    glideRateNotesPerSample = 0.0;
}

void Voice::legatoTo (int note, int velocity, double bend, bool velToTl,
                      const Patch& patch, std::uint64_t timestamp,
                      double glideTimeSamples)
{
    // Mono Legato: update the serving note / velocity / bend in place and let
    // the dirty-diff push the new frequency (and any TL change from a different
    // velocity) onto the chip — no key-off / key-on, so the envelope continues
    // from its current level (07-feature-spec.md Mono "Legato").
    midiNote       = note;
    noteVelocity   = velocity;
    bendSemitones  = bend;
    lastNoteOnTime = timestamp;

    if (glideTimeSamples > 0.0
        && std::abs (static_cast<double> (note) - glideCurrentMidi) > 1e-9)
    {
        // Task 28 — start (or re-target) a glide. glideCurrentMidi is held at
        // wherever the previous note left it (potentially mid-glide), so a
        // re-target while still sliding picks up smoothly from the live pitch
        // rather than snapping back to the previous note's int value.
        glideTargetMidi         = static_cast<double> (note);
        glideRateNotesPerSample =
            (glideTargetMidi - glideCurrentMidi) / glideTimeSamples;
        // updateRegisters writes every param register; the frequency line uses
        // glideCurrentMidi so the chip's current freq is preserved at this
        // call — advanceGlide will walk it toward the target in subsequent
        // blocks. Velocity / TL / patch changes still propagate immediately.
        updateRegisters (patch, velToTl);
    }
    else
    {
        // Glide disabled (time == 0) or already at target — snap as before.
        glideCurrentMidi        = static_cast<double> (note);
        glideTargetMidi         = glideCurrentMidi;
        glideRateNotesPerSample = 0.0;
        updateRegisters (patch, velToTl);
    }
}

void Voice::advanceGlide (int numSamples)
{
    if (glideRateNotesPerSample == 0.0 || numSamples <= 0)
        return;

    glideCurrentMidi += glideRateNotesPerSample * static_cast<double> (numSamples);

    // Detect overshoot in either direction and clamp to the target — the
    // glide ends precisely at the destination so subsequent blocks no-op.
    if ((glideRateNotesPerSample > 0.0 && glideCurrentMidi >= glideTargetMidi)
        || (glideRateNotesPerSample < 0.0 && glideCurrentMidi <= glideTargetMidi))
    {
        glideCurrentMidi        = glideTargetMidi;
        glideRateNotesPerSample = 0.0;
    }

    writeFreqRegistersForMidi (glideCurrentMidi + voiceDetune + bendSemitones);
}

void Voice::writeFreqRegistersForMidi (double effectiveMidi)
{
    const FmRegisterMap::FreqRegs f = FmRegisterMap::midiNoteToFreq (effectiveMidi);
    const std::uint8_t a4 = static_cast<std::uint8_t> (
        ((f.blk & 0x07) << 3) | ((f.fnum >> 8) & 0x07));
    const std::uint8_t a0 = static_cast<std::uint8_t> (f.fnum & 0xFF);

    // YM2612 protocol: write the high byte (0xA4) before the low byte (0xA0)
    // — the chip latches the high byte on a low-byte write so both register
    // halves take effect atomically (02-fm-synthesis.md "Frequency Writes").
    if (shadow[0xA4] != a4)
    {
        writeReg (0xA4, a4);
        shadow[0xA4] = a4;
    }
    if (shadow[0xA0] != a0)
    {
        writeReg (0xA0, a0);
        shadow[0xA0] = a0;
    }
}

void Voice::noteOff()
{
    const RegWrite off = FmRegisterMap::buildKeyOff();
    writeReg (off.reg, off.value);
    voiceState = State::Released;
    sustained  = false;
}

void Voice::updateRegisters (const Patch& patch, bool velToTl)
{
    // Re-derive the param register set and write only what changed vs the
    // shadow. The 0x28 key events are skipped — re-sending them would retrigger
    // the envelope. The frequency registers fall out of the diff naturally for
    // a static note; a pitch-bend or note change updates them through this same
    // path. Per-voice Unison detune is folded into the bend so each voice in a
    // stack lands on its own F-number. While a glide is in progress
    // (glideCurrentMidi != midiNote) the frequency line follows the
    // interpolated pitch so a patch / TL / bend edit doesn't snap the audible
    // frequency back to the target mid-glide.
    const int intPart  = static_cast<int> (std::floor (glideCurrentMidi));
    const double frac  = glideCurrentMidi - static_cast<double> (intPart);
    const FmRegisterMap::NoteParams np {
        noteVelocity, velToTl, bendSemitones + voiceDetune + frac };
    for (const auto& w : FmRegisterMap::buildNoteOn (patch, intPart, np))
    {
        if (w.reg == 0x28)
            continue;

        if (shadow[w.reg] != w.value)
        {
            writeReg (w.reg, w.value);
            shadow[w.reg] = w.value;
        }
    }
}

void Voice::setPitchBend (double bend, const Patch& patch, bool velToTl)
{
    bendSemitones = bend;
    updateRegisters (patch, velToTl);
}

void Voice::renderAdd (float* accumL, float* accumR, int numSamples)
{
    ymfm::ym2612::output_data sample;
    for (int i = 0; i < numSamples; ++i)
    {
        chip.generate (&sample, 1);
        accumL[i] += static_cast<float> (sample.data[0]) * kSampleScale;
        accumR[i] += static_cast<float> (sample.data[1]) * kSampleScale;
    }
}
