#include "VoiceAllocator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void VoiceAllocator::setVgmLogger (VgmLogger* logger) noexcept
{
    for (auto& v : voices)
        v.setVgmLogger (logger);
}

void VoiceAllocator::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate   = hostSampleRate;
    nativeRate = static_cast<double> (voices[0].nativeSampleRate());
    speedRatio = nativeRate / hostRate;

    // Worst-case native samples for one host block, plus headroom for the
    // fractional tail carried between blocks.
    nativeCapacity = static_cast<int> (std::ceil (maxBlockSize * speedRatio)) + 16;
    nativeMixBuffer.setSize (2, nativeCapacity, false, true, true);
    nativeMixBuffer.clear();

    resamplerL.reset();
    resamplerR.reset();
    carry = 0;

    for (auto& v : voices)
        v.reset();

    nextTimestamp = 0;
}

Voice& VoiceAllocator::allocateVoice()
{
    // The global voice-count cap restricts the candidate pool — only slots
    // [0, currentVoiceCount) are considered for new allocations. Voices still
    // sounding in slots above the cap keep ringing until natural release; new
    // notes simply never land there.
    const int cap = std::clamp (currentVoiceCount, 1, kNumVoices);

    // 1. A free (Idle) voice, if any.
    for (int i = 0; i < cap; ++i)
        if (voices[(std::size_t) i].isIdle())
            return voices[(std::size_t) i];

    // 2. Otherwise steal by global LRU, preferring release-phase voices: the
    //    oldest Released voice if one exists, else the oldest Active voice.
    Voice* oldestReleased = nullptr;
    Voice* oldestActive   = nullptr;

    for (int i = 0; i < cap; ++i)
    {
        auto& v = voices[(std::size_t) i];
        if (v.isReleasing())
        {
            if (oldestReleased == nullptr || v.timestamp() < oldestReleased->timestamp())
                oldestReleased = &v;
        }
        else // Active — Idle was ruled out by step 1.
        {
            if (oldestActive == nullptr || v.timestamp() < oldestActive->timestamp())
                oldestActive = &v;
        }
    }

    // With no Idle voices, the pool is all Active/Released, so at least one of
    // these is non-null; a Released steal is preferred when both exist.
    return oldestReleased != nullptr ? *oldestReleased : *oldestActive;
}

double VoiceAllocator::unisonVoiceDetuneSemitones (int voiceIndex, double spreadCents) noexcept
{
    // Symmetric fan-out per 07-feature-spec.md *Unison*:
    //   voice 0 -> 0, voice 1 -> +1*spread, voice 2 -> -1*spread,
    //   voice 3 -> +2*spread, voice 4 -> -2*spread, ...
    if (voiceIndex <= 0)
        return 0.0;
    const int    magnitude = (voiceIndex + 1) / 2;
    const double sign      = (voiceIndex % 2 == 1) ? 1.0 : -1.0;
    return sign * static_cast<double> (magnitude) * spreadCents / 100.0;
}

void VoiceAllocator::setPartMode (int part, const PartPolyMode& mode) noexcept
{
    if (part < 0 || part >= kNumParts) return;
    partModes[(std::size_t) part] = mode;
}

void VoiceAllocator::setVoiceCount (int count) noexcept
{
    currentVoiceCount = std::clamp (count, 1, kNumVoices);
}

VoiceAllocator::PartPolyMode VoiceAllocator::partMode (int part) const noexcept
{
    if (part < 0 || part >= kNumParts) return {};
    return partModes[(std::size_t) part];
}

void VoiceAllocator::noteOn (int part, int note, int velocity, double bendSemitones,
                              bool velToTl, const Patch& patch)
{
    const auto& mode = partModes[(std::size_t) std::clamp (part, 0, kNumParts - 1)];

    switch (mode.mode)
    {
        case PartPolyMode::Mode::Mono:
            noteOnMono (part, note, velocity, bendSemitones, velToTl, patch);
            return;

        case PartPolyMode::Mode::Unison:
            noteOnUnison (part, note, velocity, bendSemitones, velToTl, patch);
            return;

        case PartPolyMode::Mode::Poly:
        default:
            break;
    }

    Voice& v = allocateVoice();
    v.noteOn (part, note, velocity, bendSemitones, velToTl, patch, nextTimestamp++);
}

