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
  the active mode changes; the header persists.
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
| 5 | Preset browser | Modal overlay | 📂 icon in the header |
| 6 | Settings | Modal overlay | ⚙ icon in the header |
| 7 | About / credits | Modal overlay | `ABOUT…` in Settings, or click the wordmark |
| 8 | Notification toast | Transient overlay | System-triggered (`notify` event) |
| 9 | WebView fallback panel | Native (non-WebView) | Shown when the WebView fails to init |
| 10 | Native file choosers | Native OS dialog | Import/Export/Add-Folder buttons |

The v1-style **bottom status bar is gone** — its output level meters
moved into the header as a stacked L/R cell, and the version string
moved into the About modal (view 7). The 1200×560 canvas now hosts only
two persistent regions (header + mode panel) and the swappable modal
overlays above them.

---

## 1. Header (persistent)

The header is ~88 px tall and spans the full width. It hosts the
brand wordmark, the mode selector, the patch-name LCD with
prev/next/browse buttons, the two output-character toggles, the stacked
L/R output meters (moved here from the retired status bar), the master
VOL knob, and a gear icon for settings.

```
┌─ HEADER ─────────────────────────────────────────────────────────────────────────────────────┐
│  ●     ░GEN·VST░  [FM⋅SQ⋅D]  [◀ ▌GADGET BASS▐ ▶ 📂]  Output  ▌LEGACY▐  LADDER   L ▮▮▮  VOL ⚙│
│ NOTE ON                                              Filter  ▌CRYS C ▐   [▢]    R ▮▮▮▮  [○]  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **`●` NOTE ON LED + label** (`note-on` cluster) — a 16 px round LED
  with a visible `NOTE ON` text label stacked beneath it. Sits at the
  far left of the header. Lit while any voice is keyed on (FM/SQ) or
  while the audio input exceeds a tiny threshold (D). Mirrors the
  prominent top-left NOTE ON indicator on the RYM2612 reference panel.
- **Wordmark** — `GEN VST` in the v2 brand style (see
  [`09-visual-spec.md`](09-visual-spec.md)). Clicking opens the About
  modal (view 7).
- **Mode selector** — 3-segment pill: `FM` / `SQ` / `D`. Bound to
  `mode_select` apvts param. Tapping a different segment loads a
  sensible default preset for that mode ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).
- **Patch-name LCD** (`patch-name-lcd` widget) — large monospace LCD
  showing the active patch name. Flanked by:
  - **◀ / ▶** — prev/next patch within the active mode. Sorted-order
    navigation across all roots.
  - **📂** — opens the preset browser modal (view 5).
- **Output character toggles** — two `toggle-switch` widgets bound to
  `output_filter` and `ladder_effect` apvts params
  ([ADR-0024](adr/0024-hardware-filter-toggles.md)). The Output Filter
  toggle is rendered as a labelled 2-position physical switch —
  `LEGACY` / `CRYSTAL CLEAR` — where `LEGACY` = `output_filter == true`
  (Model-1 analog stage modelled, the default Genesis sound) and
  `CRYSTAL CLEAR` = `output_filter == false` (pure digital, filter
  bypassed); the labelling mirrors the RYM2612 and PCM2612 panels. The
  Ladder toggle is a single on/off LED rocker. **Ladder is greyed out
  in SQ mode** (it has no audible effect there).
- **`DAC PRESCALER` knob** — a `knob-sm` paired with a small `DAC PRESC`
  caption. Bound to apvts param `fm_dac_prescaler` (see
  [`02-fm-synthesis.md`](02-fm-synthesis.md) § *DAC Prescaler (FM
  mode)*). The control is **persistent** in the header — mirroring the
  RYM2612 reference, which places `DAC PRESCALER` next to `VOL` rather
  than inside the operator grid — but its semantics are mode-specific:
  - **FM mode** — active; sweeps the YM2612 DAC clock divider, colouring
    the FM voice rendering with characteristic aliasing / quantization.
  - **SQ mode** — greyed (`.is-disabled`); the SN76489 PSG bypasses the
    YM2612 DAC on real hardware so the prescaler has no audible effect.
  - **D mode** — greyed; the D-mode panel exposes its own larger central
    `DAC PRESCALER` knob (bound to a separate apvts param `prescaler`
    for the audio-FX use case — see view 4).
- **L / R output meters** (`level-meter` widget, `level-meter-mini`
  variant) — two thin horizontal level bars stacked vertically (L on
  top, R below) inside a single recessed cell. Read post-master-gain
  output levels across all modes; updated from the C++→JS telemetry
  push. Replaces the v1-style bottom status bar. The mini-meter
  variant uses 3-px segments so the cell fits the header band; the
  full-fat `level-meter` recipe stays in `09-visual-spec.md` for use
  in the D-mode panel.
- **`VOL` knob** — master output gain. Small `knob` widget sized to fit
  the header band; rest at unity, ~270° sweep. Bound to apvts param
  `master_volume`. Persistent across mode swaps so an instance's level
  rides through FM/SQ/D switches without surprises.
- **⚙ Gear** — opens the Settings modal (view 6).

The header persists across mode swaps. The patch-name LCD updates to
whichever patch loads (FM patch, SQ preset, or D preset). The L/R
output meters update in real time regardless of which mode is active.

---

## 2. FM mode panel

Active when `mode_select = FM`. Modelled on Inphonik's **RYM2612**: a
dense column-based operator grid with `TL` as its leftmost (anchor)
knob, an envelope-curve overlay with parameter-segment labels, an
8-button algorithm picker + larger topology diagram, a frequency-control
mode selector, the YM2612 DAC prescaler, and operator-1 feedback.

```
┌─ FM MODE ───────────────────────────────────────────────────────────────────────────────────────────┐
│ ┌ LFO · GLOBAL ──┐ ┌ ENVELOPE · OP 1 ─────────┐ ┌ FREQ CTRL ─┐ ┌ MISC ─┐ ┌ ALGORITHM ┐ ┌ TOPOLOGY ─┐│
│ │ LFO RATE PMS …  │ │      AR  DR              │ │ [INT MUL]  │ │RETRIG │ │ [1] [2]   │ │ ┌────────┐│
│ │ [○] [○]  [○]    │ │   ╱╲────╲___SL_____SR    │ │ [FLOAT MUL]│ │ 498 ▲▼│ │ [3] [4✓]  │ │ │ 1 ─┐   ││
│ │ POLY 11 RANGE 2 │ │  ║              ╲___     │ │ [AUTO RTR] │ │OP1 FB │ │ [5] [6]   │ │ │ 2 →[4]→││
│ │ [LEGATO·RETRIG] │ │  ║                   ╲RR │ │            │ │  [○]  │ │ [7] [8]   │ │ │ 3 ─┘   ││
│ │                 │ │ KEY ON           KEY OFF │ │            │ │   │   │ │           │ │ └────────┘│
│ └─────────────────┘ └──────────────────────────┘ └────────────┘ └───┼───┘ └───────────┘ │   ALG 4   ││
│                                                                     │                   └───────────┘│
│ ┌ GLOBAL IN ─┐ ┌─ OPERATOR GRID ─────────────────────────────────────│──────────────────┐ ┌ VEL ───┐ │
│ │            │ │  ┌─CH VOL─┐                                         │                  │ │        │ │
│ │  ▌PB▐      │ │  │ [● ]   │  AM  AR  DR  SL  SR  RR  RS  SSG  MUL FREQ FIX  DT          │ │        │ │
│ │   ─        │ │  │        │                                         │                  │ │        │ │
│ │            │ │  │   TL   │  ←  fan-out connectors fan to ops 1..4  │                  │ │        │ │
│ │  ▌MW▐      │ │  │ [●][1] ▢   ○   ○   ○   ○   ○   ○   [OFF]  ○  [3.00] ▢   ○           │ │  ○     │ │
│ │            │ │  │ [●][2] ▢   ○   ○   ○   ○   ○   ○   [OFF]  ○  [1.00] ▢   ○           │ │  ○     │ │
│ │            │ │  │ [●][3] ▢   ○   ○   ○   ○   ○   ○   [REP]  ○  [0.50] ▢   ○           │ │  ○     │ │
│ │            │ │  │ [●][4] ▢   ○   ○   ○   ○   ○   ○   [OFF]  ○  [0.50] ▢   ○           │ │  ○     │ │
│ └────────────┘ └──┴────────┴────────────────────────────────────────────────────────────┘ └────────┘ │
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

