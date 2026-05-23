#include "SN76489Wrapper.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// libvgm headers are pure C; wrap them in extern "C" so the linker resolves
// the un-mangled C symbols.
extern "C"
{
    #include "../third_party/libvgm/emu/EmuStructs.h"
    #include "../third_party/libvgm/emu/EmuCores.h"
    #include "../third_party/libvgm/emu/cores/sn764intf.h"
    #include "../third_party/libvgm/emu/cores/sn76496.h"
}

namespace
{
    // Sega VDP PSG natural sample rate = clock / 2 / clockDivider
    // = 3,579,545 / 2 / 8 = 223,721 Hz.
    constexpr int      kClockDivider = 8;
    constexpr double   kNativeRate   = static_cast<double> (SN76489Wrapper::kClockHz)
                                       / 2.0 / static_cast<double> (kClockDivider);

    // Output range: each channel contributes up to MAX_OUTPUT/4 = 8192, summed
    // bipolar across 4 channels and >>1, so libvgm produces ±~16384 INT32.
    // Scale to fit comfortably in [-1, +1] with headroom for the mix bus.
    constexpr float kSampleScale = 1.0f / 32768.0f;
}

struct SN76489Wrapper::State
{
    DEV_INFO            devInfo {};
    void*               chipPtr     = nullptr;
    DEVFUNC_WRITE_A8D8  writeFn     = nullptr;
    DEVFUNC_OPTMASK     muteMaskFn  = nullptr;
    SN76496_CFG         cfg {};
};

SN76489Wrapper::SN76489Wrapper()
    : state (std::make_unique<State>())
{
    // Sega VDP variant pin: feedback polynomial 0x0009, 16-bit LFSR, negate
    // output, /8 clock divider, treat freq=0 as a real frequency (segaPSG=1).
    // 03-psg-synthesis.md "Hardware Overview".
    auto& cfg = state->cfg;
    cfg._genCfg.emuCore   = FCC_MAME;
    cfg._genCfg.srMode    = DEVRI_SRMODE_NATIVE;
    cfg._genCfg.flags     = 0;
    cfg._genCfg.clock     = kClockHz;
    cfg._genCfg.smplRate  = 44100;     // placeholder; refined in prepare()
    cfg.noiseTaps         = 0x0009;
    cfg.shiftRegWidth     = 16;
    cfg.negate            = 1;
    cfg.clkDiv            = kClockDivider;
    cfg.ncrPSG            = 0;
    cfg.segaPSG           = 1;
    cfg.stereo            = 0;
    cfg.t6w28_tone        = nullptr;

    if (devDef_SN76496_MAME.Start (&cfg._genCfg, &state->devInfo) == 0x00)
        state->chipPtr = state->devInfo.dataPtr;

    // Resolve the write + mute-mask function pointers from the core's rwFuncs
    // table. The core publishes its register-write entry point through this
    // table rather than as a direct extern, so we look it up by funcType +
    // rwType (see sn76496.c devFunc[]).
    for (const auto* rw = devDef_SN76496_MAME.rwFuncs; rw->funcPtr != nullptr; ++rw)
    {
        if (rw->funcType == (RWF_REGISTER | RWF_WRITE) && rw->rwType == DEVRW_A8D8)
            state->writeFn = reinterpret_cast<DEVFUNC_WRITE_A8D8> (rw->funcPtr);
        else if (rw->funcType == (RWF_CHN_MUTE | RWF_WRITE) && rw->rwType == DEVRW_ALL)
            state->muteMaskFn = reinterpret_cast<DEVFUNC_OPTMASK> (rw->funcPtr);
    }

    if (state->chipPtr != nullptr)
        devDef_SN76496_MAME.Reset (state->chipPtr);

    nativeRate = kNativeRate;
    speedRatio = nativeRate / hostRate;
}

SN76489Wrapper::~SN76489Wrapper()
{
    if (state->chipPtr != nullptr)
        devDef_SN76496_MAME.Stop (state->chipPtr);
}

void SN76489Wrapper::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate   = hostSampleRate;
    nativeRate = kNativeRate;
    speedRatio = nativeRate / hostRate;

    // Worst-case native samples for one host block, plus headroom for the
    // fractional tail carried between blocks (same scheme as VoiceAllocator).
    const int worstCase = static_cast<int> (std::ceil (maxBlockSize * speedRatio)) + 16;
    nativeL.assign     (static_cast<std::size_t> (worstCase), 0);
    nativeR.assign     (static_cast<std::size_t> (worstCase), 0);
    nativeFloat.assign (static_cast<std::size_t> (worstCase), 0.0f);

    resampler.reset();
    carry = 0;
}

void SN76489Wrapper::reset()
{
    if (state->chipPtr != nullptr)
        devDef_SN76496_MAME.Reset (state->chipPtr);
    resampler.reset();
    carry = 0;
}

void SN76489Wrapper::setMuteMask (std::uint32_t mask)
{
    if (state->chipPtr != nullptr && state->muteMaskFn != nullptr)
        state->muteMaskFn (state->chipPtr, mask);
}

void SN76489Wrapper::write (std::uint8_t data)
{
    if (state->chipPtr != nullptr && state->writeFn != nullptr)
        state->writeFn (state->chipPtr, SN76496_W_REG, data);
}

void SN76489Wrapper::generate (float* monoOut, int numSamples)
{
    if (numSamples <= 0 || state->chipPtr == nullptr)
        return;

    const int needed = static_cast<int> (std::ceil (numSamples * speedRatio)) + 8;
    jassert (needed <= static_cast<int> (nativeL.size()));

    // Carry from last block is already at the front of nativeFloat. Fill the
    // remainder with freshly generated native samples.
    const int toGen = std::max (0, needed - carry);

    if (toGen > 0)
    {
        DEV_SMPL* outputs[2] = { nativeL.data(), nativeR.data() };
        devDef_SN76496_MAME.Update (state->chipPtr,
                                    static_cast<std::uint32_t> (toGen),
                                    outputs);

        // Non-stereo MAME core writes identical values to L and R; keep L.
        for (int i = 0; i < toGen; ++i)
            nativeFloat[static_cast<std::size_t> (carry + i)] =
                static_cast<float> (nativeL[static_cast<std::size_t> (i)]) * kSampleScale;
    }

    const int available = carry + toGen;

    const int used = resampler.process (speedRatio, nativeFloat.data(),
                                        monoOut, numSamples);
    jassert (used <= available);

    carry = available - used;
    if (carry > 0)
        std::memmove (nativeFloat.data(),
                      nativeFloat.data() + used,
                      static_cast<std::size_t> (carry) * sizeof (float));
}
