#include "VoiceAllocator.h"

#include <cmath>
#include <cstring>

#include "DACPlayer.h"

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
    // 1. A free (Idle) voice, if any.
    for (auto& v : voices)
        if (v.isIdle())
            return v;

    // 2. Otherwise steal by global LRU, preferring release-phase voices: the
    //    oldest Released voice if one exists, else the oldest Active voice.
    Voice* oldestReleased = nullptr;
    Voice* oldestActive   = nullptr;

    for (auto& v : voices)
    {
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

void VoiceAllocator::noteOn (int part, int note, int velocity, double bendSemitones,
                              bool velToTl, const Patch& patch)
{
    Voice& v = allocateVoice();
    v.noteOn (part, note, velocity, bendSemitones, velToTl, patch, nextTimestamp++);
}

void VoiceAllocator::noteOff (int part, int note, bool sustainHeld)
{
    // Release the first Active voice serving this (part, note); a later
    // note-off for a retriggered note finds the next one. With the pedal
    // held, the voice keeps sounding but is flagged so releaseSustained()
    // can let it go on pedal-up.
    for (auto& v : voices)
    {
        if (v.isActive() && v.part() == part && v.note() == note)
        {
            if (sustainHeld)
                v.markSustained();
            else
                v.noteOff();
            return;
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

void VoiceAllocator::render (float* outL, float* outR, int numSamples, DACPlayer* dac)
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
            v.renderAdd (mixL + carry, mixR + carry, toGen);

    // The DAC is summed into the same native mix buffer as the FM voices so
    // a single resample pass handles both (ADR-0011, ADR-0014). The dedicated
    // 17th ymfm instance never consumes an FM voice slot.
    if (dac != nullptr)
        dac->renderAdd (mixL + carry, mixR + carry, toGen);

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
