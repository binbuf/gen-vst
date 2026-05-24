# UI View Catalog (v2)

## Purpose & relationship to other docs

This document is the **exhaustive per-view specification** for the Gen
VST interface under v2. Every screen, panel, modal and dialog the user
can reach is defined here with a layout, a control list and behaviour
notes.

- [`05-ui-ux.md`](05-ui-ux.md) — UI **strategy & architecture**: the
  WebView approach, the component inventory, and the C++↔JS integration
  contract.
- [`09-visual-spec.md`](09-visual-spec.md) — the authoritative **visual
  style**: palette, fonts, exact CSS recipes per widget.
- **This document** — the **complete set of views**.

The visual language for all views follows
[`09-visual-spec.md`](09-visual-spec.md) and the modern-aesthetic
principles in [`05-ui-ux.md`](05-ui-ux.md) (top-left light, layered
shadows, monospace labels, antialiasing on). **Exact pixel coordinates
and sizes are tuned at implementation time against the visual spec** —
this doc fixes the content and structure, not the precise pixels.

## Conventions

- **One OS window.** Fixed 1200×560 WebView
  ([ADR-0023](adr/0023-fixed-window-1200x560.md)). Every "popup",
  "sub-window" and "modal" in this catalog is an **in-WebView overlay
  layer** (a DOM layer drawn on the same canvas), **not** a separate OS
  window. The only exceptions are native OS file choosers and the native
  fallback panel, which are not WebView content.
- **Mode-based view swap.** The middle "mode panel" region swaps when
  the active mode changes; the header and status bar persist.
- **Scaling.** Integer-preset scaling per
  [ADR-0017](adr/0017-hidpi-display-scaling.md). Fractional scales are
  visually acceptable in v2 (no pixel-grid constraint —
  [ADR-0022](adr/0022-modern-vst-aesthetic.md)), but the presets remain
  integer for predictability.

## View index

| # | View | Type | Entry point |
|---|------|------|-------------|
| 1 | Header | Persistent base | Always visible |
| 2 | FM mode panel | Base view (mode swap) | `mode_select = FM` |
| 3 | SQ mode panel | Base view (mode swap) | `mode_select = SQ` |
| 4 | D mode panel | Base view (mode swap) | `mode_select = D` |
| 5 | Status bar | Persistent base | Always visible |
| 6 | Preset browser | Modal overlay | 📂 icon in the header |
| 7 | Settings | Modal overlay | ⚙ icon in the header |
| 8 | About / credits | Modal overlay | `ABOUT…` in Settings, or click the wordmark |
| 9 | Notification toast | Transient overlay | System-triggered (`notify` event) |
| 10 | WebView fallback panel | Native (non-WebView) | Shown when the WebView fails to init |
| 11 | Native file choosers | Native OS dialog | Import/Export/Add-Folder buttons |

---

## 1. Header (persistent)

The header is ~64 px tall and spans the full width. It hosts the
brand wordmark, the mode selector, the patch-name LCD with
prev/next/browse buttons, the two output-character toggles, and a gear
icon for settings.

```
┌─ HEADER ─────────────────────────────────────────────────────────────┐
│  ░GEN VST░   [ FM ⋅ SQ ⋅ D ]   [ ◀  ▌GADGET BASS▐  ▶  📂 ]           │
│                              [Output Filtering] [Ladder Effect]   ⚙  │
└──────────────────────────────────────────────────────────────────────┘
```

- **Wordmark** — `GEN VST` in the v2 brand style (see
  [`09-visual-spec.md`](09-visual-spec.md)). Clicking opens the About
  modal (view 8).
- **Mode selector** — 3-segment pill: `FM` / `SQ` / `D`. Bound to
  `mode_select` apvts param. Tapping a different segment loads a
  sensible default preset for that mode ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).
- **Patch-name LCD** (`patch-name-lcd` widget) — large monospace LCD
  showing the active patch name. Flanked by:
  - **◀ / ▶** — prev/next patch within the active mode. Sorted-order
    navigation across all roots.
  - **📂** — opens the preset browser modal (view 6).
- **Output character toggles** — two `toggle-switch` widgets bound to
  `output_filter` and `ladder_effect` apvts params
  ([ADR-0024](adr/0024-hardware-filter-toggles.md)). The Ladder toggle
  is **greyed out in SQ mode** (it has no audible effect there).