void VoiceAllocator::noteOnMono (int part, int note, int velocity, double bend,
                                  bool velToTl, const Patch& patch)
{
    // Find an existing sounding voice for this part (Active or Released —
    // Released voices in their tail can still be legato'd into for a smoother
    // transition, matching the "envelope continues" intent).
    Voice* existing = nullptr;
    for (auto& v : voices)
        if (! v.isIdle() && v.part() == part)
            existing = &v;

    if (existing == nullptr)
    {
        // First Mono note on this part — allocate fresh.
        Voice& v = allocateVoice();
        v.noteOn (part, note, velocity, bend, velToTl, patch, nextTimestamp++);
        return;
    }

    const auto& mode = partModes[(std::size_t) part];
    if (mode.monoLegato)
    {
        // Task 28 — translate the per-part glide time (ms) to native-rate
        // samples for the Voice. The voice walks its frequency register each
        // block at native rate, so the rate units must match.
        const double glideSamples = mode.glideTimeMs > 0.0
            ? mode.glideTimeMs * 0.001 * nativeRate
            : 0.0;
        existing->legatoTo (note, velocity, bend, velToTl, patch,
                            nextTimestamp++, glideSamples);
    }
    else
    {
        // Retrigger: re-noteOn reuses the same voice slot. buildNoteOn emits a
        // 0x28=0x00 key-off as its first write and a 0x28=0xF0 key-on as its
        // last, so the envelope fully re-attacks in a single write burst.
        existing->noteOn (part, note, velocity, bend, velToTl, patch, nextTimestamp++);
    }
}

void VoiceAllocator::noteOnUnison (int part, int note, int velocity, double bend,
                                    bool velToTl, const Patch& patch)
{
    const auto& mode = partModes[(std::size_t) part];
    const int   cap  = std::clamp (currentVoiceCount, 1, kNumVoices);

    // Allocate N voices (where N = the global voice count) for one unison
    // stack, each with its symmetric F-number detune offset. Stealing per the
    // usual LRU rules means a fresh unison stack may evict notes on other
    // parts — that's the cost of "thick stack" mode (07-feature-spec.md).
    for (int i = 0; i < cap; ++i)
    {
        Voice& v = allocateVoice();
        const double detune = unisonVoiceDetuneSemitones (i, mode.spreadCents);
        v.noteOn (part, note, velocity, bend, velToTl, patch, nextTimestamp++, detune);
    }
}

void VoiceAllocator::noteOff (int part, int note, bool sustainHeld)
{
    // Release every Active voice serving this (part, note). For Poly / Mono
    // this is usually exactly one voice; for Unison it releases the whole
    // detuned stack. Releasing everything also handles a mode-switch mid-note
    // (e.g. Unison stack + switch to Mono mid-hold) — no ghost voices left.
    // With the pedal held, voices are flagged sustained instead and let go
    // on pedal-up via releaseSustained.
    for (auto& v : voices)
    {
        if (v.isActive() && v.part() == part && v.note() == note)
        {
            if (sustainHeld)
                v.markSustained();
            else
                v.noteOff();
        }
    }
}

void VoiceAllocator::releaseSustained (int part)
{
    for (auto& v : voices)
        if (v.isActive() && v.part() == part && v.isSustained())
            v.noteOff();
}

void VoiceAllocator::setPitchBend (int part, double bendSemitones,
                                   const Patch& patch, bool velToTl)
{
    for (auto& v : voices)
        if (! v.isIdle() && v.part() == part)
            v.setPitchBend (bendSemitones, patch, velToTl);
}

void VoiceAllocator::allNotesOff()
{
    for (auto& v : voices)
        if (v.isActive())
            v.noteOff();
}

