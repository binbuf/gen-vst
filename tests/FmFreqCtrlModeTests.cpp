#include <algorithm>
#include <gtest/gtest.h>

#include "FmRegisterMap.h"
#include "PatchSystem.h"

namespace
{
    // An audible patch with every operator set up to react clearly in any of
    // the three FREQ CTRL MODE register paths. ALG 7 makes all 4 operators
    // carriers — so the TL inversions are easy to verify on inspection — and
    // RR is non-zero so the AUTO_RETRIG auto-keyed events have audible release
    // (02-fm-synthesis.md *Register-write sequence for AUTO_RETRIG*).
    Patch makePatch()
    {
        Patch p {};
        p.alg = 7;
        p.lr  = 3;
        for (int op = 0; op < 4; ++op)
        {
            p.mul[op] = 1;
            p.ar[op]  = 31;
            p.tl[op]  = 0;     // attenuation 0 = loud
            p.rr[op]  = 15;    // fast release — required by AUTO_RETRIG
        }
        return p;
    }

    // Look up the value last written to `reg` in the ordered sequence, or
    // std::nullopt if the register doesn't appear. Used by tests to assert
    // "this register was set to this value" without pinning the entire
    // ordering (the ordering itself is asserted in the structural tests).
    template <typename Container>
    std::optional<int> lastValueWritten (const Container& writes, std::uint8_t reg)
    {
        std::optional<int> result;
        for (const auto& w : writes)
            if (w.reg == reg)
                result = w.value;
        return result;
    }

    template <typename Container>
    bool contains (const Container& writes, std::uint8_t reg, std::uint8_t value)
    {
        for (const auto& w : writes)
            if (w.reg == reg && w.value == value)
                return true;
        return false;
    }
}

// --- INT_MUL — unchanged v1 path ---------------------------------------------

TEST (FmFreqCtrlMode, IntMulIsBracketedByChannel0KeyOffAndKeyOn)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 0;   // INT_MUL — buildNoteOn() is the active path.

    const auto writes = FmRegisterMap::buildNoteOn (p, 69);  // A4

    // The legacy path opens with a ch0 key-off (0x28 = 0x00, channel select
    // value 0) and closes with a ch0 key-on (0x28 = 0xF0).
    ASSERT_GE (writes.size(), 2u);
    EXPECT_EQ (static_cast<int> (writes.front().reg),   0x28);
    EXPECT_EQ (static_cast<int> (writes.front().value), 0x00);
    EXPECT_EQ (static_cast<int> (writes.back().reg),    0x28);
    EXPECT_EQ (static_cast<int> (writes.back().value),  0xF0);
}

TEST (FmFreqCtrlMode, IntMulUsesChannel0FrequencyRegisters)
{
    Patch p = makePatch();
    const auto writes = FmRegisterMap::buildNoteOn (p, 69);

    // Standard ch0 frequency-write pair lives at 0xA4 / 0xA0 — exactly the
    // path the v1 RegisterWriteTests assert against. The ch3 special-mode
    // F-number registers (0xA8/0xA9/0xAA, 0xAC/0xAD/0xAE) must NOT appear.
    EXPECT_TRUE  (lastValueWritten (writes, 0xA4).has_value());
    EXPECT_TRUE  (lastValueWritten (writes, 0xA0).has_value());
    EXPECT_FALSE (lastValueWritten (writes, 0xA8).has_value());
    EXPECT_FALSE (lastValueWritten (writes, 0xAA).has_value());
    EXPECT_FALSE (lastValueWritten (writes, 0xAC).has_value());
}

// --- FLOAT_MUL — Channel 3 Special mode --------------------------------------

TEST (FmFreqCtrlMode, FloatMulSetsChannel3SpecialModeIn0x27)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 1;
    for (int op = 0; op < 4; ++op) p.mul_float[op] = 1.0f;

    const auto writes = FmRegisterMap::buildNoteOnFloatMul (p, 69);

    // 0x27 bits 7:6 = 01 (Special); timer bits cleared.
    const auto reg27 = lastValueWritten (writes, 0x27);
    ASSERT_TRUE (reg27.has_value());
    EXPECT_EQ ((*reg27) & 0xC0, 0x40)
        << "0x27 bits 7:6 should be 01 (Channel 3 Special), got 0x" << std::hex << *reg27;
    EXPECT_EQ ((*reg27) & 0x3F, 0x00)
        << "0x27 timer bits should be cleared in FLOAT_MUL (no auto-retrigger)";
}

