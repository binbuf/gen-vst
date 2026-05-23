# UI View Catalog

## Purpose & relationship to other docs

This document is the **exhaustive per-view specification** for the Gen VST
interface. Every screen, panel, modal and dialog the user can reach is defined
here with a layout, a control list and behaviour notes.

- [`genny-ui.md`](../genny-ui.md) — the authoritative **visual style** reference
  and the detailed spec of the main window's FM section.
- [`05-ui-ux.md`](05-ui-ux.md) — UI **strategy & architecture**: the WebView
  approach, the component inventory, and the C++↔JS integration contract.
- **This document** — the **complete set of views**. Where `genny-ui.md` already
  specifies a surface (the FM section), this doc cross-references it and fills in
  what was missing; every other surface is defined here from scratch.

The visual language for all views is the pixel-art skeuomorphic style in
`genny-ui.md` and the binding pixel-art rules in `05-ui-ux.md` (1× grid,
`image-rendering: pixelated`, no `border-radius`, hard-edged bevels, 8px grid).
New views invent layouts that obey that language; **exact pixel coordinates and
sizes are tuned at implementation time against `genny-ui.md`** — this doc fixes
the content and structure, not the precise pixels.

## Conventions

- **One OS window.** There is exactly one native window — the fixed 960×640
  WebView ([ADR-0007](adr/0007-fixed-window-size.md)). Every "popup", "sub-window"
  and "modal" in this catalog is an **in-WebView overlay layer** (a DOM layer
  drawn on the same canvas), **not** a separate OS window. The only exceptions
  are native OS file choosers (view 11) and the native fallback panel (view 9),
  which are not WebView content at all.
- **Scaling.** The whole window scales by integer presets per
  [ADR-0017](adr/0017-hidpi-display-scaling.md).
- **Backends.** The UI renders on three WebView engines; functional parity is
  required, pixel-parity is not ([ADR-0015](adr/0015-webview-backend-support.md)).

## View index

| # | View | Type | Entry point |
|---|------|------|-------------|
| 1 | Main window — FM section | Base view | Default; `FM` section pill |
| 2 | Main window — SQ (PSG) section | Base view | `SQ` section pill |
| 3 | Main window — D (DAC) section | Base view | `D` section pill |
| 4 | Patch browser | Modal overlay | Folder icon in the Presets/Import tab header |
| 5 | MIDI routing editor | Modal overlay | `MIDI ROUTING…` button in Settings |
| 6 | Settings | Modal overlay | Gear icon in the header |
| 7 | About / credits | Modal overlay | `ABOUT…` in Settings, or click the wordmark |
| 8 | Notification toast | Transient overlay | System-triggered (`notify` event) |
| 9 | WebView fallback panel | Native (non-WebView) | Shown when the WebView fails to init |
| 10 | Per-part polyphony controls | Inline (FM section) | Always visible in the FM section |
| 11 | Native file choosers | Native OS dialog | Import/Export/Add-Folder/Load-WAV buttons |

---

## 1. Main window — FM section

The default view. Its layout — header, left column (LFO/Algorithm), center column
(instrument rack + per-row routing), right column (Presets), and the bottom row of
four operator panels — is specified in full in [`genny-ui.md`](../genny-ui.md).
This section records only what `genny-ui.md` left unplaced.

### Center column — Instrument rack (Task 22)

The center column hosts a **user-curated instrument rack** in place of the
fixed `INSTRUMENTS` LCD + global `FM / SQ / D` pills + global routing strip
shown in earlier drafts. The rack is an ordered list of N loaded instruments,
each one a typed slot (FM / SQ / D) with its own MIDI channel, transpose, range,
detune and L/R balance. The underlying 6-FM + 3-PSG-tone + 1-noise + 1-DAC
engine is **unchanged** (per [ADR-0013](adr/0013-multitimbral-voice-model.md));
the rack is a UI repackaging that maps each row onto one of the fixed parts.