The thin vertical line drawn from `OP1 FB`'s knob downward into the
operator grid is a **routing connector** (decoration only). It mirrors
the RYM2612 reference's visual cue that the YM2612 `FB` field affects
operator 1's self-feedback specifically — no other operator carries an
`FB` value. The connector is purely visual; no apvts param.

**Top-left block — LFO & global controls**

- `LFO`, `RATE`, `PMS`, `AMS` — four small `knob`s. Modwheel (CC 1) is
  routed to the `PMS` field at full depth (no adjustable depth knob);
  the v2 first-pass had an `MW→PMS` knob bound to `mw_to_pms`, but it
  was removed during the post-mockup review — modwheel either affects
  PMS or it doesn't, and the per-patch `PMS` knob already covers the
  "amount of vibrato" axis.
- `POLY` — numeric stepper with up/down buttons. Range 1–16, default 16.
  Sets the active polyphony for the voice pool. Matches the RYM2612
  panel's `POLY N` stepper layout. Bound to apvts param `poly_voices`.
- `LEGATO / RETRIG` — always-visible 2-position `toggle-switch`. Mirrors
  the RYM2612 panel toggle. With `poly_voices == 1` this gates mono
  legato (no re-attack on overlapping notes) vs. retrigger; with
  `poly_voices > 1` it gates whether an incoming note that hits an
  already-sounding voice retriggers the envelope or rides through. See
  [`02-fm-synthesis.md`](02-fm-synthesis.md) § *Voice handling — LEGATO
  and RETRIG*. Bound to apvts param `note_mode` (enum:
  `0 = RETRIG, 1 = LEGATO`).