- **⚙ Gear** — opens the Settings modal (view 7).

The header persists across mode swaps. The patch-name LCD updates to
whichever patch loads (FM patch, SQ preset, or D preset).

---

## 2. FM mode panel

Active when `mode_select = FM`. Modelled on Inphonik's **RYM2612**: a
dense column-based operator grid with the LFO + algorithm controls
flanking it, an envelope curve overlay, and a frequency-control mode
selector.

```
┌─ FM MODE ──────────────────────────────────────────────────────────────────────┐
│ LFO  RATE  PMS  AMS    ┌─ ENVELOPE CURVE ─┐  FREQ CTRL MODE          OP1 FB    │
│ [○] [○]  [○]  [○]      │                  │  [INT MUL]                          │
│ POLY  LEGATO RANGE     │   ╱╲___          │  [FLOAT MUL]                [○]    │
│ [⋅]   [⋅⋅]  [⋅⋅⋅]      │                  │  [AUTO RETRIG]                      │
│ PB ▭▭▭                 └──────────────────┘                                     │
│ MW ▭▭▭                                                                          │
│                                                                                  │
│  ┌─ OPERATOR GRID ────────────────────────────────────────────────┐  ┌ ALGO ─┐ │
│  │       AM  AR  DR  SL  SR  RR  RS  SSG-EG  MUL  FREQ  FIXED  DT │  │  ┌──┐ │ │
│  │  [1]  ▢   ○   ○   ○   ○   ○   ○   [OFF]   ○   [3.00] ▢     ○  │  │  │ 4│ │ │
│  │  [2]  ▢   ○   ○   ○   ○   ○   ○   [OFF]   ○   [1.00] ▢     ○  │  │  └──┘ │ │
│  │  [3]  ▢   ○   ○   ○   ○   ○   ○   [OFF]   ○   [0.50] ▢     ○  │  │ ALG 4 │ │
│  │  [4]  ▢   ○   ○   ○   ○   ○   ○   [OFF]   ○   [0.50] ▢     ○  │  │       │ │
│  │  TL: |▟|  |▟|  |▟|  |▟|     VEL: ○  ○  ○  ○                    │  │       │ │
│  └────────────────────────────────────────────────────────────────┘  └───────┘ │
└────────────────────────────────────────────────────────────────────────────────┘
```

**Top-left block — LFO & global controls**

- `LFO`, `RATE`, `PMS`, `AMS` — four small `knob`s.
- `POLY` — three-position `toggle-switch`: `POLY / MONO / UNISON`.
- `LEGATO` — only visible in Mono: `RETRIG / LEGATO`.
- `RANGE` — only visible in Unison: spread (0–50¢).
- `PB`, `MW` — pitch bend + mod wheel level meters (read-only
  visualisation of incoming MIDI).

**Top-centre — envelope curve**

A large `envelope-curve` widget. Shows the **currently selected
operator's** ADSR shape, computed live from its envelope knobs. Click
an operator row's number badge `[1]..[4]` to swap which operator the
curve tracks.

**Top-right — frequency control mode + OP1 feedback**

- `FREQ CTRL MODE` — three-button pill: `INT MUL / FLOAT MUL / AUTO
  RETRIG`. Selects how the operator `FREQ` value is interpreted
  (integer multiple of the note, free-running float, or auto-retriggering
  for percussion). Bound to a new apvts param `freq_ctrl_mode`.
- `OP1 FB` — single `knob` for operator-1 self-feedback (the YM2612 `FB`
  field).

**Centre-right — algorithm diagram**

`algorithm-mini` widget showing the current algorithm topology (the
8 YM2612 routings; selected one highlighted). Beneath it: `ALG N`
label and a small selector for picking algorithm 1–8.

**Operator grid (main body)**

The bulk of the panel. Four rows, one per operator (S1..S4 — see note
about hardware swap order in [`02-fm-synthesis.md`](02-fm-synthesis.md)).
Columns:

| Column | Type | Bound to (per op) |
|---|---|---|
| `[N]` | Numeric badge / select | Operator index (active-curve target) |
| `AM` | Toggle | `amon[op]` |
| `AR / DR / SL / SR / RR` | Knobs | Envelope rate values |
| `RS` (Rate Scaling) | Knob | `ks[op]` |
| `SSG-EG` | Combo (OFF + 8 shapes) | `ssg[op]` |
| `MUL` | Knob | `mul[op]` |
| `FREQ` | LCD readout | Derived (mul × note) display |
| `FIXED` | Toggle | A new "fixed frequency" flag (post-MVP if not trivial) |
| `DT` | Knob | `dt[op]` |
| `TL` (right margin) | Vertical slider | `tl[op]` — separate vertical column so it reads at a glance |
| `VEL` (right margin) | Knob | A new per-op velocity-to-TL scaling factor |

All controls in the grid are `apvts`-bound through the standard relays.

---

## 3. SQ mode panel

Active when `mode_select = SQ`. Clean subtractive-style layout for 3
tone channels + 1 noise channel. Each channel is a vertical strip with
its own envelope + tuning + level.

```
┌─ SQ MODE ─────────────────────────────────────────────────────────────────────┐
│  ┌ TONE 1 ────────┐  ┌ TONE 2 ────────┐  ┌ TONE 3 ────────┐  ┌ NOISE ────┐  │
│  │ ╱╲___          │  │ ╱╲___          │  │ ╱╲___          │  │ ╱╲___      │  │
│  │ (envelope)     │  │ (envelope)     │  │ (envelope)     │  │ (envelope) │  │
│  │ ATK DR1 SUS    │  │ ATK DR1 SUS    │  │ ATK DR1 SUS    │  │ ATK DR1    │  │
│  │  ○   ○   ○     │  │  ○   ○   ○     │  │  ○   ○   ○     │  │  ○   ○     │  │
│  │ DR2  RR        │  │ DR2  RR        │  │ DR2  RR        │  │ SUS DR2 RR │  │
│  │  ○   ○         │  │  ○   ○         │  │  ○   ○         │  │  ○   ○   ○ │  │
│  │ DETUNE  ○      │  │ DETUNE  ○      │  │ DETUNE  ○      │  │            │  │
│  │ VOL     ○      │  │ VOL     ○      │  │ VOL     ○      │  │ VOL     ○  │  │
│  │ PAN     ▭▭▭    │  │ PAN     ▭▭▭    │  │ PAN     ▭▭▭    │  │ PAN     ▭▭ │  │
│  └────────────────┘  └────────────────┘  └────────────────┘  │ TYPE [W/P] │  │
│                                                              │ RATE [LMH2]│  │
│                                                              └────────────┘  │
└───────────────────────────────────────────────────────────────────────────────┘
```

**Per-tone-channel strip (×3)**

- `envelope-curve` widget showing the channel's ADSR shape (Task 23
  software-ADSR semantics, retained).
- `ATK / DR1 / SUS / DR2 / RR` — five envelope knobs.
- `DETUNE` knob — cents offset.
- `VOL` knob — channel volume.
- `PAN` slider — L/R balance.

**Noise channel strip**

Same envelope + VOL + PAN, plus noise-specific controls:
- `TYPE` — toggle: white (W) / periodic (P).
- `RATE` — 4-position selector: `LOW / MID / HIGH / CH2` (SN76489
  shift-rate options).

All controls are `apvts`-bound. Allocation across the 3 tone channels
(round-robin LRU) and noise (last-note priority) is handled by the
engine ([`03-psg-synthesis.md`](03-psg-synthesis.md)).

The v1 `PSG MIX`, `LAYER` toggle, and SHFT / PERIODIC / TYPE / RATE /
AUTO band are removed — `psg_mix` is gone (no FM-to-mix in v2);
`LAYER` (PSG-on-FM-note) is removed; the noise controls collapse into
the strip above.

---

## 4. D mode panel

Active when `mode_select = D`. Modelled directly on Inphonik's
**PCM2612 Retro Decimator Unit**. Audio FX only — MIDI is ignored.

```
┌─ D MODE — RETRO DECIMATOR UNIT ───────────────────────────────────────────────┐
│                                                                                │
│                              ┌─ DAC PRESCALER ─┐                              │
│                              │      (large)    │                              │
│                              │       ●         │                              │
│                              │                 │                              │
│                              └─────────────────┘                              │
│                                                                                │
│   ┌─ STEREO LEVEL METERS ──────────────────────────────────────────────┐      │
│   │  L  ▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮     [ MONO ]     ▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮ R│      │
│   └────────────────────────────────────────────────────────────────────┘      │
│                                                                                │
│           DRY / WET                                                            │
│              ●                                                                 │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
```