```
┌─ INSTRUMENTS ──────────────────── + − ┐
│ ◇  ~  GADGET BASS                ::: −│   ← FM row, slot 1
│ ◇  ⊓  PSG 1                       ::: −│   ← SQ row, slot M1
│ ◇  □  break_amen.wav              ::: −│   ← D row (DAC)
│ + ADD INSTRUMENT  …                    │
└────────────────────────────────────────┘
TYPE  [FM / SQ / D]                ← read-only (set by row click)
CHANS [1 2 3 4 5]                  ← read-only slot indicator
MIDI  [ 1▾]
TRPS  [ 0▾] [ 0▾]
RNG   |▟───────────▙|   0–127
DET   |─────▟────|       0 ¢
BAL   |───────▟───|
```

**Rack pool.** FM rows occupy parts 0..4 (5 slots; channel 6 is reserved per
[ADR-0014](adr/0014-special-channel-features.md)); SQ rows occupy the three
PSG tone channels (`M1`..`M3`) and the PSG noise channel (`M4`); the D pool
has the single DAC slot. `+ ADD INSTRUMENT` opens a small 3-row popover
(FM / SQ / D); the choice picks `getFreeSlot(type)` and either opens the
patch browser (FM — scoped to FM patches), the WAV loader (D), or simply
activates the slot (SQ, since PSG is parameter-driven). A full pool surfaces
a toast: "All `<type>` slots are in use."

**Row selection.** Clicking a row sets the active rack slot. For FM rows this
calls the existing `selectChannel(n)` path so the bottom panel + right
column rebind to that part's apvts parameters; for SQ rows the bottom region
swaps to the SQ view (view 2); D rows swap to the D view (view 3). The
`FM / SQ / D` type pills are non-interactive in this revision — section
selection is implied by the row click.

**Per-instrument routing.** The strip beneath the rack binds to the selected
row's apvts params:

- `midi_ch_*` — step field, 1..16 (or 0 = Off). Per-row MIDI channel
  override; this is the user-facing edit point for routing. The MIDI
  ROUTING modal (view 5) stays as the conflict-overview surface.
- `transpose_st_*` — semitone offset (−24..+24).
- `transpose_oct_*` — octave offset (−2..+2).
- `note_lo_*` / `note_hi_*` — two-thumb range slider; notes outside the
  window are silently dropped.
- `detune_cents_*` — −100..+100 cents, applied as a fractional pitch offset
  on top of pitch bend.
- `balance_*` — −1..+1 stereo balance.

The `_*` suffix is `_part<n>` for FM parts, `_psg_ch1..3` / `_psg_noise`
for the PSG slots, and `_dac` for the DAC slot.

**Channel slot cells.** The `CHANS` row below the type indicator is now a
read-only slot indicator for the currently selected row's type. FM rows
highlight one of `1..5`; SQ rows show `M1 M2 M3 M4` and highlight one of
them; D rows show just `6` (the DAC chip channel). User-driven slot
reassignment is a post-MVP nicety.

### Right column — Patch-list data sources

The two right-column lists each pin to one of the patch roots from
[`04-patch-system.md`](04-patch-system.md) *Patch roots*:

- **PRESETS** tab → user-saved root (`…/patches/saved/`).
- **IMPORT** tab → user-imported root (`…/patches/imported/`).

Both start empty on a fresh install and fill in as the user saves or
imports. The factory bank is no longer pinned to the center column — the
rack's `+ → FM` button opens the patch browser modal (view 4) which is the
unified navigator for every patch root, factory included. The full mapping,
the modal's role, and the list/tab behaviour are specified in view 4 below.

### Header meter bay

`genny-ui.md` places the logo, a "TRUE STEREO" VU meter and the 7-segment
patch-name display in the ~80px header. The header's left zone is extended into a
**meter bay** that also hosts the telemetry indicators that previously had no
home:

```
┌─ HEADER ────────────────────────────────────────────────────────────┐
│ ░GEN VST░   ┌VU┐ ┌─oscilloscope─┐   [ ▌ RED 7-SEG PATCH NAME ▐ ] ⚙ │
│             └──┘ └──────────────┘   ················voice ▮▮▮▮▮ ◆clip│
└──────────────────────────────────────────────────────────────────────┘
```

