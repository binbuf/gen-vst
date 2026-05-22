#pragma once

#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>

#include "GenVstYmfmInterface.h"
#include "PatchSystem.h"
#include "ymfm_opn.h"

// Single-voice YM2612 (OPN2) render engine.
//
// Drives one ymfm chip instance with a loaded FM patch, renders at the
// chip's native rate (~53.27 kHz) and resamples to the host sample rate in a
// single pass (ADR-0011). Task 02 is monophonic; the shared 16-voice pool is
// Task 05.
class FmRenderEngine
{
public:
    FmRenderEngine() = default;

    // Allocates all working buffers and resets the chip. Call from prepareToPlay.
    void prepare (double hostSampleRate, int maxBlockSize);

    // Sets the patch used for subsequent note-ons. Call from the message
    // thread before audio starts; live patch swaps arrive in Task 05.
    void setPatch (const Patch& newPatch);

    // Keys the current patch on at the pitch of the given MIDI note.
    void noteOn (int midiNote);

    // Keys off, but only if midiNote is the note currently sounding.
    void noteOff (int midiNote);

    // Renders numSamples of host-rate audio into outL / outR.
    void render (float* outL, float* outR, int numSamples);

private:
    // Writes one Bank 0 register (this task drives only channel 0).
    void writeReg (uint8_t address, uint8_t data);

    GenVstYmfmInterface interface;
    ymfm::ym2612        chip { interface };

    double hostRate   = 44100.0;
    double nativeRate  = 53267.0;
    double speedRatio = 1.0;                 // nativeRate / hostRate

    juce::AudioBuffer<float> nativeBuffer;    // chip-rate render scratch (L/R)
    int nativeCapacity = 0;
    int carry          = 0;                   // native samples held over per block

    juce::LagrangeInterpolator resamplerL;
    juce::LagrangeInterpolator resamplerR;

    Patch currentPatch;          // drives note-on; replaced live in Task 05
    int   currentNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmRenderEngine)
};