- `RANGE` — numeric stepper for **pitch-bend range in semitones**.
  Range ±1..±12, default ±2. Matches the RYM2612 `RANGE N` stepper.
  Bound to apvts param `pitch_bend_range`.
**Global inputs block (`GLOBAL IN`, mid-row, left of the operator grid)**

- `PB`, `MW` — **global** pitch-bend + mod-wheel visualisations, read-only.
  Implemented as `midi-wheel` widgets (vertical wheel/slider shape with
  a thumb showing the current MIDI value) — they read as hardware
  pitch-bend / mod-wheel controls rather than LED meters, matching the
  RYM2612 reference's bottom-left PB/MW block. The PB wheel uses the
  `midi-wheel-pb` variant (center-detent, thin centerline drawn at the
  zero position); the MW wheel uses `midi-wheel-mw` (full-range, thumb
  at 0 = bottom). The dedicated `GLOBAL IN` block lives in the mid-row
  to the **left** of the operator grid (not buried in the LFO/global
  block any more).

These are *not* per-operator; the RYM2612 reference shows MW as a single
modwheel level for the whole instrument, and Gen VST follows. The
per-operator MW modulation that an earlier draft routed through a
`mw[op]` column has been folded into the per-op `vel[op]` row only —
modwheel still influences operator amplitude globally via the LFO
`PMS` field (at fixed full depth — the v2 first-pass `mw_to_pms` knob
was removed), but doesn't carry an independent per-op depth knob.

The v2-only MONO and UNISON sub-modes that an earlier draft of this
view exposed are not present on the RYM2612 reference and have been
dropped from the FM panel. A standalone `UNISON DETUNE` knob in
Settings (view 6) covers the unison-spread use case without cluttering
the main panel.

**Top-centre — envelope curve with segment labels**

A large `envelope-curve` widget. Shows the **currently selected
operator's** ADSR shape, computed live from its envelope knobs. Click
an operator row's number badge `[1]..[4]` to swap which operator the
curve tracks.