- **Oscilloscope** — a small green-LCD inset in the meter bay, right of the VU
  meter. Draws the recent mixed output; fed by the C++→JS `meterData` telemetry
  push (`05-ui-ux.md`). Component: `oscilloscope`.
- **Voice-activity LEDs** — a row of **16** tiny LEDs (one per pool voice,
  [ADR-0010](adr/0010-ymfm-instance-model.md)); each lights while its voice is
  keyed on. Driven by the `voiceMask` field of the telemetry push. Placed along
  the lower edge of the header.
- **Clip LED** — one red LED at the end of the voice row; lights from the
  telemetry `clip` flag and decays over ~1s.
- **Gear icon** — top-right of the header; opens the Settings modal (view 6).

### Section tabs

The `FM / SQ / D` pills (`genny-ui.md`, center column) swap **only the bottom
~220px region** of the window via the `selectSection` native function
(`05-ui-ux.md`): FM shows the four operator panels; SQ shows view 2; D shows
view 3. The header and the left/center/right columns persist across sections.

### Per-part polyphony controls

The FM section's center-column control stack gains a polyphony group — see
view 10.

### Reset-part button

The CHANNELS 1–6 selector row ends with a small `R` button that resets every
parameter of the currently selected FM part to its `juce::AudioParameter`
default (operators, envelopes, alg/fb, polyphony settings) and clears the
active patch path. A confirmation modal guards the destructive action.
Backed by the `resetCurrentPart` native function — implemented as a parameter
walk over IDs ending in `_part<n>`, so the FM-relay rebind path repaints the
entire panel in one batch (same mechanism as channel paging).

### Center-column visual treatment

The lower half of the center column (CHANNELS / MIDI / TRPS / RNG / DEL / PAN
/ POLY / GLIDE / SPREAD) sits on the same green LCD background as the
Instruments list directly above it, rather than the dark chassis used by the
bottom row of operator panels. Labels use the dark-LCD ink colour
(`--lcd-text-dark`); numeric value placeholders stay LED-red so each reads
as a tiny inset display printed on the green LCD. The bottom row (operator
panels) stays on the dark chassis — that contrast is intentional and matches
the mixing-console feel called out in `genny-ui.md`.

### Header oscilloscope and VU idle behaviour

When no audio is playing, the oscilloscope still draws a faint baseline trace
in `lcd-pixel-hi` so the meter visibly lives, and the VU meter's first
segment is always lit (dim phosphor) for the same reason. Without these
"alive" indicators the meters in a silent project looked indistinguishable
from a broken telemetry pipe.

---

## 2. Main window — SQ (PSG) section

Shown in the bottom region when the `SQ` pill is selected. The SN76489 PSG has
three tone channels and one noise channel ([`03-psg-synthesis.md`](03-psg-synthesis.md)),
so the region mirrors the FM section's four-panel rhythm: a thin section-header
band plus **four channel panels**.