TEST (FmFreqCtrlMode, FloatMulWritesPerOpChannel3FNumbers)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 1;
    for (int op = 0; op < 4; ++op) p.mul_float[op] = 1.0f;

    const auto writes = FmRegisterMap::buildNoteOnFloatMul (p, 69);

    // Each operator gets a HIGH + LOW F-number write at its ch3-special
    // register pair. Both must be present.
    for (int op = 0; op < 4; ++op)
    {
        const auto hi = lastValueWritten (writes,
                          FmRegisterMap::kChannel3OpFreqHigh[(std::size_t) op]);
        const auto lo = lastValueWritten (writes,
                          FmRegisterMap::kChannel3OpFreqLow [(std::size_t) op]);
        EXPECT_TRUE (hi.has_value()) << "missing HIGH F-num for op " << op;
        EXPECT_TRUE (lo.has_value()) << "missing LOW  F-num for op " << op;
    }
}

TEST (FmFreqCtrlMode, FloatMulKeyOnTargetsChannel3)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 1;

    const auto writes = FmRegisterMap::buildNoteOnFloatMul (p, 69);

    // Final 0x28 in the sequence: OPS=0xF0 | channel select = 2 (ch3) → 0xF2.
    ASSERT_FALSE (writes.empty());
    EXPECT_EQ (static_cast<int> (writes.back().reg),   0x28);
    EXPECT_EQ (static_cast<int> (writes.back().value), 0xF2);
}

TEST (FmFreqCtrlMode, FloatMulFixedFlagsUseAbsoluteHz)
{
    Patch p = makePatch();
    p.freq_ctrl_mode    = 1;
    p.fixed[0]          = true;
    p.freq_fixed_hz[0]  = 440.0f;       // A4
    p.mul_float[0]      = 2.0f;         // would otherwise be 2× the played note

    // Compare the OP1 F-numbers when fixed[0] = true (440 Hz) vs the same
    // patch played at a clearly different MIDI note. The F-numbers must NOT
    // change with the played note — fixed mode pins the operator's pitch.
    const auto writesAtC4 = FmRegisterMap::buildNoteOnFloatMul (p, 60);
    const auto writesAtC5 = FmRegisterMap::buildNoteOnFloatMul (p, 72);

    const auto hiC4 = lastValueWritten (writesAtC4, FmRegisterMap::kChannel3OpFreqHigh[0]);
    const auto loC4 = lastValueWritten (writesAtC4, FmRegisterMap::kChannel3OpFreqLow [0]);
    const auto hiC5 = lastValueWritten (writesAtC5, FmRegisterMap::kChannel3OpFreqHigh[0]);
    const auto loC5 = lastValueWritten (writesAtC5, FmRegisterMap::kChannel3OpFreqLow [0]);

    EXPECT_EQ (hiC4, hiC5) << "fixed op1 HIGH F-num must not depend on MIDI note";
    EXPECT_EQ (loC4, loC5) << "fixed op1 LOW  F-num must not depend on MIDI note";
}

// --- AUTO_RETRIG — Channel 3 CSM + TimerA ------------------------------------

TEST (FmFreqCtrlMode, AutoRetrigSetsCsmModeAndTimerLoadIn0x27)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 2;
    p.retrig_rate    = 498;             // RYM2612 reference value

    const auto writes = FmRegisterMap::buildNoteOnAutoRetrig (p, 69);

    // 0x27 appears twice: once with CSM mode + timer cleared (during the op
    // block) and once at the end with mode CSM | LOAD/EN/RST set (0x15). The
    // *last* 0x27 in the sequence is the timer-LOAD write.
    const auto lastReg27 = lastValueWritten (writes, 0x27);
    ASSERT_TRUE (lastReg27.has_value());
    EXPECT_EQ ((*lastReg27) & 0xC0, 0xC0) << "0x27 bits 7:6 should be 11 (CSM)";
    EXPECT_TRUE ((*lastReg27) & 0x01)    << "0x27 LOAD A (bit 0) should be set";
    EXPECT_TRUE ((*lastReg27) & 0x04)    << "0x27 EN A   (bit 2) should be set";
    EXPECT_TRUE ((*lastReg27) & 0x10)    << "0x27 RST A  (bit 4) should be set";
}

TEST (FmFreqCtrlMode, AutoRetrigWritesTimerAValueSplitAcross0x24And0x25)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 2;
    p.retrig_rate    = 498;             // 0x1F2 = 0b01_11110010

    const auto writes = FmRegisterMap::buildNoteOnAutoRetrig (p, 69);

    const auto reg24 = lastValueWritten (writes, 0x24);
    const auto reg25 = lastValueWritten (writes, 0x25);
    ASSERT_TRUE (reg24.has_value());
    ASSERT_TRUE (reg25.has_value());

    // 10-bit value split: 0x24 = (value >> 2) & 0xFF, 0x25 = value & 0x03.
    EXPECT_EQ (*reg24, (498 >> 2) & 0xFF);
    EXPECT_EQ (*reg25, 498 & 0x03);
}

