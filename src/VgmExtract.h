#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "PatchSystem.h"

// Bank import — the only patch path that returns multiple patches.
//
// Reads `.vgm` or `.vgz` (gzipped VGM) register-log files, walks the YM2612
// register-write stream, snapshots the targeted channel's shadow register
// state on every key-on event, dedupes by content hash, and returns one Patch
// per unique state. Names patches "<filename-stem> #<n>", with `n` starting
// at 1 in observation order.
//
// Scope of the parser (per `04-patch-system.md` *VGM Bank Import → Parser
// scope* and ADR-0019):
//   * `0x52 rr dd`   — YM2612 port 0 register write (FM channels 1-3 + global).
//   * `0x53 rr dd`   — YM2612 port 1 register write (FM channels 4-6).
//   * `0x61 nn nn`   — wait N samples; `0x62` 735; `0x63` 882; `0x70`-`0x7F`
//                      (n+1) samples. Sample timing isn't used for extraction,
//                      but the cursor must advance correctly.
//   * `0x66`         — end of sound data; stop.
//   * Any other byte is a command whose operand length is known from the VGM
//     spec; we skip its bytes correctly but do nothing (SN76489, DAC stream
//     control, other chips, reserved blocks).
//
// .vgz is decompressed in memory via `juce::GZIPDecompressorInputStream`. No
// new third-party dependency: the existing libvgm submodule (ADR-0009) stays
// scoped to SN76489 emulation and is NOT expanded to provide VGM parsing.
//
// Message thread only (CPU-bound on a multi-MB buffer); the caller in
// PluginEditor hops onto a juce::Thread before invoking this. On any parse
// error (wrong magic, unsupported version, missing YM2612, no key-ons found,
// truncated stream), returns an empty vector and sets `error` to a
// descriptive message; on success, `error` is cleared.
std::vector<Patch> extractFmPatches (const std::filesystem::path& vgmPath,
                                     std::string&                 error);