```
┌─ SQUARE · SN76489 PSG ───────────────────  PSG MIX ▭▭▭▭▭·· 0  [LAYER] ┐
│ ┌ TONE 1 ──────┐ ┌ TONE 2 ──────┐ ┌ TONE 3 ──────┐ ┌ NOISE ───────┐ │
│ │ ◆  MIDI [11] │ │ ◆  MIDI [12] │ │ ◆  MIDI [13] │ │ ◆  MIDI [14] │ │
│ │  (VOL) (PAN) │ │  (VOL) (PAN) │ │  (VOL) (PAN) │ │  (VOL) (PAN) │ │
│ │  BEND [on ]  │ │  BEND [on ]  │ │  BEND [on ]  │ │ TYPE [WHITE] │ │
│ │  note:  A4   │ │  note:  ──   │ │  note:  ──   │ │ RATE [ MID ] │ │
│ │              │ │              │ │              │ │ AUTO [off]   │ │
│ └──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

**Section-header band**

- `PSG MIX` — global PSG mix-level slider (0–1), PSG contribution to the main
  output.
- `LAYER` — "PSG Layer Mode" toggle (Option B in `03-psg-synthesis.md`): layer
  PSG on every FM note-on. Off by default.

**Tone panel (×3 — channels 0/1/2)**

- Activity LED + `TONE n` label.
- `MIDI` — step-field, the channel's MIDI assignment (defaults 11/12/13).
- `VOL` — volume knob (maps to 4-bit attenuation).
- `PAN` — soft-pan slider (per-channel L/R gain; PSG has no hardware pan).
- `BEND` — pitch-bend enable toggle (PSG bend is opt-in, `07-feature-spec.md`).
- `note` — read-only current-note readout.

**Noise panel (channel 3)**

- Activity LED + `NOISE` label; `MIDI` step-field (default 14); `VOL`; `PAN`.
- `TYPE` — periodic / white toggle.
- `RATE` — shift-rate selector: `HIGH` / `MID` / `LOW` / `CH2` (the four shift
  rates, `CH2` = locked to tone-channel-2 frequency).
- `AUTO` — toggle for the optional MIDI-note → shift-rate auto-mapping
  (`03-psg-synthesis.md`); off by default.

`TYPE` and `RATE` are direct, automatable parameters (`03-psg-synthesis.md`).
All SQ controls are `apvts` parameters bound through the standard relays.

---

## 3. Main window — D (DAC) section

Shown in the bottom region when the `D` pill is selected. Controls the dedicated
DAC sample channel ([ADR-0014](adr/0014-special-channel-features.md)).

```
┌─ DAC · PCM SAMPLE CHANNEL ──────────────  ENABLE [on]   MIDI [16] ────┐
│ ┌ SAMPLE ──────────────────────────┐  ┌ PLAYBACK ───────────────────┐ │
│ │ [ LOAD WAV… ]   break_amen.wav   │  │ RATE  [ 8000 ·11025· 22050 ] │ │
│ │ ▟▙▟▆▂▃▅▇▆▂▁▃▆█▆▃▁  1.4 s · 8-bit │  │ MODE  [ ONE-SHOT · LOOP ]    │ │
│ │ [ CLEAR ]                         │  │ (LEVEL)                      │ │
│ └───────────────────────────────────┘  └──────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

- `ENABLE` — DAC enable toggle (register `0x2B`).
- `MIDI` — step-field, the DAC's MIDI channel (default 16).
- `LOAD WAV…` — opens a native file chooser (view 11); loads a WAV, converts to
  8-bit PCM. The converted PCM is embedded in plugin state (`07-feature-spec.md`).
- Sample strip — loaded filename, a green-LCD **waveform display** of the sample,
  its length and bit-depth; `CLEAR` unloads it.
- `RATE` — 8000 / 11025 / 22050 Hz selector.
- `MODE` — one-shot / loop toggle.
- `LEVEL` — DAC output-level knob.

Empty state: before a WAV is loaded the sample strip shows `— no sample —` and
the waveform display is blank; `CLEAR` is disabled.

---

## 4. Patch browser (modal overlay)

A **full-window modal overlay** ([ADR-0006](adr/0006-folder-tree-patch-browser.md);
form confirmed by the user). It covers the 960×640 window; the main UI is dimmed
behind it.

```
┌─ PATCH BROWSER ──────────────────────────────────────────────── [X] ┐
│ [ Search patches…                                              🔍 ] │
│ ┌────────────────────┬──────────────────────────────────────────┐  │
│ │ ▼ Factory      🔒  │  Bass Guitar                              │  │
│ │ ▼ Saved            │  Techno Lead                              │  │
│ │ ▼ Imported         │ ▶ Synth Brass                      ◀ sel  │  │
│ │ ▼ extra  (custom)  │  Marimba                                  │  │
│ │   ▶ 01      (842)  │  …                                        │  │
│ │   ▼ 02      (915)  │                                           │  │
│ │     ▶ game_a  (28) │                                           │  │
│ │   ▶ 03      (770)  │                                           │  │
│ │ [ + Add Folder… ]  │                                           │  │
│ └────────────────────┴──────────────────────────────────────────┘  │
│ [Import file] [Export▾] [Delete]            [▶ Preview]  [ Close ]  │
└──────────────────────────────────────────────────────────────────────┘
```

