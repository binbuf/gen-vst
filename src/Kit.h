#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "PatchSystem.h"

// `.gnkit` — v2 FM-mode drum-kit preset (04-patch-system.md *.gnkit Format*,
// ADR-0021 amendment, ADR-0025).
//
// A kit layers a note → FM-patch map onto FM mode so one MIDI track can play a
// whole drum kit from one instance (RYM2612/RX1200-style pads, host-sequenced).
// It is NOT a new engine and NOT a sequencer — every pad plays the ordinary
// YM2612 FM engine, just at a FIXED pitch with per-pad volume / decay.
//
// A kit holds up to kNumPads slots (4 banks × 8 pads). Each slot binds a trigger
// MIDI note to an EMBEDDED Patch (so a kit is self-contained even if the source
// `.tfi`/etc. moves). The factory kit may instead reference a `source` file
// path, which loadKit() resolves and embeds at load time.

struct KitSlot
{
    bool        enabled  = false;   // empty pad — not triggerable
    int         midiNote = -1;      // 0-127 trigger note (GM drum map default)
    Patch       patch;              // embedded resolved FM patch
    std::string sourcePath;         // origin path — UI label / re-pick only
    std::string label;              // "Kick", "Snare", ...

    float       volume   = 1.0f;    // 0.0-1.0 — folded into patch.channel_tl
    int         decayRr  = -1;      // -1 = keep patch RR; else 0-15 RR override
    int         fixedNote = 60;     // the constant pitch this pad always plays
};

struct Kit
{
    static constexpr int kNumBanks    = 4;
    static constexpr int kPadsPerBank = 8;
    static constexpr int kNumPads     = kNumBanks * kPadsPerBank;   // 32

    int                            version = 1;
    std::string                    name;
    std::array<KitSlot, kNumPads>  slots {};

    // Index of the enabled slot whose trigger note == `note`, or -1 if no
    // enabled slot maps that note. Lower pad index wins on a duplicate note.
    int slotForNote (int note) const noexcept;
};

struct KitLoadResult
{
    std::optional<Kit> kit;
    std::string        error;     // fatal — no kit produced
    std::string        warning;   // non-fatal (e.g. a slot whose source failed)
};

// Parse a `.gnkit` JSON file into a fully-embedded Kit. Message thread only
// (it may resolve `source` references through the on-disk patch loaders).
// Source paths are resolved relative to the `.gnkit` file's directory. A slot
// whose embedded patch is absent and whose source fails to load is left
// disabled and noted in `warning`; an unparseable file returns `error`.
KitLoadResult loadKit (const std::filesystem::path& path);

// Write `kit` to disk as `.gnkit` JSON, embedding every enabled slot's full
// Patch. Returns empty on success or a descriptive error string. Message
// thread only.
std::string saveKit (const Kit& kit, const std::filesystem::path& path);

// In-memory equivalents of loadKit / saveKit, used to embed the active kit in
// the plugin's project state (PluginState). kitToJson always embeds every
// enabled slot's full Patch. kitFromJson resolves `source` references relative
// to `baseDir` only when an embedded patch is absent — pass an empty baseDir
// (the default) for fully-embedded state JSON, where no resolution is needed.
std::string   kitToJson   (const Kit& kit);
KitLoadResult kitFromJson (const std::string& json,
                           const std::filesystem::path& baseDir = {});

// Load one FM patch file (`.tfi`/`.vgi`/`.dmp`/`.y12`/`.opm`) for embedding
// into a kit pad. `.psg` and unknown extensions are rejected (kits are
// FM-only). Message thread only. Used by the editor's pad-assignment path.
PatchLoadResult loadKitSourcePatch (const std::filesystem::path& path);

// Fold a slot's per-pad overrides into a copy of its patch, ready for
// VoiceAllocator::noteOn: `volume` scales `channel_tl`; `decayRr` (when >= 0)
// replaces every operator's release rate. Pure — no I/O — so the engine path
// and the unit tests share one definition.
Patch resolvedPadPatch (const KitSlot& slot);