void VoiceAllocator::allSoundOff()
{
    for (auto& v : voices)
        v.reset();
}

void VoiceAllocator::updateActiveVoices (const std::array<Patch, kNumParts>& partPatches,
                                         bool velToTl)
{
    for (auto& v : voices)
        if (! v.isIdle())
            v.updateRegisters (partPatches[static_cast<std::size_t> (v.part())], velToTl);
}

void VoiceAllocator::updateActiveVoicesForPart (int part, const Patch& patch, bool velToTl)
{
    for (auto& v : voices)
        if (! v.isIdle() && v.part() == part)
            v.updateRegisters (patch, velToTl);
}

void VoiceAllocator::render (float* outL, float* outR, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int needed = static_cast<int> (std::ceil (numSamples * speedRatio)) + 8;
    jassert (needed <= nativeCapacity);

    float* mixL = nativeMixBuffer.getWritePointer (0);
    float* mixR = nativeMixBuffer.getWritePointer (1);

    // Generate just enough fresh native samples after the tail carried over
    // from the previous block. The fresh region is cleared first so voices can
    // accumulate into it; the carried region [0, carry) is last block's
    // unconsumed mix and is kept.
    const int toGen = juce::jmax (0, needed - carry);
    nativeMixBuffer.clear (0, carry, toGen);
    nativeMixBuffer.clear (1, carry, toGen);

    for (auto& v : voices)
        if (! v.isIdle())
        {
            v.advanceGlide (toGen);
            v.renderAdd (mixL + carry, mixR + carry, toGen);
        }

    const int available = carry + toGen;

    // Single-pass resample, native rate -> host rate. Both channels run the
    // same algorithm in lockstep, so they consume identical input counts.
    const int used  = resamplerL.process (speedRatio, mixL, outL, numSamples);
    const int usedR = resamplerR.process (speedRatio, mixR, outR, numSamples);
    jassert (used == usedR);
    juce::ignoreUnused (usedR);

    jassert (used <= available);

    // Keep the unconsumed native tail at the front of the mix buffer.
    carry = available - used;
    if (carry > 0)
    {
        std::memmove (mixL, mixL + used, static_cast<std::size_t> (carry) * sizeof (float));
        std::memmove (mixR, mixR + used, static_cast<std::size_t> (carry) * sizeof (float));
    }
}

int VoiceAllocator::numActiveVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isActive())
            ++n;
    return n;
}

int VoiceAllocator::numReleasingVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isReleasing())
            ++n;
    return n;
}

int VoiceAllocator::numIdleVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isIdle())
            ++n;
    return n;
}

bool VoiceAllocator::isNoteActive (int part, int note) const
{
    for (const auto& v : voices)
        if (v.isActive() && v.part() == part && v.note() == note)
            return true;
    return false;
}

bool VoiceAllocator::hasAudibleVoice() const noexcept
{
    for (const auto& v : voices)
        if (! v.isIdle())
            return true;
    return false;
}

std::uint32_t VoiceAllocator::activeVoiceMask() const noexcept
{
    std::uint32_t m = 0;
    for (int i = 0; i < kNumVoices; ++i)
        if (voices[(std::size_t) i].isActive())
            m |= (std::uint32_t {1} << i);
    return m;
}

bool VoiceAllocator::hasActiveVoiceUsingChannel3() const noexcept
{
    for (const auto& v : voices)
    {
        if (v.isIdle()) continue;
        const auto m = v.currentMode();
        if (m == Voice::FreqCtrlMode::FloatMul || m == Voice::FreqCtrlMode::AutoRetrig)
            return true;
    }
    return false;
}

std::uint16_t VoiceAllocator::fmPartSoundingMask() const noexcept
{
    std::uint16_t m = 0;
    for (const auto& v : voices)
    {
        if (v.isIdle()) continue;
        const int p = v.part();
        if (p >= 0 && p < kNumParts)
            m |= (std::uint16_t) (1u << p);
    }
    return m;
}