**Controls**

- **Search box** — filters by patch name across all roots; each hit shows its
  folder path. Backed by the background search index (`04-patch-system.md`).
- **Left pane — folder tree** — every root and its subfolders as a collapsible
  tree. `Factory` carries a lock glyph (read-only). `Saved` and `Imported` are
  the two writable user roots (see `04-patch-system.md` *Patch roots*); custom
  roots follow. Each scanned folder shows its patch count. Lazy scan on first
  expand.
- **Right pane — patch list** — the `.tfi`/`.vgi`/`.dmp` files in the selected
  folder.
- **`+ Add Folder…`** — directory picker; registers a custom root (view 11).
- **`Import file`** — file picker (`*.tfi;*.vgi;*.dmp`); copies into the
  user-imported root (`…/patches/imported/`).
- **`Export▾`** — export the current patch as TFI or VGI (save dialog).
- **`Delete`** — removes a patch from a writable root; disabled for `Factory`.
- **`Preview`** — middle-C note-on at fixed velocity for ~1s into the active part.
- **`Close` / `[X]`** — dismiss the modal.

**Behaviour**

- Single-click or `Enter` on a patch loads it into the **currently selected FM
  part** ([ADR-0013](adr/0013-multitimbral-voice-model.md)); the modal stays open
  so several patches can be auditioned. `Close` dismisses it.
- **Relationship to the main-window lists.** The main window's three
  patch-list surfaces are *quick-access* views, each pinned to one of the
  writable/read-only roots from `04-patch-system.md`:
    - **INSTRUMENTS** (center column) → the **factory** root.
    - **PRESETS** tab (right column) → the **user-saved** root
      (`…/patches/saved/`). Empty until the user calls `savePatch()`.
    - **IMPORT** tab (right column) → the **user-imported** root
      (`…/patches/imported/`). Empty until the user imports a file or
      drag-drops one.
  The browser modal is the full folder-tree navigator and the only place to
  manage roots, import, export and delete; it can also browse any custom
  roots, which the main-window lists do not expose. The folder icon in the
  Presets/Import tab header opens this modal.
- A load failure raises a notification toast (view 8); it never blocks.

---

## 5. MIDI routing editor (modal overlay)

A modal giving a single consolidated view of the MIDI-channel → destination
binding table. The per-destination MIDI channel can also be set inline (the
`MIDI` step-fields in views 1/2/3); this editor is the authoritative overview and
the place conflicts are surfaced.

```
┌─ MIDI ROUTING ───────────────────────────────────────────────── [X] ┐
│  DESTINATION              MIDI CHANNEL                                │
│  FM Part 1                [  1 ▾]                                     │
│  FM Part 2                [  2 ▾]                                     │
│  …                        …                                          │
│  FM Part 6                [  6 ▾]                                     │
│  PSG Tone 1 / 2 / 3       [ 11 ▾] [ 12 ▾] [ 13 ▾]                     │
│  PSG Noise                [ 14 ▾]                                     │
│  DAC                      [ 16 ▾]                                     │
│  ⚠ Channel 11 is assigned to two destinations.                        │
│                              [ Reset to defaults ]      [ Close ]    │
└──────────────────────────────────────────────────────────────────────┘
```

- One row per destination: 6 FM parts, 3 PSG tone slots, PSG noise, DAC. Each
  has a MIDI-channel selector (1–16, or `Off`).
- **Conflict highlighting** — if two destinations share a channel, both rows are
  flagged and a warning line is shown. Sharing is permitted (it is a valid layer
  setup) but surfaced so it is never accidental.
- `Reset to defaults` restores the documented default map (FM 1–6, PSG 11–14,
  DAC 16).
- The table is persisted in plugin state (`07-feature-spec.md`).

---

## 6. Settings (modal overlay)

Global plugin preferences. Opened from the header gear icon.

