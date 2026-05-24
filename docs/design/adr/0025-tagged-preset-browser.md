# ADR-0025: Tagged unified preset browser with mode auto-switch

- **Status:** Accepted
- **Date:** 2026-05-24 (revised 2026-05-24 to drop the `.gdac` D-mode preset format — see *Alternatives considered*)
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

A file's extension *is* the tag — no separate metadata channel, no
mistaggable sidecar. This keeps the tag derivation deterministic and means
adding a new patch format is a one-line table update.

**`.psg` preset format** (new in v2): a small JSON file with per-channel
envelope settings, noise-shifter rate, channel volumes, and a name field.
Schema is defined in `docs/design/04-patch-system.md`. Default presets ship
in `extern/patches/sq/`. Existing PSG controls in the engine already have
apvts params; the loader is a thin JSON-to-apvts mapper.

**D mode has no preset format.** D is an audio FX with three apvts params
(`prescaler`, `mono`, `dry_wet`); the DAW's project save / load already
persists those values via the normal `setStateInformation` flow and every
DAW ships its own "user preset" system for cross-project recall. A
dedicated `.gdac` JSON format was considered (3 floats + a name, mirroring
`.psg`) and dropped because the machinery — schema, loader, factory files
in `extern/patches/d/`, browser tag, drag-drop handling, CMake staging —
is disproportionate for a 3-knob FX, and the asymmetry of "D-mode preset
chrome in the header" was forcing UX trade-offs elsewhere (greyed/disabled
patch controls, half-existing browser tag). FM (50 params, six import
formats) and SQ (per-channel envelopes) genuinely earn their patch
systems; D does not. Removing the format makes D act like any other
built-in DAW audio FX: tune the knobs, the host owns the state.

**Browser UI structure** (full layout in v2 `08-ui-views.md`):

- A single browser surface — a modal overlay in v2 just as in v1, scaled to
  the new 1200×560 canvas.
- Left pane: folder tree, with a per-root collapsible group. Root types
  (Factory / Saved / Imported / custom) are unchanged from
  [ADR-0006](0006-folder-tree-patch-browser.md).
- Top of the left pane: a **mode filter** chip row — `All / FM / SQ` —
  for narrowing the visible patches without abandoning the cross-mode view.
  Default: filter set to the instance's current mode (or `All` when the
  instance is in D mode, since D has no presets to filter to), so the
  user sees "patches for what I'm currently editing" first; switching
  to `All` lets them browse across.
- Right pane: patch list, each row prefixed with a small mode badge
  (`FM` / `SQ`). Search filters across all roots and both modes.
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
- `src/PatchSystem.{h,cpp}` gains a `Tag` enum (`FM / SQ`) and a
  `tagFromExtension(...)` free function. The folder-scan code starts
  classifying every found file by extension and bucketing it by tag.
  D mode is still a `mode_select` value but is **not** a preset tag —
  there is no file extension that resolves to a D tag.
- A new `src/PsgPreset.{h,cpp}` defines the `.psg` JSON load / save. The
  factory PSG preset folder `extern/patches/sq/` is created and seeded
  with a small starter set (a handful of bass / lead / arp tones). No
  equivalent class exists for D mode.
- The v1 dedicated DAC `LOAD WAV...` button and WAV drag-drop onto the
  D-section are **removed** — D mode does not load WAV files at all
  ([ADR-0021](0021-three-mode-single-engine-ui.md)). Patch drag-drop onto
  the window continues to work for `.tfi/.vgi/.dmp/.y12/.opm/.psg`.
- **Header patch chrome in D mode**: the persistent patch-name LCD +
  prev/next + browse buttons in the header are greyed (`.is-disabled`)
  when `mode_select == D` — there is no D preset to display, navigate,
  or save. A D-mode user who wants to switch modes via a preset clicks
  the FM or SQ pill in the mode selector first (which leaves the D
  apvts values untouched — see [ADR-0021](0021-three-mode-single-engine-ui.md)
  on manual mode switch), then opens the browser modal from the
  now-active FM/SQ header.
- Auto mode-switch on patch load means `setStateInformation` need only
  restore the patch path — the mode follows automatically *for FM and SQ
  patches*. The `mode_select` apvts param is still persisted explicitly
  because D mode has no preset to restore (D-mode instances persist via
  `mode_select == D` + the three D apvts values; loading the project
  re-enters D with whatever values the host saved).
- The Bank Select / Program Change semantics change: a Program Change now
  loads the Nth patch **of the currently active mode** for FM and SQ
  modes; in D mode Program Change is ignored (there are no presets to
  index). PC-driven mode switching is **not** supported — modes are
  a per-instance UI/state decision, not a real-time MIDI surface.

## Alternatives considered

- **`.gdac` JSON preset format for D mode** — proposed in an earlier
  draft: 3 floats (`prescaler`, `mono`, `dry_wet`) + a name, mirroring
  `.psg`. Would have required `src/DacPreset.{h,cpp}`, factory files in
  `extern/patches/d/` ("Crunchy Drums" / "Voice Sample" / "Subtle
  Crush"), browser tag, drag-drop handler, CMake staging, and a `D`
  chip in the browser. Rejected — disproportionate machinery for a
  3-knob FX when DAW project state + DAW user-preset systems already
  cover the recall use case, and the asymmetry of "D-mode preset chrome
  in the header" was forcing greyed/disabled controls elsewhere in the
  UI for no real user benefit. D acts like a built-in DAW audio FX.
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
