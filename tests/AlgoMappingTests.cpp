#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "FmRegisterMap.h"

// Algorithm carrier-mask correctness vs. the canonical YM2612 reference
// (docs/design/02-fm-synthesis.md "FM Algorithms" — cross-checked against
// plutiedev's YM2612 algorithm table). A carrier is an operator whose output
// feeds the channel mix bus; only carriers should receive velocity -> TL
// scaling (modulator TL controls timbre, not loudness).
//
// Carrier set per ALG:
//   ALG 0-3: S4 only
//   ALG 4:   S2, S4
//   ALG 5:   S2, S3, S4
//   ALG 6:   S2, S3, S4
//   ALG 7:   S1, S2, S3, S4   (fully additive)
//
// Encoded as a bitmask where bit i set => OP(i+1) is a carrier.
// The JS algorithm diagram (ui/src/widgets/algo-diagram.js) mirrors this
// table and is documented to stay in lockstep.

TEST (AlgoMapping, CarrierMaskMatchesYm2612Reference)
{
    constexpr std::array<uint8_t, 8> kExpected {
        0b1000, // alg 0 — S4 only
        0b1000, // alg 1 — S4 only
        0b1000, // alg 2 — S4 only
        0b1000, // alg 3 — S4 only
        0b1010, // alg 4 — S2 + S4
        0b1110, // alg 5 — S2 + S3 + S4
        0b1110, // alg 6 — S2 + S3 + S4
        0b1111, // alg 7 — all four
    };

    for (int alg = 0; alg < 8; ++alg)
    {
        SCOPED_TRACE ("alg " + std::to_string (alg));
        EXPECT_EQ (static_cast<int> (FmRegisterMap::kCarrierMaskByAlg[(std::size_t) alg]),
                   static_cast<int> (kExpected[(std::size_t) alg]));
    }
}

// Spot-check carrier-only velocity scaling against the carrier mask: a
// modulator operator's TL must pass through unchanged regardless of velocity,
// while a carrier's TL must be attenuated by (127 - vel) / 2.
TEST (AlgoMapping, VelocityScalingRespectsCarrierMask)
{
    // ALG 4: carriers are OP2 (bit 1) and OP4 (bit 3). OP1 + OP3 are modulators.
    constexpr int alg = 4;
    constexpr uint8_t patchTl  = 20;
    constexpr int     velocity = 64;
    constexpr uint8_t expectedCarrier = static_cast<uint8_t> (patchTl + (127 - velocity) / 2);

    for (int op = 0; op < 4; ++op)
    {
        SCOPED_TRACE ("op " + std::to_string (op));
        const bool isCarrier = ((FmRegisterMap::kCarrierMaskByAlg[(std::size_t) alg] >> op) & 1) == 1;
        const auto tl = FmRegisterMap::scaleCarrierTl (patchTl, alg, op, velocity, /*velToTl*/ true);
        if (isCarrier)
            EXPECT_EQ (static_cast<int> (tl), static_cast<int> (expectedCarrier));
        else
            EXPECT_EQ (static_cast<int> (tl), static_cast<int> (patchTl));
    }
}

// With velToTl disabled the patch TL is returned untouched for every operator.
TEST (AlgoMapping, VelocityScalingDisabledLeavesPatchTl)
{
    for (int alg = 0; alg < 8; ++alg)
        for (int op = 0; op < 4; ++op)
        {
            SCOPED_TRACE ("alg " + std::to_string (alg) + " op " + std::to_string (op));
            const auto tl = FmRegisterMap::scaleCarrierTl (40, alg, op, 1, /*velToTl*/ false);
            EXPECT_EQ (static_cast<int> (tl), 40);
        }
}