```
┌─ SETTINGS ──────────────────────────────────────────────────── [X] ┐
│  VOICE COUNT        [ 8 · 12 ·(16)]                                  │
│  PITCH BEND RANGE   [±1 ·(±2)· ±7 · ±12]   semitones                 │
│  UI SCALE           [(1×)· 2× · 3×]                                  │
│  VELOCITY → TL      [ on ]                                           │
│  AFTERTOUCH         [ Off ·(LFO depth)· Carrier TL ]                 │
│  TOOLTIPS           [ on ]                                           │
│  ────────────────────────────────────────────────────────────────   │
│  [ MIDI ROUTING… ]      [ ABOUT / CREDITS… ]                         │
│  [ RESET ALL TO DEFAULTS ]                                           │
│                                                  [ Close ]          │
└──────────────────────────────────────────────────────────────────────┘
```

- `VOICE COUNT` — 8 / 12 / 16 (`07-feature-spec.md`; default 16).
- `PITCH BEND RANGE` — ±1 / ±2 / ±7 / ±12 semitones (default ±2).
- `UI SCALE` — 1× / 2× / 3× integer presets ([ADR-0017](adr/0017-hidpi-display-scaling.md)).
- `VELOCITY → TL` — enable/disable velocity → TL scaling.
- `AFTERTOUCH` — channel-pressure routing: Off / LFO depth / Carrier TL.
- `TOOLTIPS` — global hover-tooltip toggle (default on). Persisted in apvts
  as `tooltips_enabled` so the user's preference survives across sessions.
- `MIDI ROUTING…` opens view 5; `ABOUT / CREDITS…` opens view 7.
- `RESET ALL TO DEFAULTS` — destructive button (red label). After a
  confirmation modal, snaps every parameter (every FM part, PSG, DAC, global
  settings) to its `juce::AudioParameter` default and resets routing. Active
  patch paths are cleared; the DAC sample is unloaded.

Per-part settings (polyphony mode, unison spread, mono retrigger/legato) are
**not** here — they live with the selected part (view 10).

---

## 7. About / credits (modal overlay)

A modal carrying the version and the **license attributions**. The project is
GPL v3 and bundles third-party code and data, so this surface is legally
required, not optional.

```
┌─ ABOUT ─────────────────────────────────────────────────────── [X] ┐
│              ░ GEN VST ░   v0.1.0                                    │
│        Sega Genesis YM2612 + SN76489 emulation                       │
│                                                                      │
│  Gen VST is free software under the GNU GPL v3.                      │
│                                                                      │
│  ymfm                  BSD-3-Clause      (YM2612 core)               │
│  libvgm sn764xx        LGPL              (SN76489 core)               │
│  JUCE 8                GPL v3                                         │
│  Furnace tfilib        GPL               (factory patch bank)         │
│  Press Start 2P        SIL OFL           (label font)                │
│  torinak 7-segment     SIL OFL           (patch-display font)         │
│                                                                      │
│  Source: <repository URL>                          [ Close ]        │
└──────────────────────────────────────────────────────────────────────┘
```

The attribution list is kept in sync with the *Legal Notes* table in
[`04-patch-system.md`](04-patch-system.md) and the licensing ADRs
([ADR-0003](adr/0003-gpl-v3-license.md), [ADR-0004](adr/0004-furnace-only-factory-bank.md)).

---

## 8. Notification toast

The single user-visible error/status channel (`05-ui-ux.md`, component
`notification-toast`). Driven by the C++→JS `notify` event `{ level, message }`.

- **Position** — slides down from the top edge, centered, below the header.
- **Levels & colour** — `info` (green-LCD palette), `warn` (logo-yellow palette),
  `error` (LED-red palette). Hard 1–2px border, no radius, per the pixel-art
  rules.
- **Duration** — auto-dismiss after ~4s; click to dismiss immediately.
- **Stacking** — at most two visible at once; further notifications queue.
- **Triggers** — bad/unreadable patch file, DMP version rejected
  ([ADR-0012](adr/0012-dmp-version-scope.md)), a custom root that no longer
  resolves, a saved patch path that no longer resolves on project load.

---

## 9. WebView fallback panel (native, non-WebView)

