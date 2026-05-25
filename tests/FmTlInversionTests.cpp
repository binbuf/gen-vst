#include <gtest/gtest.h>

#include "FmRegisterMap.h"

// `levelToAttenuation` is the apvts → register inversion at the heart of
// 02-fm-synthesis.md *UI level vs hardware attenuation*. The UI surface
// exposes a *level* (0 = silent, max = loudest); the YM2612's TL/SL registers
// store *attenuation* (0 = loudest, max = silent). The conversion is a flip
// around `maxAttenuation`:
//   attenuation = max - level
// with edge-clamping for out-of-range input.

// --- TL — 7-bit range, max 127 ------------------------------------------------

TEST (FmTlInversion, TlLevelZeroMapsToRegister127)
{
    // Level 0 (silent) -> register 127 (loudest attenuation = silence on TL).
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (0, 127), 127);
}

TEST (FmTlInversion, TlLevel127MapsToRegister0)
{
    // Level 127 (loudest) -> register 0 (no attenuation = full volume).
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (127, 127), 0);
}

TEST (FmTlInversion, TlLevel64MapsToRegister63)
{
    // Level 64 -> register 63 (127 - 64). Pin the rounding direction: the v2
    // task spec note "(or 64 — pin the rounding)" allows for either result,
    // and the canonical inverse is the integer `max - level`, which gives 63.
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (64, 127), 63);
}

TEST (FmTlInversion, TlIsSelfInverseOverFullRange)
{
    for (int level = 0; level <= 127; ++level)
    {
        const int attenuation = FmRegisterMap::levelToAttenuation (level, 127);
        const int roundTrip   = FmRegisterMap::levelToAttenuation (attenuation, 127);
        EXPECT_EQ (roundTrip, level) << "round-trip failed at level " << level;
    }
}

// --- SL — 4-bit range, max 15 -------------------------------------------------

TEST (FmTlInversion, SlLevelZeroMapsToRegister15)
{
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (0, 15), 15);
}

TEST (FmTlInversion, SlLevel15MapsToRegister0)
{
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (15, 15), 0);
}

TEST (FmTlInversion, SlLevel7MapsToRegister8)
{
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (7, 15), 8);
}

// --- Edge cases — out-of-range input clamps -----------------------------------

TEST (FmTlInversion, NegativeLevelClampsToMaxAttenuation)
{
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (-5,  127), 127);
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (-50, 15),  15);
}

TEST (FmTlInversion, OverMaxLevelClampsToZeroAttenuation)
{
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (200, 127), 0);
    EXPECT_EQ (FmRegisterMap::levelToAttenuation (100, 15),  0);
}
