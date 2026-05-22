#include "FmRenderEngine.h"

#include <cmath>
#include <cstring>

#include "FmRegisterMap.h"

void FmRenderEngine::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate   = hostSampleRate;
    nativeRate = (double) chip.sample_rate (7670454);   // NTSC OPN2 clock -> ~53267 Hz
    speedRatio = nativeRate / hostRate;

    // Worst-case native samples for one host block, plus headroom for the
    // fractional tail carried between blocks.
    nativeCapacity = (int) std::ceil (maxBlockSize * speedRatio) + 16;
    nativeBuffer.setSize (2, nativeCapacity, false, true, true);
    nativeBuffer.clear();

    chip.reset();
    resamplerL.reset();
    resamplerR.reset();

    carry       = 0;
    currentNote = -1;
}

void FmRenderEngine::writeReg (uint8_t address, uint8_t data)
{
    // Channel 0 lives in Bank 0: address port 0, data port 1.
    chip.write (0, address);
    chip.write (1, data);
}

void FmRenderEngine::setPatch (const Patch& newPatch)
{
    currentPatch = newPatch;
}

void FmRenderEngine::noteOn (int midiNote)
{
    // Full note-on register sequence for the current patch at this pitch.
    // Task 05 replaces this full rewrite with per-block dirty diffing.
    for (const auto& w : FmRegisterMap::buildNoteOn (currentPatch, midiNote))
        writeReg (w.reg, w.value);

    currentNote = midiNote;
}

void FmRenderEngine::noteOff (int midiNote)
{
    // Monophonic last-note behaviour: ignore note-offs for stale notes.
    if (midiNote == currentNote)
    {
        const RegWrite off = FmRegisterMap::buildKeyOff();
        writeReg (off.reg, off.value);
        currentNote = -1;
    }
}

void FmRenderEngine::render (float* outL, float* outR, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int needed = (int) std::ceil (numSamples * speedRatio) + 8;
    jassert (needed <= nativeCapacity);

    float* natL = nativeBuffer.getWritePointer (0);
    float* natR = nativeBuffer.getWritePointer (1);

    // Generate just enough native-rate samples, after the tail carried over
    // from the previous block, so the chip never runs ahead of consumption.
    const int toGen = juce::jmax (0, needed - carry);
    ymfm::ym2612::output_data sample;
    for (int i = 0; i < toGen; ++i)
    {
        chip.generate (&sample, 1);
        natL[carry + i] = (float) sample.data[0] * (1.0f / 32768.0f);
        natR[carry + i] = (float) sample.data[1] * (1.0f / 32768.0f);
    }

    const int available = carry + toGen;

    // Single-pass resample, native rate -> host rate. Both channels run the
    // same algorithm in lockstep, so they consume identical input counts.
    const int used  = resamplerL.process (speedRatio, natL, outL, numSamples);
    const int usedR = resamplerR.process (speedRatio, natR, outR, numSamples);
    jassert (used == usedR);
    juce::ignoreUnused (usedR);

    jassert (used <= available);

    // Keep the unconsumed native tail at the front of the buffer for next block.
    carry = available - used;
    if (carry > 0)
    {
        std::memmove (natL, natL + used, (size_t) carry * sizeof (float));
        std::memmove (natR, natR + used, (size_t) carry * sizeof (float));
    }
}
