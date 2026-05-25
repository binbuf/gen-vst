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

namespace
{
    // Shared internal: convert a frequency in Hz to YM2612 (BLK, F-number)
    // using the NTSC native rate. midiNoteToFreq and hzToFreq both go through
    // this so the rounding and clamping behaviour is identical.
    FreqRegs hzToFreqInternal (double noteHz) noexcept
    {
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
} // namespace

FreqRegs midiNoteToFreq (double midiNote)
{
    return hzToFreqInternal (Tuning::instance().lookupHz (midiNote));
}

FreqRegs hzToFreq (double hz) noexcept
{
    return hzToFreqInternal (std::max (0.0, hz));
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
    //    TL is composed via composeTl so the FM panel's CH VOL knob
    //    (channel_tl) and per-op `vel[op]` velocity-depth knob both layer
    //    onto the patch TL on the register-write path — the patch struct
    //    stays as hardware attenuation, so a Patch without the v2 fields
    //    set (channel_tl = 1, vel[op] = 0) reproduces the v1 behaviour
    //    byte-for-byte.
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

        const uint8_t tl = composeTl (patch.tl[op], patch.channel_tl, patch.alg,
                                       op, params.velocity, params.velToTl,
                                       patch.vel[op]);

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

// --- v2 helpers (Task 05) ----------------------------------------------------

int levelToAttenuation (int level, int maxAttenuation) noexcept
{
    // Levels above max clamp at "loudest" (attenuation 0); negatives clamp to
    // silence (attenuation max). Standard inversion: attenuation = max - level.
    if (level <= 0)              return maxAttenuation;
    if (level >= maxAttenuation) return 0;
    return maxAttenuation - level;
}

std::uint8_t composeRegister27 (Channel3Mode mode, std::uint8_t timerBits) noexcept
{
    return static_cast<std::uint8_t> (
        (static_cast<std::uint8_t> (mode) & 0xC0) | (timerBits & 0x3F));
}

TimerAWrites buildTimerA (int retrigRate) noexcept
{
    const int v = std::clamp (retrigRate, 0, 0x3FF);
    return { static_cast<std::uint8_t> ((v >> 2) & 0xFF),
             static_cast<std::uint8_t> (v & 0x03) };
}

std::uint8_t composeTl (std::uint8_t patchTlAttenuation,
                        float        channelTl,
                        int          patchAlg,
                        int          opIndex,
                        int          velocity,
                        bool         velToTl,
                        float        velPerOp) noexcept
{
    // 1. Patch TL (already hardware-attenuation in Patch). Convert back to a
    //    "level" so we can scale by channel_tl, then back to attenuation.
    //    Working in level-space keeps the channel_tl multiplier intuitive
    //    (channel_tl = 0 -> silent, regardless of patch TL).
    const int   tlAttenuation = std::clamp (static_cast<int> (patchTlAttenuation), 0, 127);
    const float tlLevel       = static_cast<float> (127 - tlAttenuation);
    const float scaledLevel   = tlLevel * std::clamp (channelTl, 0.0f, 1.0f);
    int         attenuation   = 127 - static_cast<int> (std::lround (scaledLevel));

    // 2. v1 global velocity → carrier TL (legacy half-range formula).
    attenuation += static_cast<int> (scaleCarrierTl (0, patchAlg, opIndex,
                                                     velocity, velToTl));

    // 3. Per-op velocity → TL depth (RYM2612 manual page 10). Linear over
    //    velocity: at v=127 the per-op term contributes 0; at v=0 it
    //    contributes (127 × velPerOp).
    const int vClamped   = std::clamp (velocity, 0, 127);
    const float velFloor = std::clamp (velPerOp, 0.0f, 1.0f);
    const float perOpTerm = 127.0f * velFloor
                          * static_cast<float> (127 - vClamped) / 127.0f;
    attenuation += static_cast<int> (std::lround (perOpTerm));

    return static_cast<std::uint8_t> (std::clamp (attenuation, 0, 127));
}

namespace
{
    // Effective per-op pitch in Hz for FLOAT_MUL / AUTO_RETRIG. midiNote is the
    // played MIDI note (with bend folded in). `fixed[op] == true` plays the
    // operator at `freq_fixed_hz[op]` regardless of the played note; otherwise
    // the played-note Hz is multiplied by `mul_float[op]`.
    double effectiveOpHz (const Patch& patch, int op, double midiNote)
    {
        if (patch.fixed[op])
            return static_cast<double> (patch.freq_fixed_hz[op]);

        const double baseHz = Tuning::instance().lookupHz (midiNote);
        return baseHz * static_cast<double> (patch.mul_float[op]);
    }

    // Emit the operator-block registers for a Patch at a channel offset
    // (0 = ch1 / ch4 base, 2 = ch3 / ch6 base — we only use 0 for INT_MUL on
    // ch1, and 2 for ch3 in FLOAT_MUL / AUTO_RETRIG). Writes per-op DT/MUL,
    // TL (already composed externally — passed in via `composedTl`), KS/AR,
    // AMON/DR, SR, SL/RR, SSG-EG, in S1/S3/S2/S4 order.
    template <typename Emit>
    void emitOperatorBlock (const Patch&                     patch,
                            std::uint8_t                     channelOffset,
                            const std::array<std::uint8_t, 4>& composedTl,
                            Emit&&                           emit)
    {
        for (const int op : kOperatorWriteOrder)
        {
            const std::uint8_t off = static_cast<std::uint8_t> (
                kOperatorRegOffset[static_cast<std::size_t> (op)] + channelOffset);

            const std::uint8_t dtMul  = static_cast<std::uint8_t> (
                (detuneToRegister (patch.dt[op]) << 4) | (patch.mul[op] & 0x0F));
            const std::uint8_t ksAr   = static_cast<std::uint8_t> (
                ((patch.ks[op] & 0x03) << 6) | (patch.ar[op] & 0x1F));
            const std::uint8_t amonDr = static_cast<std::uint8_t> (
                ((patch.amon[op] & 0x01) << 7) | (patch.dr[op] & 0x1F));
            const std::uint8_t slRr   = static_cast<std::uint8_t> (
                ((patch.sl[op] & 0x0F) << 4) | (patch.rr[op] & 0x0F));

            emit (static_cast<std::uint8_t> (0x30 + off), dtMul);
            emit (static_cast<std::uint8_t> (0x40 + off),
                  composedTl[static_cast<std::size_t> (op)] & 0x7F);
            emit (static_cast<std::uint8_t> (0x50 + off), ksAr);
            emit (static_cast<std::uint8_t> (0x60 + off), amonDr);
            emit (static_cast<std::uint8_t> (0x70 + off),
                  static_cast<std::uint8_t> (patch.sr[op] & 0x1F));
            emit (static_cast<std::uint8_t> (0x80 + off), slRr);
            emit (static_cast<std::uint8_t> (0x90 + off),
                  static_cast<std::uint8_t> (patch.ssg[op] & 0x0F));
        }
    }

    // Common channel-3 note-on body: writes the 0x27 mode bits, the operator
    // block at channel offset +2, the channel registers (0xB2/0xB6), and the
    // per-op F-numbers from `effectiveOpHz`. Used by both FLOAT_MUL and
    // AUTO_RETRIG which differ only at the start (0x27 mode bits) and end
    // (TimerA + 0x28 vs key-off+key-on).
    std::vector<RegWrite> buildCh3OpAndChannelBlock (const Patch&     patch,
                                                     int              midiNote,
                                                     const NoteParams& params,
                                                     Channel3Mode     mode)
    {
        std::vector<RegWrite> writes;
        writes.reserve (50);
        const auto emit = [&writes] (std::uint8_t reg, std::uint8_t value)
        {
            writes.push_back ({ reg, value });
        };

        // 1. LFO control.
        emit (0x22, static_cast<std::uint8_t> (
            ((patch.lfo_enable & 0x01) << 3) | (patch.lfo_rate & 0x07)));

        // 2. 0x27: select ch3 mode with timer bits cleared. AUTO_RETRIG writes
        //    a follow-up 0x27 with LOAD / EN / RST set, after the F-numbers.
        emit (0x27, composeRegister27 (mode, 0));

        // 3. Compose per-op TLs (channel_tl × velocity layering).
        std::array<std::uint8_t, 4> composedTl {};
        for (int op = 0; op < 4; ++op)
            composedTl[static_cast<std::size_t> (op)] =
                composeTl (patch.tl[op], patch.channel_tl, patch.alg, op,
                           params.velocity, params.velToTl, patch.vel[op]);

        // 4. Operator block at channel offset +2.
        emitOperatorBlock (patch, /*channelOffset*/ 2, composedTl, emit);

        // 5. Channel-level registers — at offset +2 (ch3).
        emit (0xB2, static_cast<std::uint8_t> (
            ((patch.fb & 0x07) << 3) | (patch.alg & 0x07)));

        const int left  = (patch.lr >> 1) & 0x01;
        const int right = patch.lr & 0x01;
        emit (0xB6, static_cast<std::uint8_t> (
            (left << 7) | (right << 6) | ((patch.ams & 0x03) << 4) | (patch.pms & 0x07)));

        // 6. Per-operator F-numbers — HIGH then LOW so the chip latches both.
        //    The played-note pitch + bend is folded into the effective MIDI
        //    note; per-op pitch comes from mul_float / freq_fixed_hz.
        const double effectiveMidi = static_cast<double> (midiNote) + params.bendSemitones;
        for (int op = 0; op < 4; ++op)
        {
            const double hz = effectiveOpHz (patch, op, effectiveMidi);
            const FreqRegs f = hzToFreq (hz);
            const std::uint8_t high = static_cast<std::uint8_t> (
                ((f.blk & 0x07) << 3) | ((f.fnum >> 8) & 0x07));
            const std::uint8_t low  = static_cast<std::uint8_t> (f.fnum & 0xFF);

            emit (kChannel3OpFreqHigh[static_cast<std::size_t> (op)], high);
            emit (kChannel3OpFreqLow [static_cast<std::size_t> (op)], low);
        }

        return writes;
    }
} // namespace

std::vector<RegWrite> buildNoteOnFloatMul (const Patch& patch, int midiNote, NoteParams params)
{
    // 1. Key-off on ch3 (channel-select value 2). Re-running the envelope
    //    cleanly even when the previous note used a different mode.
    std::vector<RegWrite> writes;
    writes.reserve (50);
    writes.push_back ({ 0x28, 0x02 });

    // 2. Op + channel + per-op F-number block in ch3 Special mode.
    const std::vector<RegWrite> body =
        buildCh3OpAndChannelBlock (patch, midiNote, params, Channel3Mode::Special);
    writes.insert (writes.end(), body.begin(), body.end());

    // 3. Key-on ch3 — OPS=0xF0, channel-select=2.
    writes.push_back ({ 0x28, 0xF2 });
    return writes;
}

std::vector<RegWrite> buildNoteOnAutoRetrig (const Patch& patch, int midiNote, NoteParams params)
{
    // AUTO_RETRIG never uses the standard 0x28 key-on/off pair — the TimerA
    // overflow fires the auto-retrigger internally. Start by clearing 0x27 (no
    // LOAD/EN), then write the op + channel + F-numbers (which also writes
    // 0x27 with mode CSM, timer cleared), then TimerA, then 0x27 LOAD/EN/RST.
    std::vector<RegWrite> writes;
    writes.reserve (50);

    // Key-off ch3 first to silence any previous note before reconfiguring.
    writes.push_back ({ 0x28, 0x02 });

    // Op block writes 0x27 = CSM | timer cleared inside the body.
    const std::vector<RegWrite> body =
        buildCh3OpAndChannelBlock (patch, midiNote, params, Channel3Mode::Csm);
    writes.insert (writes.end(), body.begin(), body.end());

    // TimerA value high / low.
    const TimerAWrites timer = buildTimerA (static_cast<int> (patch.retrig_rate));
    writes.push_back ({ 0x24, timer.high });
    writes.push_back ({ 0x25, timer.low });

    // 0x27 with mode CSM AND timer LOAD/EN/RST bits set (bit4 = RST A,
    // bit2 = EN A, bit0 = LOAD A → 0x15). The TimerA fires repeatedly from
    // this point, auto-retriggering the ch3 operator envelopes.
    writes.push_back ({ 0x27, composeRegister27 (Channel3Mode::Csm, 0x15) });

    return writes;
}

std::array<RegWrite, 2> buildKeyOffAutoRetrig() noexcept
{
    // Clear TimerA LOAD so the auto-retrigger stops, plus a regular ch3 key-off
    // for cleanliness. The 0x27 write keeps mode CSM but drops LOAD/EN/RST.
    return { {
        { 0x27, composeRegister27 (Channel3Mode::Csm, 0) },
        { 0x28, 0x02 },
    } };
}

RegWrite buildKeyOffCh3() noexcept
{
    return { 0x28, 0x02 };
}

}  // namespace FmRegisterMap
