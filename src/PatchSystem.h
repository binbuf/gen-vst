#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// All YM2612 parameters for ONE FM part, stored as plain integers matching the
// hardware register ranges (04-patch-system.md "Patch Data Model"). The final
// processor holds one Patch per part; Task 04 drives a single part.
struct Patch
{
    // Channel-level parameters.
    uint8_t alg = 0;          // 0-7: algorithm
    uint8_t fb  = 0;          // 0-7: S1 self-feedback
    uint8_t lr  = 3;          // bit1 = L, bit0 = R. Defaults to both enabled:
                              // TFI carries no L/R, and 0 here would be silent.
    uint8_t ams = 0;          // 0-3: amplitude mod sensitivity
    uint8_t pms = 0;          // 0-7: phase mod sensitivity
    uint8_t lfo_enable = 0;   // 0/1: LFO on
    uint8_t lfo_rate   = 0;   // 0-7: LFO frequency select

    // Per-operator parameters. Index 0 = OP1/S1, 1 = OP2/S2, 2 = OP3/S3,
    // 3 = OP4/S4 — the operator's number, not its register order.
    uint8_t mul[4]  {};       // 0-15:  frequency multiple
    uint8_t dt[4]   {};       // 0-6:   detune (TFI encoding; converted in FmRegisterMap)
    uint8_t tl[4]   {};       // 0-127: total level
    uint8_t ks[4]   {};       // 0-3:   key scale
    uint8_t ar[4]   {};       // 0-31:  attack rate
    uint8_t dr[4]   {};       // 0-31:  first decay rate
    uint8_t sr[4]   {};       // 0-31:  second decay / sustain rate
    uint8_t rr[4]   {};       // 0-15:  release rate
    uint8_t sl[4]   {};       // 0-15:  sustain level
    uint8_t ssg[4]  {};       // 0 or 8-15: SSG-EG (values 1-7 are invalid)
    uint8_t amon[4] {};       // 0/1:   amplitude mod enable per operator

    std::string name;         // display name (filename stem)
};

// TFI files are a fixed 42 bytes with no header or magic number.
inline constexpr std::size_t kTfiFileSize = 42;

// VGI files are a fixed 43 bytes — TFI plus one AMS/FMS byte at offset 0x02.
inline constexpr std::size_t kVgiFileSize = 43;

// DMP version 11 Genesis FM files are 51 bytes: a 7-byte header (version,
// system, mode, FMS, FB, ALG, AMS) followed by 4 operators × 11 bytes each.
// Verified against the Furnace reference loader DivEngine::loadDMP in
// third_party/furnace/src/engine/fileOpsIns.cpp (the design doc's reference
// to src/format/dmp.cpp is the historical path).
inline constexpr std::size_t kDmpV11FmFileSize = 51;

// Result of a patch load. C++20, so no std::expected — a std::optional patch
// plus an error string. On success `patch` holds the data and `error` is
// empty; on failure `patch` is empty and `error` describes the problem for the
// UI notification toast.
struct PatchLoadResult
{
    std::optional<Patch> patch;
    std::string          error;
};

// Parse a 42-byte TFI file into a Patch. Runs on the message thread only.
// Every value is clamped to its valid hardware range, so a corrupt or
// wrong-size file fails gracefully instead of producing junk register writes.
PatchLoadResult loadTFI (const std::filesystem::path& path);

// Parse a 43-byte VGI file into a Patch. Message thread only. AMS/FMS are
// unpacked from byte 0x02 (`0b00AA0FFF`); AMON is unpacked from each
// operator's DR byte bit 7. Values are clamped to hardware ranges.
PatchLoadResult loadVGI (const std::filesystem::path& path);

// Parse a DMP file into a Patch. Only DMP version 11 (0x0B) with system 0x02
// (Genesis) or 0x42 (Genesis extended) and an FM-type instrument is accepted;
// every other version, system byte, or PSG-type instrument is rejected with
// a descriptive `error` and no patch (ADR-0012). Message thread only.
PatchLoadResult loadDMP (const std::filesystem::path& path);

// Write a Patch to disk as a 42-byte TFI file. Returns an empty string on
// success or a descriptive error message on failure. TFI carries no
// AMS/FMS/AMON/LFO data, so those fields are silently dropped.
std::string exportTFI (const Patch& patch, const std::filesystem::path& path);

// Write a Patch to disk as a 43-byte VGI file, packing AMS/FMS into byte
// 0x02 and AMON into each operator's DR byte (bit 7). Returns an empty
// string on success or a descriptive error message on failure.
std::string exportVGI (const Patch& patch, const std::filesystem::path& path);