- **`DAC PRESCALER`** — large central `decimator-knob`. Bound to the
  `prescaler` apvts param (0.0..1.0; 0 = no decimation, 1 = max).
- **Stereo level meters** — `level-meter` widget showing input
  (pre-decimation) L/R peaks. Updated via the C++→JS telemetry push.
- **`MONO`** — `toggle-switch` (lit when on); bound to `mono`.
- **`DRY / WET`** — `knob`; bound to `dry_wet`.

The Output Filtering and Ladder Effect toggles live in the **header**,
not on this panel (they're global, not D-specific) — consistent with
how RYM2612 keeps `OUTPUT FILTERING` on its main chassis.

No MIDI controls, no sample loader, no MIDI channel selector — D mode
processes the audio input bus and only the audio input bus
([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).

---

## 5. Status bar (persistent)

A thin (~16 px) strip at the bottom of the window, always visible.

```
┌─ STATUS ─────────────────────────────────────────────────────────────┐
│ ◉ NOTE ON    L ▮▮▮▮▮▮▮▮▮▮▮▮     R ▮▮▮▮▮▮▮▮▮▮▮▮            v0.2.0   │
└──────────────────────────────────────────────────────────────────────┘
```

- `◉ NOTE ON` — `note-on-led`, lit while any voice is keyed on.
  In D mode, lit while audio input exceeds a tiny threshold.
- L / R level bars — output level meters.
- Version string — read-only.

---

## 6. Preset browser (modal overlay)

A **full-window modal overlay**
([ADR-0006](adr/0006-folder-tree-patch-browser.md),
[ADR-0025](adr/0025-tagged-preset-browser.md)). Covers the 1200×560
window; the main UI is dimmed behind it.

```
┌─ PRESET BROWSER ────────────────────────────────────────────────  [X] ┐
│  [All] [FM] [SQ] [D]   [ Search…                                   🔍 ]│
│ ┌────────────────────┬─────────────────────────────────────────────┐  │
│ │ ▼ Factory      🔒  │  FM   Bass Guitar                            │  │
│ │ ▼ Saved            │  FM   Techno Lead                            │  │
│ │ ▼ Imported         │  FM ▶ Synth Brass                  ◀ sel    │  │
│ │ ▼ extra  (custom)  │  SQ   Pulse Arp                              │  │
│ │   ▶ 01      (842)  │  D    Crunchy Drums                          │  │
│ │   ▼ 02      (915)  │  …                                           │  │
│ │     ▶ game_a  (28) │                                              │  │
│ │   ▶ 03      (770)  │                                              │  │
│ │ [ + Add Folder… ]  │                                              │  │
│ └────────────────────┴─────────────────────────────────────────────┘  │
│ [Import file] [Export▾] [Delete]                            [ Close ]  │
└────────────────────────────────────────────────────────────────────────┘
```

**Controls**

- **Mode filter chips** (top-left) — `All / FM / SQ / D`. Default = the
  instance's current mode, so the user first sees patches for what
  they're editing. Switching to `All` shows everything.
- **Search box** — filters by patch name across all roots, honouring
  the active mode chip.
- **Left pane — folder tree** — every root and its subfolders as a
  collapsible tree. `Factory` carries a lock glyph (read-only).
  `Saved`/`Imported` are writable. Each scanned folder shows its patch
  count. Lazy scan on first expand.
- **Right pane — patch list** — each row prefixed with a `FM` / `SQ` /
  `D` badge. Files in the selected folder, filtered by the active mode
  chip.
- **`+ Add Folder…`** — registers a custom root (view 11 native chooser).
- **`Import file`** — file picker
  (`*.tfi;*.vgi;*.dmp;*.y12;*.opm;*.psg;*.gdac`); copies into the
  user-imported root.
- **`Export▾`** — export the current mode's patch as TFI/VGI (FM),
  `.psg` (SQ), or `.gdac` (D).
- **`Delete`** — removes a patch from a writable root; disabled for
  `Factory`.
- **`Close` / `[X]`** — dismiss the modal.

**Behaviour**

- Single-click or `Enter` on a patch loads it into the instance.
  **If the patch's tag differs from the current mode, the instance's
  mode auto-switches** ([ADR-0025](adr/0025-tagged-preset-browser.md));
  no confirmation modal.
- The browser stays open after loading so several patches can be
  auditioned in turn; `Close` dismisses.
- A load failure raises a notification toast (view 9); it never blocks.

The v1 main-window LCD lists (INSTRUMENTS / PRESETS / IMPORT in the
center/right columns) are **removed** — this browser is the only patch
navigator.

---

## 7. Settings (modal overlay)

Global plugin preferences. Opened from the header gear icon.

```
┌─ SETTINGS ──────────────────────────────────────────────────── [X] ┐
│  VOICE COUNT (FM mode)  [ 8 · 12 ·(16)]                              │
│  PITCH BEND RANGE       [±1 ·(±2)· ±7 · ±12]   semitones             │
│  UI SCALE               [(1×)· 2× · 3×]                              │
│  VELOCITY → TL (FM)     [ on ]                                       │
│  AFTERTOUCH             [ Off ·(LFO depth)· Carrier TL ]             │
│  TOOLTIPS               [ on ]                                       │
│  ────────────────────────────────────────────────────────────────   │
│  [ ABOUT / CREDITS… ]                                                │
│  [ RESET ALL TO DEFAULTS ]                                           │
│                                                  [ Close ]          │
└──────────────────────────────────────────────────────────────────────┘
```

- `VOICE COUNT` (FM mode) — 8 / 12 / 16; default 16.
- `PITCH BEND RANGE` — ±1 / ±2 / ±7 / ±12 semitones; default ±2.
- `UI SCALE` — integer presets ([ADR-0017](adr/0017-hidpi-display-scaling.md)).
- `VELOCITY → TL` — FM mode velocity → TL scaling toggle.
- `AFTERTOUCH` — channel pressure routing: Off / LFO depth / Carrier TL.
  Default = **LFO depth (PMS)**.
- `TOOLTIPS` — global hover-tooltip toggle.
- `ABOUT / CREDITS…` opens view 8.
- `RESET ALL TO DEFAULTS` — destructive button (red label). After a
  confirmation modal, snaps every parameter to its `juce::AudioParameter`
  default and clears the active patch path.

The v1 `MIDI ROUTING…` button is **removed** (no routing matrix in v2).

---

## 8. About / credits (modal overlay)

A modal carrying the version and the **license attributions**. The
project is GPL v3 and bundles third-party code and data, so this surface
is legally required, not optional.

```
┌─ ABOUT ─────────────────────────────────────────────────────── [X] ┐
│              ░ GEN VST ░   v0.2.0                                    │
│        Sega Genesis YM2612 + SN76489 emulation                       │
│                                                                      │
│  Gen VST is free software under the GNU GPL v3.                      │
│                                                                      │
│  ymfm                  BSD-3-Clause      (YM2612 core)               │
│  libvgm sn764xx        LGPL              (SN76489 core)               │
│  JUCE 8                GPL v3                                         │
│  Furnace tfilib        GPL               (factory patch bank)         │
│  IBM Plex Mono /       SIL OFL           (label font — TBD)          │
│  JetBrains Mono                                                       │
│  LCD-style face        SIL OFL           (LCD display font — TBD)    │
│                                                                      │
│  Source: <repository URL>                          [ Close ]        │
└──────────────────────────────────────────────────────────────────────┘
```

The attribution list is kept in sync with the *Legal Notes* table in
[`04-patch-system.md`](04-patch-system.md) and the licensing ADRs
([ADR-0003](adr/0003-gpl-v3-license.md), [ADR-0004](adr/0004-furnace-only-factory-bank.md)).
Exact font choices for v2 are pinned in
[`09-visual-spec.md`](09-visual-spec.md).

---

## 9. Notification toast

The single user-visible error/status channel
([`05-ui-ux.md`](05-ui-ux.md), component `notification-toast`). Driven
by the C++→JS `notify` event `{ level, message }`.

- **Position** — slides down from the top edge, centered, below the
  header.
- **Levels & colour** — `info`, `warn`, `error` — palette per
  [`09-visual-spec.md`](09-visual-spec.md).
- **Duration** — auto-dismiss after ~4 s; click to dismiss immediately.
- **Stacking** — at most two visible at once; further notifications queue.
- **Triggers** — bad/unreadable patch file, DMP version rejected
  ([ADR-0012](adr/0012-dmp-version-scope.md)), a custom root that no
  longer resolves, a saved patch path that no longer resolves on project
  load.

---

## 10. WebView fallback panel (native, non-WebView)

If `juce::WebBrowserComponent` fails to initialise — most often a
missing or broken WebView2 runtime on Windows
([ADR-0016](adr/0016-webview2-runtime-distribution.md)) — the editor
shows this panel **instead of** the WebView. Drawn with native
`juce::Graphics`, plain and functional rather than pixel-art styled.

```
┌──────────────────────────────────────────────────────┐
│                                                       │
│              Gen VST — UI unavailable                 │
│                                                       │
│   The WebView component could not be initialised.     │
│   On Windows this usually means the Microsoft         │
│   WebView2 Runtime is missing.                        │
│                                                       │
│   Install it from:                                    │
│   https://developer.microsoft.com/microsoft-edge/     │
│   webview2/                                           │
│                                                       │
│                 [ Retry ]                             │
│                                                       │
│   Audio continues to work while this message shows.   │
└──────────────────────────────────────────────────────┘
```

- Sized to the 1200×560 editor area.
- `Retry` attempts to recreate the WebView.
- The audio processor is unaffected — only the editor is degraded.

---

## 11. Native file choosers

Native OS dialogs (`juce::FileChooser`), not WebView content.

| Trigger | Kind | Filter / result |
|---------|------|-----------------|
| `Import file` (browser) | Open file | `*.tfi;*.vgi;*.dmp;*.y12;*.opm;*.psg;*.gdac` → copied into the user-imported root |
| `Export▾` (browser) | Save file | Writes the current mode's patch format |
| `+ Add Folder…` (browser) | Choose directory | Registers a custom patch root |

**Drag-and-drop** is the non-dialog path: any
`.tfi/.vgi/.dmp/.y12/.opm/.psg/.gdac` file dropped on the plugin window
imports into the user-imported root (and auto-switches mode if it's a
different tag than the current mode). A `.vgm`/`.vgz` triggers VGM bank
import (Task 21 semantics retained). A **folder** dropped on the window
is treated as an import — every supported patch file inside the folder
is copied recursively into the user-imported root. Users who want to
register a folder as a browser-only custom root use the Preset Browser's
"Add Folder…" button instead. Because an OS drop must yield real
filesystem paths, this uses a native `juce::FileDragAndDropTarget` on
the editor, **not** HTML5 drag-and-drop.

The v1 dedicated `LOAD WAV…` button (D section) is **removed** — D mode
no longer loads WAV files.

---

## Modal behaviour (shared)

Views 6–8 are in-WebView modal overlays and share this behaviour:

- Open over a **dimmed** main UI; only one modal is open at a time.
  View 8 is opened *from* view 7 and replaces it.
- Dismissed by `Close`, the `[X]`, or the `Esc` key.
- Modal while open: clicks outside the modal panel do not reach the
  main UI.
- The notification toast (view 9) may still appear above an open modal.
- Modals are sized within the 1200×560 canvas; they never spawn an OS
  window.

---

## What v2 removed from v1's catalog

The following v1 views no longer exist:

- **Instrument rack** (v1 view 1 center column) — multi-instrument
  rack is gone; one engine per instance.
- **MIDI routing editor** (v1 view 5) — no routing matrix; each
  instance is on the host's MIDI channel.
- **Per-part polyphony controls** (v1 view 10) — polyphony is an
  instance-level setting now, lives in the FM mode panel.
- **Header meter bay** (v1 — VU + oscilloscope + 16-voice LED bank +
  clip LED) — replaced by the simpler status bar (view 5) + level
  meters integrated into the D mode panel.
- **Section tabs** (v1 — FM/SQ/D pills as a section selector inside the
  bottom region) — replaced by the **header mode selector** that also
  swaps the full mode panel.
- **Reset-part button** (v1) — replaced by `RESET ALL TO DEFAULTS` in
  Settings (whole-instance, since there's nothing finer than the
  instance).
- **Per-instrument routing strip** (v1) — removed entirely.
