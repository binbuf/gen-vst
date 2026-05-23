# ADR-0019: Y12, OPM, and VGM bank import are in MVP scope

- **Status:** Accepted
- **Date:** 2026-05-23
- **Supersedes:** [ADR-0018](0018-additional-patch-formats.md)
- **Related:** [04-patch-system.md](../04-patch-system.md), [ADR-0004](0004-furnace-only-factory-bank.md), [ADR-0012](0012-dmp-version-scope.md), [docs/genny-ui.md](../../genny-ui.md)

## Context

ADR-0018 deferred Y12, OPM, and VGM extraction post-MVP on the reasoning that
all three are additive and the MVP corpus (TFI + VGI + DMP v11) is sufficient.
That reasoning still holds at the architectural level — these formats need no
`Patch` struct changes and no plumbing changes — but it understated the
product-level cost.

Gen VST's positioning statement is **Genny VST feature parity and beyond**.
Genny ships a single, prominent "Import Bank" entry point that accepts `.vgm`,
`.vgz`, `.tfi`, and a couple of bank formats; one click loads every patch from
the file into the user's import list, ready to play. This is the canonical way
Genny users acquire game-original instrument timbres without touching ROMs.
Shipping our MVP without it is a visible parity gap, not a missing power-user
feature.

The three formats from ADR-0018:

- **Y12** — 128-byte single-channel YM2612 register dump. Emitted by SMPS-style
  ROM-hacking tools when extracting patches from Mega Drive ROMs. User-supplied
  only; not shippable as factory content (ADR-0004 still applies).
- **OPM** — Yamaha YM2151 line-based ASCII instrument format. Large public
  patch library (VOPM and the YM2151 MML community). YM2151 ↔ YM2612 mismatch
  is bounded: the YM2151's `DT2` field has no YM2612 equivalent and is dropped;
  the YM2612's `SSG-EG` has no OPM source and defaults to off (0).
- **VGM bank import** — open a `.vgm` or `.vgz` (gzip-wrapped VGM) register-log
  file and emit one `Patch` per unique register state captured at each key-on
  event across the six FM channels.

## Decision

**All three land in MVP** as two new sequential tasks (20: Y12 + OPM loaders;
21: VGM bank import). ADR-0018 is **superseded** by this ADR.

The "Patch struct already captures the full YM2612 register set" reasoning from
ADR-0018 still applies — none of these formats add sound-shaping metadata. The
value is breadth of accessible patch corpus, not playback fidelity.

### UX: one-click Import Bank

VGM import matches Genny's UX exactly: a single "Import Bank" button → native
file picker → all extracted patches written to
`<userAppData>/GenVst/patches/imported/` and visible in the IMPORT list
immediately. **No second confirmation dialog. No per-patch checkbox/preview
modal.** Progress and errors surface via the existing WebView notification toast.

A multi-step extraction wizard (scrub-by-time, per-channel selection, preview
playback) was considered and rejected as friction the user has explicitly named
as the Genny advantage we should not lose.

### Parameter-mapping decisions

- **OPM `DT2`** — silently dropped. YM2612 has no DT2 register; warning would
  be noise on every OPM file. Documented in the loader's source comment so the
  decision is traceable.
- **OPM `SSG-EG`** — defaults to 0 (off). OPM has no SSG-EG field.
- **OPM `AMS-EN`** per operator → `Patch::amon[op]` directly.
- **Y12 byte layout** — verified against the TFM Music Maker reference at
  implementation time and recorded in `04-patch-system.md`'s Y12 section, the
  same pattern used for DMP v11 in ADR-0012.

### Parser dependency for VGM

Custom minimal parser under `src/VgmExtract/`. Handles VGM 1.50+ headers, the
YM2612 port-0/port-1 register writes (`0x52` / `0x53`), wait commands
(`0x61` / `0x62` / `0x63` / `0x70-0x7F`), end-of-stream (`0x66`), and `.vgz`
gzip via `juce::GZIPDecompressorInputStream`. Other chip writes (SN76489, PCM,
etc.) are skipped. ~250-350 LOC, all under our copyright.

The existing libvgm submodule (used only for the SN76489 emulation core,
ADR-0009) is intentionally **not** expanded to provide the VGM parser — the
submodule's narrow scope keeps the dependency surface small and the licensing
boundary clean.

## Consequences

- ADR-0018 status flips to `Superseded by ADR-0019`. Its body is unchanged so
  the deferral reasoning remains readable in the history.
- Two new task files: `docs/tasks/20-y12-opm-loaders.md` and
  `docs/tasks/21-vgm-bank-import.md`. The post-MVP backlog entry in
  `docs/tasks/README.md` that referenced ADR-0018 is removed.
- `docs/design/04-patch-system.md` grows Y12, OPM, and VGM Bank Import
  subsections (byte/parameter mapping for Y12 + OPM; flow + state-tracker
  description for VGM). File-picker filter widens from `*.tfi;*.vgi;*.dmp` to
  `*.tfi;*.vgi;*.dmp;*.y12;*.opm`. The "Import Bank" button is separately wired
  with `*.vgm;*.vgz`.
- `src/PatchSystem.h` gains `kY12FileSize`, `loadY12`, `loadOPM`, and a
  `kSupportedPatchExtensions` constant. `src/VgmExtract/` is a new module.
- The copyright constraint from ADR-0004 still applies: factory content is
  unchanged; Y12 / VGM extraction enables *users* to load their own files.

## Alternatives considered

- **Keep ADR-0018 as written and ship MVP without these formats** — the
  technical-debt argument is still correct (additive, no plumbing change). The
  product-level argument is what flipped: the explicit parity goal makes
  Genny's most-used import feature a release blocker rather than a nice-to-have.
- **Add Y12 + OPM only; defer VGM extraction to its own ADR** — rejected.
  VGM extraction is the Genny-parity feature; Y12 + OPM alone would not close
  the gap and we would re-litigate this decision in weeks.
- **Multi-step VGM extraction wizard** (scrub timeline, per-channel preview,
  pick-and-save) — rejected as the deliberate deviation from Genny UX it would
  represent. Recorded in [[reference-genny-vst-features]]: Genny's one-click
  flow is the bar; any future move to a wizard requires its own ADR.
