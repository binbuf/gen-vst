#include <array>
#include <string>

#include <gtest/gtest.h>

#include "FmRegisterMap.h"
#include "PatchSystem.h"

namespace
{
    // A patch with a distinct, easily-traced value in every field, so the
    // test verifies field packing AND that each operator lands at the right
    // register offset.
    Patch makeKnownPatch()
    {
        Patch p {};
        p.alg = 4;
        p.fb  = 3;
        p.lr  = 3;     // both L and R enabled
        p.ams = 1;
        p.pms = 2;

        // op0 = OP1/S1
        p.mul[0] = 1;  p.dt[0] = 0;  p.tl[0] = 0;   p.ks[0] = 0;  p.ar[0] = 31;
        p.dr[0]  = 0;  p.sr[0] = 0;  p.rr[0] = 15;  p.sl[0] = 0;  p.ssg[0] = 0;     p.amon[0] = 0;
        // op1 = OP2/S2
        p.mul[1] = 2;  p.dt[1] = 1;  p.tl[1] = 20;  p.ks[1] = 1;  p.ar[1] = 20;
        p.dr[1]  = 10; p.sr[1] = 5;  p.rr[1] = 8;   p.sl[1] = 4;  p.ssg[1] = 0;     p.amon[1] = 1;
        // op2 = OP3/S3
        p.mul[2] = 3;  p.dt[2] = 5;  p.tl[2] = 40;  p.ks[2] = 2;  p.ar[2] = 10;
        p.dr[2]  = 15; p.sr[2] = 20; p.rr[2] = 4;   p.sl[2] = 8;  p.ssg[2] = 0x0E;  p.amon[2] = 0;
        // op3 = OP4/S4
        p.mul[3] = 4;  p.dt[3] = 6;  p.tl[3] = 60;  p.ks[3] = 3;  p.ar[3] = 0;
        p.dr[3]  = 31; p.sr[3] = 31; p.rr[3] = 0;   p.sl[3] = 15; p.ssg[3] = 0;     p.amon[3] = 1;

        return p;
    }
}

// The note-on sequence for a known patch must match an exact register log:
// key-off, operator blocks in S1/S3/S2/S4 order, channel registers, frequency
// HIGH-then-LOW, key-on — with correctly packed values, incl. DT conversion
// (op2 dt=5 -> hardware 6, op3 dt=6 -> hardware 7).
TEST (RegisterWrite, NoteOnSequenceMatchesExpectedLog)
{
    const auto writes = FmRegisterMap::buildNoteOn (makeKnownPatch(), 69);  // A4

    // A4 -> BLK 4, F-number 0x43B: 0xA4 = (4<<3)|(0x43B>>8) = 0x24, 0xA0 = 0x3B.
    const std::array<RegWrite, FmRegisterMap::kNoteOnWriteCount> expected {{
        { 0x28, 0x00 },                                       // key-off

        { 0x30, 0x01 }, { 0x40, 0x00 }, { 0x50, 0x1F },       // S1 block, offset 0x00
        { 0x60, 0x00 }, { 0x70, 0x00 }, { 0x80, 0x0F }, { 0x90, 0x00 },

        { 0x34, 0x63 }, { 0x44, 0x28 }, { 0x54, 0x8A },       // S3 block, offset 0x04
        { 0x64, 0x0F }, { 0x74, 0x14 }, { 0x84, 0x84 }, { 0x94, 0x0E },

        { 0x38, 0x12 }, { 0x48, 0x14 }, { 0x58, 0x54 },       // S2 block, offset 0x08
        { 0x68, 0x8A }, { 0x78, 0x05 }, { 0x88, 0x48 }, { 0x98, 0x00 },

        { 0x3C, 0x74 }, { 0x4C, 0x3C }, { 0x5C, 0xC0 },       // S4 block, offset 0x0C
        { 0x6C, 0x9F }, { 0x7C, 0x1F }, { 0x8C, 0xF0 }, { 0x9C, 0x00 },

        { 0xB0, 0x1C }, { 0xB4, 0xD2 },                       // ALG/FB, L/R/AMS/PMS
        { 0xA4, 0x24 }, { 0xA0, 0x3B },                       // frequency HIGH then LOW
        { 0x28, 0xF0 },                                       // key-on
    }};

    for (int i = 0; i < FmRegisterMap::kNoteOnWriteCount; ++i)
    {
        SCOPED_TRACE ("write index " + std::to_string (i));
        EXPECT_EQ (static_cast<int> (writes[i].reg),   static_cast<int> (expected[i].reg));
        EXPECT_EQ (static_cast<int> (writes[i].value), static_cast<int> (expected[i].value));
    }
}

// TFI detune 0-3 pass straight through; 4-6 map to hardware 5-7, skipping the
// hardware's value 4 ("same as no detune").
TEST (RegisterWrite, DetuneTfiToHardwareConversion)
{
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (0)), 0);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (1)), 1);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (2)), 2);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (3)), 3);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (4)), 5);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (5)), 6);
    EXPECT_EQ (static_cast<int> (FmRegisterMap::detuneToRegister (6)), 7);
}

// The sequence is bracketed by a key-off (register 0x28 = 0x00) and a key-on
// (0x28 = 0xF0); buildKeyOff() emits the same key-off write.
TEST (RegisterWrite, SequenceIsBracketedByKeyOffAndKeyOn)
{
    const auto writes = FmRegisterMap::buildNoteOn (makeKnownPatch(), 60);
    EXPECT_EQ (static_cast<int> (writes.front().reg),   0x28);
    EXPECT_EQ (static_cast<int> (writes.front().value), 0x00);
    EXPECT_EQ (static_cast<int> (writes.back().reg),    0x28);
    EXPECT_EQ (static_cast<int> (writes.back().value),  0xF0);

    const RegWrite off = FmRegisterMap::buildKeyOff();
    EXPECT_EQ (static_cast<int> (off.reg),   0x28);
    EXPECT_EQ (static_cast<int> (off.value), 0x00);
}