TEST (FmFreqCtrlMode, AutoRetrigDoesNotIssueStandardKeyOn)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 2;

    const auto writes = FmRegisterMap::buildNoteOnAutoRetrig (p, 69);

    // The standard 0x28 key-on path is NOT used in CSM mode — TimerA fires
    // the auto-key internally. The sequence starts with a 0x28 ch3 key-off
    // to silence any previous note, but no 0x28 OPS=0xF0 write should appear.
    bool sawKeyOn = false;
    for (const auto& w : writes)
        if (w.reg == 0x28 && (w.value & 0xF0) == 0xF0)
            sawKeyOn = true;
    EXPECT_FALSE (sawKeyOn) << "AUTO_RETRIG must not emit a 0x28 key-on";
}

TEST (FmFreqCtrlMode, AutoRetrigWritesChannel3FrequencyRegisters)
{
    Patch p = makePatch();
    p.freq_ctrl_mode = 2;
    for (int op = 0; op < 4; ++op) p.mul_float[op] = 1.0f;

    const auto writes = FmRegisterMap::buildNoteOnAutoRetrig (p, 69);

    // Same per-op F-number register set as FLOAT_MUL — both use ch3.
    for (int op = 0; op < 4; ++op)
    {
        EXPECT_TRUE (lastValueWritten (writes,
            FmRegisterMap::kChannel3OpFreqHigh[(std::size_t) op]).has_value());
        EXPECT_TRUE (lastValueWritten (writes,
            FmRegisterMap::kChannel3OpFreqLow [(std::size_t) op]).has_value());
    }
}

// --- TimerA build helper -----------------------------------------------------

TEST (FmFreqCtrlMode, BuildTimerAClampsRangeAndSplitsBits)
{
    auto t = FmRegisterMap::buildTimerA (498);
    EXPECT_EQ (t.high, (498 >> 2) & 0xFF);
    EXPECT_EQ (t.low,  498 & 0x03);

    // Out-of-range clamps to 10-bit [0, 0x3FF].
    t = FmRegisterMap::buildTimerA (-10);
    EXPECT_EQ (t.high, 0);
    EXPECT_EQ (t.low,  0);

    t = FmRegisterMap::buildTimerA (5000);
    EXPECT_EQ (t.high, 0xFF);
    EXPECT_EQ (t.low,  0x03);
}

// --- 0x27 register composition -----------------------------------------------

TEST (FmFreqCtrlMode, ComposeRegister27CombinesModeAndTimerBits)
{
    EXPECT_EQ (FmRegisterMap::composeRegister27 (
                   FmRegisterMap::Channel3Mode::Normal, 0), 0x00);
    EXPECT_EQ (FmRegisterMap::composeRegister27 (
                   FmRegisterMap::Channel3Mode::Special, 0), 0x40);
    EXPECT_EQ (FmRegisterMap::composeRegister27 (
                   FmRegisterMap::Channel3Mode::Csm, 0), 0xC0);

    // Mode bits 7:6 + timer bits 5:0 = 0b11_010101 = 0xD5.
    EXPECT_EQ (FmRegisterMap::composeRegister27 (
                   FmRegisterMap::Channel3Mode::Csm, 0x15), 0xD5);
}

// --- Key-off paths ----------------------------------------------------------

TEST (FmFreqCtrlMode, KeyOffCh3UsesChannelSelectTwo)
{
    const RegWrite off = FmRegisterMap::buildKeyOffCh3();
    EXPECT_EQ (static_cast<int> (off.reg),   0x28);
    EXPECT_EQ (static_cast<int> (off.value), 0x02);   // OPS=0, channel select 2
}

TEST (FmFreqCtrlMode, KeyOffAutoRetrigClearsTimerLoadAndKeysOffCh3)
{
    const auto offs = FmRegisterMap::buildKeyOffAutoRetrig();

    // First write: 0x27 with CSM mode still selected but LOAD/EN/RST cleared,
    // so the auto-retrigger stops.
    EXPECT_EQ (offs[0].reg, 0x27);
    EXPECT_EQ (offs[0].value & 0xC0, 0xC0) << "CSM mode preserved";
    EXPECT_EQ (offs[0].value & 0x3F, 0x00) << "timer bits cleared";

    // Second write: standard ch3 key-off.
    EXPECT_EQ (offs[1].reg,   0x28);
    EXPECT_EQ (offs[1].value, 0x02);
}