The widget labels each curve segment with the knob that shapes it —
`AR` on the attack ramp, `DR` on the first decay, `SL` near the sustain
shelf, `SR` on the second decay, `RR` on the release tail. Two dashed
vertical markers labelled `KEY ON` (at the note-on column) and `KEY OFF`
(at the start of the release segment) anchor the time axis. Labels and
markers render in `--lcd-text-on` with the same phosphor bloom as the
rest of the LCD glyphs, matching the RYM2612 reference panel. Computing
label positions is part of the widget's render pass — segment lengths
come from the envelope knob values, label positions are placed at the
midpoint of each segment (clamped 4 px away from the LCD edges).

**Right column — frequency control mode + misc block + algorithm
picker + topology diagram**

The top row's right half splits into four narrow blocks, top-to-bottom
inside each:

- **`FREQ CTRL`** — three-button pill (vertical): `INT MUL / FLOAT MUL /
  AUTO RETRIG`. Bound to apvts param `freq_ctrl_mode` (enum: 0=INT_MUL,
  1=FLOAT_MUL, 2=AUTO_RETRIG). Behaviour and the per-op `FREQ` display
  change state-dependently — see the `FREQ` row in the operator-grid
  table below, and
  [`02-fm-synthesis.md`](02-fm-synthesis.md) § *FREQ Control Mode* for
  the register semantics.
- **Misc** — two small cells stacked: `RETRIG RATE` (a
  `stepper-readout` — LCD value flanked by ▲/▼ buttons — bound to
  `retrig_rate`; visible only when `freq_ctrl_mode == AUTO_RETRIG`,
  greyed out otherwise; the RYM2612 reference shows `498`), and
  `OP1 FB` (`knob` bound to `op1_fb`, the YM2612 `FB` field). The OP1
  FB knob carries a short vertical connector line drawn beneath it
  (CSS pseudo-element, decoration only) pointing toward operator 1 in
  the grid below — the `FB` field affects op 1's self-feedback only,
  and the connector visualises that binding. The `DAC PRESCALER` knob
  that an earlier draft placed in this Misc column has moved to the
  **header** (see view 1) per the RYM2612 reference.
- **`ALGORITHM`** — an `algo-grid` of 8 visible numbered buttons (2
  columns × 4 rows), each one selecting an algorithm index 1..8. The
  selected algorithm carries `.is-active` (blue fill + outer glow). All
  8 buttons are visible all the time — no popover. Bound to apvts param
  `algorithm` (0..7).
- **`TOPOLOGY`** — an `algorithm-mini` LCD tile **roughly 2×** the size
  of the algorithm-grid buttons, drawing the routing diagram of the
  *currently selected* algorithm (operator boxes + arrows) with the
  `ALG N` label beneath. Read-only — the picker is the `algo-grid` above.

**Operator grid (main body)**

The bulk of the panel. Four rows, one per operator (S1..S4 — see note
about hardware swap order in [`02-fm-synthesis.md`](02-fm-synthesis.md)).

**Master `CH VOL` knob (channel TL) above the operator grid.** A small
knob sits centered above the leftmost operator-grid column (the TL
column), with thin connector lines fanning down into each of the four
operator rows' TL knobs. The control multiplies into each per-op TL
on the register-write path — `effective_tl_level[op] = tl[op] ×
channel_tl` — and lets the user ride the whole channel's level without
touching all 4 per-op knobs. This is a **UI-only convenience** (the
YM2612 has no per-channel master TL register); the apvts param
`channel_tl` (0.0–1.0, default 1.0) stores it. The fan-out connector
lines are decoration drawn behind the knobs (the `.connector-overlay`
recipe in `09-visual-spec.md`); they reinforce the "this knob touches
every operator's TL" mental model without competing visually with the
actual controls.

Columns (left → right):

