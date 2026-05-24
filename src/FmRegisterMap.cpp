#include "FmRegisterMap.h"

#include <algorithm>
#include <cmath>

#include "Tuning.h"

namespace
{
    // YM2612 native sample rate for the NTSC clock (7670454 / 144). The
    // F-number formula is defined against this constant (02-fm-synthesis.md).
    constexpr double kNativeRate = 53267.0;

    constexpr int kMaxFnum = 0x7FF;   // 11-bit F-number
    constexpr int kMaxBlk  = 7;       // 3-bit block
}

namespace FmRegisterMap
{

FreqRegs midiNoteToFreq (double midiNote)
{
    const double noteHz = Tuning::instance().lookupHz (midiNote);

    // fnum(blk) = noteHz * 2^(21-blk) / nativeRate. Start at block 4 and raise
    // the block — halving the F-number each step — until it fits 0x000-0x7FF.
    const auto fnumAt = [noteHz] (int blk)
    {
        return noteHz * static_cast<double> (1 << (21 - blk)) / kNativeRate;
    };

    int blk = 4;
    while (fnumAt (blk) > kMaxFnum && blk < kMaxBlk)
        ++blk;

    int fnum = static_cast<int> (std::lround (fnumAt (blk)));
    fnum = std::clamp (fnum, 0, kMaxFnum);

    return { blk, fnum };
}

uint8_t detuneToRegister (uint8_t tfiDetune)
{
    // TFI 0-3 pass straight through. TFI 4-6 (negative detune) shift up by one
    // to skip hardware value 4, which the YM2612 treats as "no detune".
    return static_cast<uint8_t> (tfiDetune < 4 ? tfiDetune : tfiDetune + 1);
}

uint8_t scaleCarrierTl (uint8_t patchTl, int patchAlg, int opIndex,
                        int velocity, bool velToTl) noexcept
{
    if (! velToTl)
        return patchTl;

    const uint8_t carrierMask = kCarrierMaskByAlg[static_cast<std::size_t> (patchAlg & 0x07)];
    if (((carrierMask >> opIndex) & 0x01) == 0)
        return patchTl;   // modulator — TL controls timbre, leave it untouched

    // Half-range linear attenuation: v=127 -> no change, v=0 -> +63 TL
    // (~47 dB drop). A full-range offset (127 - v) is too dramatic for typical
    // playing; this keeps the dynamic envelope musically usable.
    const int offset    = (127 - std::clamp (velocity, 0, 127)) / 2;
    const int effective = std::clamp (static_cast<int> (patchTl) + offset, 0, 127);
    return static_cast<uint8_t> (effective);
}

std::array<RegWrite, kNoteOnWriteCount> buildNoteOn (const Patch& patch,
                                                     int midiNote,
                                                     NoteParams params)
{
    std::array<RegWrite, kNoteOnWriteCount> writes {};
    int n = 0;
    const auto emit = [&writes, &n] (uint8_t reg, int value)
    {
        writes[static_cast<std::size_t> (n++)] = { reg, static_cast<uint8_t> (value & 0xFF) };
    };

    // 1. Key-off first, so the envelope retriggers cleanly (channel 0, OPS=0).
    emit (0x28, 0x00);

    // 1b. LFO control (0x22): bit 3 = enable, bits 2:0 = rate. Each voice is its
    // own ymfm instance (ADR-0010), so the chip's single global LFO is in effect
    // per-voice and is set here from the part's patch.
    emit (0x22, ((patch.lfo_enable & 0x01) << 3) | (patch.lfo_rate & 0x07));

    // 2. Per-operator parameters, written in hardware order S1, S3, S2, S4.
    for (const int op : kOperatorWriteOrder)
    {
        const uint8_t off = kOperatorRegOffset[static_cast<std::size_t> (op)];

        const uint8_t dtMul  = static_cast<uint8_t> ((detuneToRegister (patch.dt[op]) << 4)
                                                       | (patch.mul[op] & 0x0F));
        const uint8_t ksAr   = static_cast<uint8_t> (((patch.ks[op] & 0x03) << 6)
                                                       | (patch.ar[op] & 0x1F));
        const uint8_t amonDr = static_cast<uint8_t> (((patch.amon[op] & 0x01) << 7)
                                                       | (patch.dr[op] & 0x1F));
        const uint8_t slRr   = static_cast<uint8_t> (((patch.sl[op] & 0x0F) << 4)
                                                       | (patch.rr[op] & 0x0F));

        const uint8_t tl = scaleCarrierTl (patch.tl[op], patch.alg, op,
                                           params.velocity, params.velToTl);

        emit (static_cast<uint8_t> (0x30 + off), dtMul);
        emit (static_cast<uint8_t> (0x40 + off), tl & 0x7F);
        emit (static_cast<uint8_t> (0x50 + off), ksAr);
        emit (static_cast<uint8_t> (0x60 + off), amonDr);
        emit (static_cast<uint8_t> (0x70 + off), patch.sr[op] & 0x1F);
        emit (static_cast<uint8_t> (0x80 + off), slRr);
        emit (static_cast<uint8_t> (0x90 + off), patch.ssg[op] & 0x0F);
    }

    // 3. Channel parameters: ALG/FB, then L/R/AMS/PMS.
    emit (0xB0, ((patch.fb & 0x07) << 3) | (patch.alg & 0x07));

    const int left  = (patch.lr >> 1) & 0x01;
    const int right = patch.lr & 0x01;
    emit (0xB4, (left << 7) | (right << 6) | ((patch.ams & 0x03) << 4) | (patch.pms & 0x07));

    // 4. Frequency: HIGH byte (BLK + F-number high bits) before LOW byte.
    const FreqRegs f = midiNoteToFreq (static_cast<double> (midiNote) + params.bendSemitones);
    emit (0xA4, ((f.blk & 0x07) << 3) | ((f.fnum >> 8) & 0x07));
    emit (0xA0, f.fnum & 0xFF);

    // 5. Key-on: all four operators, channel 0.
    emit (0x28, 0xF0);

    return writes;
}

RegWrite buildKeyOff()
{
    return { 0x28, 0x00 };
}

}  // namespace FmRegisterMap
