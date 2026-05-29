#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// All YM2612 parameters for ONE FM part, stored as plain integers matching the
// hardware register ranges (04-patch-system.md "Patch Data Model"). The final
// processor holds one Patch per part; Task 04 drives a single part.
//
// TL / SL semantics. The fields below carry **hardware attenuation** (0 =
// loudest, max = silent) — the on-disk TFI/VGI/DMP/Y12/OPM round-trip target.
// The v2 UI / apvts surface exposes the *inverted* level (0 = silent, max =
// loudest) per 02-fm-synthesis.md § *UI level vs hardware attenuation*; the
// apvts ↔ Patch boundary (FmParamCache::readPatch and the patch-browser drain
// handler in PluginProcessor.cpp) does the flip via
// FmRegisterMap::levelToAttenuation.
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
    uint8_t tl[4]   {};       // 0-127: total level (attenuation; see header note)
    uint8_t ks[4]   {};       // 0-3:   key scale
    uint8_t ar[4]   {};       // 0-31:  attack rate
    uint8_t dr[4]   {};       // 0-31:  first decay rate
    uint8_t sr[4]   {};       // 0-31:  second decay / sustain rate
    uint8_t rr[4]   {};       // 0-15:  release rate
    uint8_t sl[4]   {};       // 0-15:  sustain level (attenuation; see header note)
    uint8_t ssg[4]  {};       // 0 or 8-15: SSG-EG (values 1-7 are invalid)
    uint8_t amon[4] {};       // 0/1:   amplitude mod enable per operator

    // --- v2 additions (02-fm-synthesis.md *FREQ Control Mode*, RYM2612-style
    //                   *Velocity → TL layering*, *Channel TL*, *DAC Prescaler*)
    //
    // These do not round-trip through legacy TFI/VGI/DMP/Y12/OPM formats; the
    // loaders default them so a legacy patch behaves identically to v1. The
    // v2-native .gnpat format (Task 09) will persist them explicitly.

    // 0 = INT_MUL (channel 0, shared F-number, MUL field per op) — the v1 path,
    // default. 1 = FLOAT_MUL (channel 3 special, per-op F-numbers from
    // note × mul_float[op] or freq_fixed_hz[op]). 2 = AUTO_RETRIG (channel 3
    // CSM + TimerA per retrig_rate).
    uint8_t  freq_ctrl_mode = 0;

    // 10-bit TimerA value, written split across 0x24 / 0x25 in AUTO_RETRIG.
    uint16_t retrig_rate    = 500;

    float mul_float[4]      { 1.0f, 1.0f, 1.0f, 1.0f }; // FLOAT_MUL multiplier
    bool  fixed[4]          {};                          // per-op fixed-Hz toggle
    float freq_fixed_hz[4]  { 440.0f, 440.0f, 440.0f, 440.0f };

    // Per-op velocity → TL depth (0 = no effect, 1 = full). Stacks additively
    // with the global `velocity_to_tl` toggle (02-fm-synthesis.md § *Velocity
    // → TL layering*).
    float vel[4]            {};

    // UI master TL multiplier folded into every op's TL register write.
    // 1.0 = patch played verbatim; 0.0 = silent.
    float channel_tl        = 1.0f;

    // 0..1 sweep of the YM2612 DAC clock prescaler (DspDecimator hold count).
    // 0 = bypass; non-zero engages decimation on the summed FM bus (the
    // per-voice ladder runs upstream inside ymfm — see Voice::renderAdd).
    float fm_dac_prescaler  = 0.0f;

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

// Y12 files are a fixed 128-byte single-channel YM2612 register dump emitted by
// SMPS-style ROM-hacking tools (Gens KMod and TFM Music Maker). Per-operator
// byte ordering verified against Furnace's DivEngine::loadY12; see the source
// comment on loadY12 for the offset table.
inline constexpr std::size_t kY12FileSize = 128;

// File extensions the patch system imports. Used by the patch browser, the
// import file chooser, and the drag-and-drop handler — one source of truth so
// the supported-format list stays consistent across UI surfaces.
//
// Extensions are stored lower-case with the leading dot. Callers comparing
// against them must lower-case their own input first.
inline constexpr std::array<std::string_view, 6> kSupportedPatchExtensions {
    ".tfi", ".vgi", ".dmp", ".y12", ".opm", ".psg"
};