| Column | Type | Bound to (per op) |
|---|---|---|
| `TL` (leftmost — anchor knob) | Knob (slightly larger than the row's other knobs, darker body — visual "anchor") | `tl[op]` — UI value is **level** (0 = silent, max = full); see [`02-fm-synthesis.md`](02-fm-synthesis.md) § *UI level vs hardware attenuation* for the inversion |
| `[N]` | `op-badge` widget (blue-filled square per [`09-visual-spec.md`](09-visual-spec.md) § *Operator badge*) | Operator index — click to make this row the active target of the `envelope-curve` widget; carries an outer glow when active |
| `AM` | Toggle | `amon[op]` |
| `AR / DR / SL / SR / RR` | Knobs | Envelope rate values |
| `RS` (Rate Scaling) | Knob | `ks[op]` |
| `SSG-EG` | Combo (OFF + 8 named shapes — `Repeat`, `Hold`, `Alternate`, `Inv. Repeat`, …) | `ssg[op]` |
| `MUL` | Knob | `mul[op]` in `INT_MUL` mode (integer 0–15); `mul_float[op]` in `FLOAT_MUL` mode (decimal); ignored when `fixed[op]` is on |
| `FREQ` | LCD readout (state-dependent) | Display depends on `freq_ctrl_mode` × `fixed[op]`: <br>• `INT MUL` → integer multiplier (`×0.5`, `×1`, `×2`, …, `×15`); `FIXED` has no effect<br>• `FLOAT MUL`, fixed off → float multiplier (e.g. `1.50`)<br>• `FLOAT MUL`, fixed on → absolute frequency in Hz (e.g. `523 Hz`)<br>• `AUTO RETRIG` → same as `FLOAT MUL` |
| `FIXED` | Toggle | `fixed[op]` — when on (and mode = `FLOAT_MUL`/`AUTO_RETRIG`), the operator runs at the absolute frequency `freq_fixed_hz[op]` instead of `note × mul_float[op]`. Greyed out in `INT_MUL` mode (no effect). |
| `DT` | Knob | `dt[op]` |
| `VEL` (right margin, single column) | Knob | `vel[op]` — per-op velocity → TL depth (0 = no effect, 1 = full). RYM2612 manual page 10 |

All controls in the grid are `apvts`-bound through the standard relays.
`TL` lives at the leftmost slot of each operator row (matching the
RYM2612 reference, where the per-op output level is the visually loudest
knob in each row); `VEL` is the only per-op modulation column in the
right margin. The earlier `MW (right margin)` per-op column is removed
— modwheel is a global instrument-level input only (PB / MW meters in
the `GLOBAL IN` block).

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

**Layout note (canvas shape).** PCM2612's hardware artwork is roughly
portrait (9:16), while Gen VST's window is fixed landscape 1200×560
([ADR-0023](adr/0023-fixed-window-1200x560.md)). The D mode controls
are therefore centered horizontally and the wide bands on either side
of the centered column carry the same brushed-metal chassis treatment
defined in [`09-visual-spec.md`](09-visual-spec.md) so the panel reads
as one continuous physical surface rather than a small unit floating
on grey. The empty band above the `DAC PRESCALER` knob is the natural
home for a stylised mode wordmark (e.g. `RETRO DECIMATOR` in IBM Plex
Mono Bold) — implementation detail, tuned at render time.

**Deliberate divergences from PCM2612** (recorded so they don't get
re-litigated during implementation):

1. **Output Filtering switch lives in the header**, not on the D
   chassis. PCM2612 places its `CRYSTAL CLEAR / LEGACY` switch on the
   unit itself; v2 promotes it to the header for cross-mode
   consistency ([ADR-0024](adr/0024-hardware-filter-toggles.md)).
2. **Ladder Effect toggle is added**, even though PCM2612 does not
   expose a Ladder switch at all. The YM2612 ladder DAC nonlinearity
   is part of the "Genesis sound" downstream of the same DAC path D
   mode emulates, so the toggle stays useful here
   ([ADR-0024](adr/0024-hardware-filter-toggles.md)).

---

## 5. Preset browser (modal overlay)

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
- **`+ Add Folder…`** — registers a custom root (view 10 native chooser).
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
- A load failure raises a notification toast (view 8); it never blocks.

The v1 main-window LCD lists (INSTRUMENTS / PRESETS / IMPORT in the
center/right columns) are **removed** — this browser is the only patch
navigator.

---

## 6. Settings (modal overlay)

Global plugin preferences. Opened from the header gear icon.

```
┌─ SETTINGS ──────────────────────────────────────────────────── [X] ┐
│  HARDWARE STRICT (FM)   [ off ]                                      │
│  UNISON DETUNE (FM)     [▌  0 ¢ ▐]   0..50 cents                     │
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

- `HARDWARE STRICT` (FM mode) — opt-in authenticity toggle modelled on
  the RYM2612 manual's *For the Purists* page (p. 13). When **on**:
  clamps `poly_voices` to 6 (the YM2612's hardware channel count);
  restricts `FLOAT_MUL` / `AUTO_RETRIG` to one voice at a time
  (additional voices using those modes silently fall back to
  `INT_MUL`); forces `output_filter` and `ladder_effect` on and locks
  their header toggles. Off by default — Gen VST's extended polyphony
  and free filter switches are the expected starting point. Bound to
  apvts param `hardware_strict`.
- `UNISON DETUNE` (FM mode) — small detune spread in cents applied to
  voices triggered by the same MIDI note. 0 ¢ = off (default); up to
  50 ¢ = pronounced unison. Bound to apvts param `unison_detune_cents`.
  This replaces the v2-draft UNISON sub-mode on the FM panel; the
  effect now layers freely with the numeric `POLY` count.
- `UI SCALE` — integer presets ([ADR-0017](adr/0017-hidpi-display-scaling.md)).
- `VELOCITY → TL` — FM mode velocity → TL scaling toggle.
- `AFTERTOUCH` — channel pressure routing: Off / LFO depth / Carrier TL.
  Default = **LFO depth (PMS)**.
- `TOOLTIPS` — global hover-tooltip toggle.
- `ABOUT / CREDITS…` opens view 7.
- `RESET ALL TO DEFAULTS` — destructive button (red label). After a
  confirmation modal, snaps every parameter to its `juce::AudioParameter`
  default and clears the active patch path.

Voice count and pitch-bend range that previously lived here have been
**promoted to the FM mode panel** — `POLY` is a numeric stepper next to
the LFO block, `RANGE` is the pitch-bend semitones stepper beside it.
The Settings surface is now reserved for global plugin preferences
that don't belong on the performance chassis.

The v1 `MIDI ROUTING…` button is **removed** (no routing matrix in v2).

---

## 7. About / credits (modal overlay)

A modal carrying the version and the **license attributions**. The
project is GPL v3 and bundles third-party code and data, so this surface
is legally required, not optional.

The version string (e.g. `v0.2.0`) is **only surfaced here** — the
former bottom status bar that carried it was removed during the
post-mockup review. Users open the About modal from the gear → ABOUT
path or by clicking the header wordmark.

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

## 8. Notification toast

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

## 9. WebView fallback panel (native, non-WebView)

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

## 10. Native file choosers

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

Views 5–7 are in-WebView modal overlays and share this behaviour:

- Open over a **dimmed** main UI; only one modal is open at a time.
  View 7 is opened *from* view 6 and replaces it.
- Dismissed by `Close`, the `[X]`, or the `Esc` key.
- Modal while open: clicks outside the modal panel do not reach the
  main UI.
- The notification toast (view 8) may still appear above an open modal.
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
  clip LED) — replaced by the persistent header L/R meters integrated
  into view 1.
- **Bottom status bar** (v2 first-pass view 5) — removed during the
  post-mockup review. The L/R output meters moved into the header
  cluster; the version string moved into the About modal (view 7) only.
- **Section tabs** (v1 — FM/SQ/D pills as a section selector inside the
  bottom region) — replaced by the **header mode selector** that also
  swaps the full mode panel.
- **Reset-part button** (v1) — replaced by `RESET ALL TO DEFAULTS` in
  Settings (whole-instance, since there's nothing finer than the
  instance).
- **Per-instrument routing strip** (v1) — removed entirely.
