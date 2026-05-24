# ADR-0025: Tagged unified preset browser with mode auto-switch

- **Status:** Accepted
- **Date:** 2026-05-24
- **Supersedes:** The per-tab patch-list arrangement in archived `v1-08-ui-views.md` (Patch Browser view 4)
- **Related:** [ADR-0006](0006-folder-tree-patch-browser.md), [ADR-0021](0021-three-mode-single-engine-ui.md), `docs/design/04-patch-system.md`

## Context

Under v1, patch types lived in separate UI surfaces — the INSTRUMENTS LCD
(factory FM patches) in the center column, PRESETS and IMPORT tabs in the
right column, plus a full-window patch-browser modal for the folder-tree
navigation across all FM patch roots. PSG had no patch concept; DAC had its
own WAV loader. The v2 three-mode model (ADR-0021) lets a single instance
become any of three engines, which makes the v1 tab-per-source layout
obsolete: a unified browser that shows **all** patches and that auto-switches
the instance's mode when a patch is loaded is a far better fit.

The 2026-05-24 design pivot called for exactly that: a clean tagging
system (e.g. `[FM] Metallic Bass`, `[SQ] Classic Arp`, `[D] Crunchy
Drums`) where double-clicking any preset instantly snaps the VST into the
correct engine mode and layout.

## Decision

The v2 plugin has **one preset browser** that surfaces patches for all three
modes, each tagged with its engine. Loading a patch loads it into the
instance and — if the instance is currently in a different mode — switches
the mode to match.

**Tag derivation: by file extension, no sidecar metadata.**

| Extension | Tag | Source |
|---|---|---|
| `.tfi`, `.vgi`, `.dmp`, `.y12`, `.opm` | **FM** | Existing FM patch loaders |
| `.vgm`, `.vgz` | **FM** | Extracted FM patches from VGM (per Task 21) |
| `.psg` | **SQ** | New v2 JSON preset format for the SN76489 engine |
| `.gdac` | **D** | New v2 JSON preset format for the D-mode DSP (PCM2612-style audio FX). Holds `prescaler`, `mono`, `dry_wet` values + a name. `.wav` files are **not** tagged D in v2 — D mode is an audio FX, not a sampler, and does not load WAV files. |

A file's extension *is* the tag — no separate metadata channel, no
mistaggable sidecar. This keeps the tag derivation deterministic and means
adding a new patch format is a one-line table update.

**`.psg` preset format** (new in v2): a small JSON file with per-channel
envelope settings, noise-shifter rate, channel volumes, and a name field.
Schema is defined in `docs/design/04-patch-system.md`. Default presets ship
in `extern/patches/sq/`. Existing PSG controls in the engine already have
apvts params; the loader is a thin JSON-to-apvts mapper.

**`.gdac` preset format** (new in v2): a small JSON file holding the three
D-mode DSP values — `prescaler` (0.0–1.0 sample-rate decimation amount),
`mono` (bool), `dry_wet` (0.0–1.0 mix) — plus a name field. Schema is
defined in `docs/design/04-patch-system.md`. Default presets ship in
`extern/patches/d/` ("Crunchy Drums", "Voice Sample", "Subtle Crush"). The
loader is a thin JSON-to-apvts mapper, mirroring `.psg`.

**Browser UI structure** (full layout in v2 `08-ui-views.md`):

- A single browser surface — a modal overlay in v2 just as in v1, scaled to
  the new 1200×560 canvas.
- Left pane: folder tree, with a per-root collapsible group. Root types
  (Factory / Saved / Imported / custom) are unchanged from
  [ADR-0006](0006-folder-tree-patch-browser.md).
- Top of the left pane: a **mode filter** chip row — `All / FM / SQ / D` —
  for narrowing the visible patches without abandoning the cross-mode view.
  Default: filter set to the instance's current mode, so the user sees
  "patches for what I'm currently editing" first; switching to `All` lets
  them browse across.
- Right pane: patch list, each row prefixed with a small mode badge
  (`FM` / `SQ` / `D`). Search filters across all roots and all modes.
- Loading a patch — single-click or `Enter` — auto-switches the instance to
  the patch's mode if it differs, then applies the patch. The mode change
  is silent (no confirmation modal); the previous patch is not auto-saved
  but remains untouched on disk.

**Main-window preset access.** The v2 header has a compact patch-name LCD
plus prev/next buttons and a "browse" button that opens the modal. There is
no main-window LCD list of patches — the v1 INSTRUMENTS / PRESETS / IMPORT
list trio is removed (it depended on the multi-pane center/right columns
that v2 no longer uses).

**Preview behaviour.** Single-click on a patch in the browser loads it
immediately into the instance — preview *is* load. This is consistent with
RYM2612 and most modern preset browsers. The browser stays open so several
patches can be auditioned in turn; `Close` dismisses.

## Consequences

- The v1 patch-browser modal (`ui/src/modals/patch-browser.js`) is
  rewritten for the unified tagged view; the v1 main-window LCD lists in
  the center/right columns are deleted along with the rest of the v1 UI in
  Task v2/01.
- `src/PatchSystem.{h,cpp}` gains a `Tag` enum (`FM / SQ / D`) and a
  `tagFromExtension(...)` free function. The folder-scan code starts
  classifying every found file by extension and bucketing it by tag.
- A new `src/PsgPreset.{h,cpp}` defines the `.psg` JSON load / save. The
  factory PSG preset folder `extern/patches/sq/` is created and seeded
  with a small starter set (a handful of bass / lead / arp tones).
- A new `src/DacPreset.{h,cpp}` defines the `.gdac` JSON load / save for
  D-mode DSP settings. The factory D preset folder `extern/patches/d/` is
  created and seeded with a starter set of decimation presets.
- The v1 dedicated DAC `LOAD WAV...` button and WAV drag-drop onto the
  D-section are **removed** — D mode does not load WAV files at all
  ([ADR-0021](0021-three-mode-single-engine-ui.md)). Patch drag-drop onto
  the window continues to work for `.tfi/.vgi/.dmp/.y12/.opm/.psg/.gdac`.
- Auto mode-switch on patch load means `setStateInformation` need only
  restore the patch path — the mode follows automatically. The `mode_select`
  apvts param is still persisted explicitly as a tiebreaker (e.g. for the
  "no patch loaded" state, where the user manually chose a mode).
- The Bank Select / Program Change semantics change: a Program Change now
  loads the Nth patch **of the currently active mode**, not the Nth FM
  patch globally. PC-driven mode switching is **not** supported — modes are
  a per-instance UI/state decision, not a real-time MIDI surface.

## Alternatives considered

- **Per-mode separate browsers (three modal layouts)** — clean separation
  but defeats the "click anything, mode follows" UX the brief asks for, and
  triples the modal-view code. Rejected.
- **Sidecar JSON metadata for tags** — flexible (a `.tfi` could be tagged
  SQ if a user really wanted) but introduces a class of bugs (sidecar
  missing → patch invisible; sidecar out of sync) for no realistic gain.
  Rejected.
- **Tag in the filename** (`bass_FM.tfi`) — robust but ugly, and breaks
  existing factory filenames. Rejected.
- **PC-driven mode switching** — would mean a single MIDI Program Change
  could flip an instance's engine mid-bar. Surprising and unwanted —
  modes are an arrangement-level decision, not a performance gesture.
  Rejected.