If `juce::WebBrowserComponent` fails to initialise — most often a missing or
broken WebView2 runtime on Windows ([ADR-0016](adr/0016-webview2-runtime-distribution.md))
— the editor shows this panel **instead of** the WebView. It is drawn with native
`juce::Graphics` (there is no WebView to host HTML), so it is plain and
functional rather than pixel-art styled.

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

- Sized to the 960×640 editor area.
- `Retry` attempts to recreate the WebView (e.g. after the user installs the
  runtime without reloading the plugin).
- The audio processor is unaffected — only the editor is degraded.

---

## 10. Per-part polyphony controls (inline, FM section)

Polyphony mode is a **per-part** setting ([`07-feature-spec.md`](07-feature-spec.md)).
Its controls are added to the FM section's center-column control stack (the
`MIDI / TRANSPOSE / RNG / DEL / PAN` group in `genny-ui.md`), so they edit the
currently selected FM part alongside the rest of that stack.

```
  POLY    [ POLY · MONO · UNISON ]
  ├ MONO   → GLIDE [ RETRIG · LEGATO ]
  └ UNISON → SPREAD ▭▭▭··· 12 ¢
```

- `POLY` — three-way mode selector for the selected part.
- When `MONO` is selected, a `GLIDE` toggle appears: retrigger vs legato.
- When `UNISON` is selected, a `SPREAD` slider appears: unison detune spread in
  cents (0–50).
- In `POLY` mode neither sub-control is shown.

These bind to per-part `apvts` parameters and re-bind on part selection like the
rest of the FM-part controls (`05-ui-ux.md`, *FM channel paging*).

---

## 11. Native file choosers

These are **native OS dialogs** (`juce::FileChooser`), not WebView content. They
look native on each platform — consistent with the functional-parity /
not-pixel-parity stance of [ADR-0015](adr/0015-webview-backend-support.md).

| Trigger | Kind | Filter / result |
|---------|------|-----------------|
| `Import file` (browser) | Open file | `*.tfi;*.vgi;*.dmp` → copied into the user-imported root (`…/patches/imported/`) |
| `Export▾` (browser) | Save file | writes a TFI (42 B) or VGI (43 B) file |
| `+ Add Folder…` (browser) | Choose directory | registers a custom patch root |
| `LOAD WAV…` (D section) | Open file | `*.wav` → converted to 8-bit PCM |

**Drag-and-drop** is the non-dialog path: `.tfi`/`.vgi`/`.dmp` files dropped
on the plugin window import into the user-imported root
(`…/patches/imported/`). A **folder** dropped on the window is now also
treated as an import — every patch file inside the folder is copied
recursively into the user-imported root so the patches appear in the main
window's IMPORT tab. Users who want to register a folder as a browser-only
*custom root* (no copy) use the Patch Browser's "Add Folder..." button
instead. Because an OS drop must yield real filesystem paths, this uses a
native `juce::FileDragAndDropTarget` on the editor, **not** HTML5
drag-and-drop (see `05-ui-ux.md`).

---

## Modal behaviour (shared)

Views 4–7 are in-WebView modal overlays and share this behaviour:

- Open over a **dimmed** main UI; only one modal is open at a time. View 5 and
  view 7 are opened *from* view 6 and replace it.
- Dismissed by `Close`, the `[X]`, or the `Esc` key.
- Modal while open: clicks outside the modal panel do not reach the main UI.
- The notification toast (view 8) may still appear above an open modal.
- Modals are sized within the 960×640 canvas; they never spawn an OS window.

---

## Resolved open questions

This catalog closes the UI open questions previously tracked in `05-ui-ux.md`:

- **Window scaling** — resolved by [ADR-0017](adr/0017-hidpi-display-scaling.md).
- **WebView2 runtime fallback** — the fallback panel is view 9, per
  [ADR-0016](adr/0016-webview2-runtime-distribution.md).
- **SQ / D section parity** — both sections are **fully specified** (views 2 and
  3); they are not stubbed in the design. Build sequencing may still implement
  them after the FM section, but the design is complete.
- **Patch list ↔ part** — the browser loads into the selected part (view 4),
  consistent with [ADR-0013](adr/0013-multitimbral-voice-model.md).