// True if `ext` (a file extension like ".tfi", case-insensitive, must include
// the leading dot) is in kSupportedPatchExtensions. The drag-and-drop handler
// and the patch browser's extension check both go through this so the set
// stays in one place.
bool isSupportedPatchExtension (std::string_view ext);

// Build the juce::FileChooser filter literal from kSupportedPatchExtensions —
// e.g. "*.tfi;*.vgi;*.dmp;*.y12;*.opm;*.psg". Returned as std::string so this
// header stays JUCE-free; the caller wraps it in juce::String.
// PluginEditor.cpp's import file chooser uses this so widening the
// supported-format list is a one-line change to kSupportedPatchExtensions.
std::string buildPatchExtensionFilter();

// The mode a preset belongs to. The browser uses Tag to filter the visible
// patches and to auto-switch the instance's mode when a patch loads
// (ADR-0025). D mode has no preset format, so it never appears as a Tag.
// `Pending` is the placeholder emitted by the folder-scan for `.dmp` files;
// resolved lazily on folder-expand or load attempt via tagFromFile
// (ADR-0026). Task 09 treats every `.dmp` as FM at scan and load time; the
// full content-peek arrives in Task 10.
enum class Tag : std::uint8_t { FM, SQ, Pending };

// Extension-only tag derivation. Used by the fast folder-scan path (no file
// I/O). `ext` is case-insensitive and must include the leading dot. Returns
// `Tag::Pending` for `.dmp` (the tag depends on byte 2 — ADR-0026), or
// `Tag::FM` / `Tag::SQ` for every other supported extension, or std::nullopt
// for an unrecognised extension.
std::optional<Tag> tagFromExtension (std::string_view ext);

// File-aware tag derivation. Used by the file picker, drag-and-drop handler
// and load path. For `.dmp` files this peeks byte 2 to choose between
// `Tag::FM` (mode 1) and `Tag::SQ` (mode 0) per ADR-0026; on read failure or
// for any other byte-2 value the function returns `Tag::FM` so the FM loader
// surfaces a descriptive error (matching the existing FM-only rejection
// behaviour for malformed DMPs). For all other extensions delegates to
// `tagFromExtension` (its `Pending` is never returned from here). Returns
// std::nullopt for an unrecognised extension.
std::optional<Tag> tagFromFile (const std::filesystem::path& path);

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

// Parse a 128-byte Y12 file into a Patch. Message thread only. Y12 carries no
// L/R enables, so `lr` defaults to 3 (both enabled) — matching loadTFI/loadVGI.
// Per-operator bytes are hardware-register-encoded; the DT field is converted
// from YM2612 register encoding (0-7) to the patch model's TFI 0-6 encoding.
PatchLoadResult loadY12 (const std::filesystem::path& path);

// Parse a YM2151 OPM/VOPM text instrument file into a Patch. Message thread
// only. Multi-instrument OPM files load the first `@:` block only; subsequent
// blocks are ignored (multi-instrument bank import is post-MVP). YM2151's DT2
// has no YM2612 equivalent and is silently dropped; SSG-EG defaults to 0 (off)
// because OPM has no SSG-EG field. LFO enable is derived from whether any of
// LFRQ/AMD/PMD is non-zero. Missing required lines (LFO/CH/M1/C1/M2/C2) or
// non-integer / too-few-token operator lines are load errors.
PatchLoadResult loadOPM (const std::filesystem::path& path);

// Write a Patch to disk as a 42-byte TFI file. Returns an empty string on
// success or a descriptive error message on failure. TFI carries no
// AMS/FMS/AMON/LFO data, so those fields are silently dropped.
std::string exportTFI (const Patch& patch, const std::filesystem::path& path);

// Write a Patch to disk as a 43-byte VGI file, packing AMS/FMS into byte
// 0x02 and AMON into each operator's DR byte (bit 7). Returns an empty
// string on success or a descriptive error message on failure.
std::string exportVGI (const Patch& patch, const std::filesystem::path& path);
