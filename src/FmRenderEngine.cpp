#include "FmRenderEngine.h"

#include <cmath>
#include <cstring>

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

void FmRenderEngine::noteOn (int midiNote)
{
    // --- Hard-coded patch (Task 04 replaces this with loaded Patch data) ------
    // Algorithm 7: S1+S2+S3+S4 are all carriers, fully additive — a clean,
    // always-audible organ-ish timbre. Indexed by operator number: 0=S1 .. 3=S4.
    struct Op { uint8_t dtMul, tl, ksAr, amDr, sr, slRr, ssgEg; };
    static constexpr Op patch[4] =
    {
        //  DT|MUL  TL     KS|AR  AM|DR  SR     SL|RR  SSG-EG
        { 0x01,   0x00,  0x1F,  0x00,  0x00,  0x0A,  0x00 },  // S1  MUL 1, full level
        { 0x02,   0x10,  0x1F,  0x00,  0x00,  0x0A,  0x00 },  // S2  MUL 2, -12 dB
        { 0x04,   0x18,  0x1F,  0x00,  0x00,  0x0A,  0x00 },  // S3  MUL 4, -18 dB
        { 0x01,   0x0A,  0x1F,  0x00,  0x00,  0x0A,  0x00 },  // S4  MUL 1, -7.5 dB
    };
    static constexpr uint8_t algFb    = 0x07;   // FB = 0, ALG = 7
    static constexpr uint8_t lrAmsPms = 0xC0;   // L + R enabled, AMS = 0, PMS = 0

    // Hardware operator slots are written in the order S1, S3, S2, S4 at
    // register offsets +0x00 / +0x04 / +0x08 / +0x0C.
    struct Slot { uint8_t offset; int op; };
    static constexpr Slot slots[4] =
    {
        { 0x00, 0 },   // S1
        { 0x04, 2 },   // S3
        { 0x08, 1 },   // S2
        { 0x0C, 3 },   // S4
    };

    // 1. Key-off first, so the envelope retriggers cleanly.
    writeReg (0x28, 0x00);

    // 2. Per-operator parameters, in hardware slot order.
    for (const auto& s : slots)
    {
        const auto& op = patch[s.op];
        writeReg (uint8_t (0x30 + s.offset), op.dtMul);
        writeReg (uint8_t (0x40 + s.offset), op.tl);
        writeReg (uint8_t (0x50 + s.offset), op.ksAr);
        writeReg (uint8_t (0x60 + s.offset), op.amDr);
        writeReg (uint8_t (0x70 + s.offset), op.sr);
        writeReg (uint8_t (0x80 + s.offset), op.slRr);
        writeReg (uint8_t (0x90 + s.offset), op.ssgEg);
    }

    // 3. Channel parameters.
    writeReg (0xB0, algFb);
    writeReg (0xB4, lrAmsPms);

    // 4. Frequency: pick BLK so the F-number fits 0x000-0x7FF; write HIGH first.
    const double noteHz = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
    int blk = 4;
    while (noteHz * (1 << (21 - blk)) / 53267.0 > 0x7FF && blk < 7) ++blk;
    while (noteHz * (1 << (21 - blk)) / 53267.0 < 0.0   && blk > 0) --blk;
    int fnum = juce::roundToInt (noteHz * (1 << (21 - blk)) / 53267.0);
    fnum = juce::jlimit (0, 0x7FF, fnum);

    writeReg (0xA4, uint8_t (((blk & 0x07) << 3) | ((fnum >> 8) & 0x07)));
    writeReg (0xA0, uint8_t (fnum & 0xFF));

    // 5. Key-on: all four operators, channel 0.
    writeReg (0x28, 0xF0);

    currentNote = midiNote;
}

void FmRenderEngine::noteOff (int midiNote)
{
    // Monophonic last-note behaviour: ignore note-offs for stale notes.
    if (midiNote == currentNote)
    {
        writeReg (0x28, 0x00);
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
