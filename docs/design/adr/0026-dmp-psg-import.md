# ADR-0026: DMP PSG instrument import for SQ mode

- **Status:** Accepted
- **Date:** 2026-05-24
- **Related:** [ADR-0025](0025-tagged-preset-browser.md), [ADR-0012](0012-dmp-version-scope.md), `docs/design/04-patch-system.md`

## Context

[ADR-0025](0025-tagged-preset-browser.md) established "tag = file extension"
as the tagging rule for the preset browser. This works cleanly for all
formats except `.dmp`: a DefleMask Preset file is identified at byte 2 as
either FM (mode = 1) or STD/PSG (mode = 0). The extension alone is
ambiguous.

[ADR-0012](0012-dmp-version-scope.md) scoped the DMP loader to version 11,
FM mode only. PSG DMP files were explicitly rejected. That was the right
call for v1 because Gen VST had no SQ mode; it is revisited here for v2.

The SN76489 hardware has **no envelope generator**. Every driver on the
hardware implements envelopes in software (SMPS, Echo, etc.) using entirely
proprietary formats. As a result there is no community-standard SN76489
"patch file" format the way `.tfi`/`.vgi` are for FM.

The DefleMask Preset format (DMP, mode 0) is the closest thing to a
community SQ patch format: DefleMask supports the Sega Master System /
Game Gear / Genesis PSG and has a user-contributed preset library. A DMP
PSG instrument stores **macros** — sequences of attenuation values over
timed steps — not ADSR parameters.

Gen VST's SQ mode uses a **software ADSR model** (`SN76489Engine`, Task 23).
The two models are not 1:1. An import bridge that approximates ADSR
parameters from the volume macro is lossy but useful; it gives users access
to the DefleMask PSG community library without requiring Gen VST to adopt a
macro-playback architecture.

## Decision

**Accept DMP version 11, mode 0 (STD/PSG) files as SQ imports.** Derive
approximate SQ ADSR parameters from the DMP volume macro. The import is
explicitly lossy and documented as such.

**Tag resolution for `.dmp` requires peeking at file content.** The
`tagFromExtension()` function in `PatchSystem` is replaced by
`tagFromFile(path)` for `.dmp` files; all other extensions continue to use
extension-only tag derivation. This is a narrow, documented exception to
ADR-0025's extension-only rule.

```
tagFromFile(".dmp", path):
  if file is not readable or not version 11 → FM (existing rejection behaviour)
  read byte 2:
    1 → Tag::FM   (existing FM loader)
    0 → Tag::SQ   (new PSG loader)
    other → reject with toast
```

`tagFromExtension()` remains for all non-`.dmp` extensions; it is called
by the folder-scan path (which must be fast — no file I/O per extension).
The scan marks `.dmp` files with a `Tag::Pending` placeholder and resolves
them lazily (on first expand of the containing folder, or on load attempt).

## Macro → ADSR approximation

The DMP PSG volume macro is a finite sequence of attenuation values (0–15,
where 0 = loudest, 15 = silent) followed by a loop point.

Approximation algorithm for `PsgPreset` params derived from this sequence:

1. **`atk`** — count steps from the first attenuation value down to the
   minimum (peak loudness) in the sequence. Scale to the ADSR atk range.
2. **`dr1`** — count steps from the peak to the first sustained plateau.
3. **`sus`** — the stable plateau level (converted from attenuation to the
   ADSR sustain-level range).
4. **`dr2`** — 0 unless the sequence has a second decay phase below the
   plateau.
5. **`rr`** — inferred from the loop tail if the sequence ends with a
   fade-out; otherwise a sensible default (mid-range).
6. **`vol`** — 1.0 (the macro's peak attenuation already calibrates the
   envelope curve; per-channel volume is left at unity for the user to
   adjust).
7. **`pan`** — 0.0 (centred; DMP PSG has no pan concept).
8. **`detune`** — 0 (DMP arpeggio macro is a pitch sequence, not a static
   detune; it is dropped for now — multi-step arpeggios are outside the
   `.psg` schema).

DMP PSG instruments that use the arpeggio or pitch macros will import with
`detune = 0` and a note in the notification toast: "DMP PSG arpeggio / pitch
macro ignored — only volume envelope imported."

The DMP STD body for Genesis PSG (system 0x02 / 0x42) carries duty- and
wave-macro slots that have no community-agreed mapping to the SN76489 noise
channel's mode (`white` / `periodic`) and shift rate (`low` / `mid` /
`high` / `ch2`) — DefleMask configures Genesis noise mode at the channel
level, not per-instrument. The import bridge therefore consumes those
macros to advance the parser cursor but **does not** derive `noise.type` or
`noise.rate` from them; the noise channel lands at the apvts defaults
(`vol = 0`, `type = white`, `rate = mid`). Users tune the noise channel
manually after import.

The approximation is **best-effort**. A DMP preset with a complex,
multi-plateau volume sequence will not round-trip cleanly. This is
acceptable: the import bridge provides a starting point, not a perfect
conversion. The `.psg` native format is the authoritative SQ preset model;
DMP PSG is an import-only path.

## Consequences

- `PatchSystem` gains `tagFromFile(const std::filesystem::path&)` for
  `.dmp` only; all other extensions continue through `tagFromExtension`.
  The folder-scan marks `.dmp` files as `Tag::Pending`; a pending-tag
  file is resolved on first browse-expand or on load attempt.
- A new `loadDmpPsg(path)` function in `src/PsgPreset.{h,cpp}` (the file
  that already owns the `PsgPreset` data model and the `.psg` loader) handles
  mode-0 DMP files. It returns a `PsgPreset` on success, or a descriptive
  error string for the notification toast — plus a non-fatal `warning` field
  on `PsgPresetLoadResult` for the arpeggio / pitch-macro toast. The FM DMP
  loader (`loadDMP`) stays where it has always lived, in `src/PatchSystem.cpp`
  alongside the other format loaders.
- The `.dmp` extension is added to `kSupportedPatchExtensions` (it was
  already present for FM; the set remains unchanged — both modes use the
  same extension and the tag resolver handles the split).
- Users see a `SQ` badge for PSG DMP files in the browser — they load
  straight into SQ mode with auto-mode-switch, identical to `.psg`.
- The file-picker filter `*.tfi;*.vgi;*.dmp;*.y12;*.opm;*.psg` is
  unchanged; the `Import file` path resolves the tag via `tagFromFile`.
- Drag-and-drop of a `.dmp` file is resolved via `tagFromFile` by the
  native drop handler (it already has the full path).
- DMP PSG import lands in **Task 10**. Task 09's `tagFromExtension`
  continues to treat `.dmp` as FM-only; Task 10 upgrades it to
  `tagFromFile` for `.dmp` and adds the PSG loader.

## Alternatives considered

- **Treat all `.dmp` as FM-only permanently** — simple, but silently
  rejects valid PSG instruments that a user explicitly drops onto the
  window; the error toast would be confusing. Rejected.
- **Separate extension for PSG DMP** (e.g., `.dmp-psg` or `.sdmp`) — no
  community convention exists for this; renaming user files is not
  acceptable. Rejected.
- **Adopt a macro-playback engine for SQ mode** — would make DMP PSG
  playback faithful but fundamentally changes the SQ mode from a
  parametric ADSR synth to a tracker-style macro player. Incompatible with
  the plugin's DAW-instrument design goals (automation lanes, per-parameter
  modulation, predictable envelope behaviour). Rejected.
- **Extend ADR-0025 to allow a sidecar `.dmp.tag` file** — complexity with
  no benefit; the problem is uniquely narrow (one ambiguous extension).
  Rejected.
