# Task 11 — Main FM view & channel paging

> **Depends on:** Task 05, Task 10.
> **Design references:** `docs/genny-ui.md` (primary visual spec),
> `docs/design/08-ui-views.md` (view 1), `docs/design/05-ui-ux.md` (*FM channel
> paging*, *Component Inventory*, *Native functions*),
> `docs/design/02-fm-synthesis.md` (*FM Algorithms*, *Envelope Generator*),
> ADR-0013.

## Objective

Assemble the **complete FM editing screen** — the plugin's primary view — from
the Task 10 widgets plus the FM-specific widgets, and wire **FM-channel paging**
so the one-part-at-a-time editor edits all 6 multitimbral parts.

## Context & key constraints

- The full layout is specified in `genny-ui.md` (authoritative visual spec) and
  `08-ui-views.md` view 1. Build every region: header (wordmark, VU, 7-segment
  patch display, oscilloscope, voice LEDs, clip LED, gear icon), left column
  (LFO/AMS/FMS knobs + readouts, ALGORITHM 1–8 buttons + diagram, FEEDBACK
  knob), center column (INSTRUMENTS `lcd-list`, FM/SQ/D pills, CHANNELS 1–6,
  MIDI/TRANSPOSE/RNG/DEL/PAN stack), right column (PRESETS/IMPORT tabs +
  `lcd-list`s), and the bottom row of **four operator panels** (badge,
  ADSR graph, 5 knobs `ATK DR1 SUS DR2 RR`, 4 sliders
  `DETUNE FREQ ENV-SCALE LFO/SSG`).
- **FM-specific widgets built in this task:** `seg-display` (7-segment patch
  name), `algo-buttons` (8 numbered, selected wrapped in a red ring),
  `algo-diagram` (the 8 YM2612 routings from `02-fm-synthesis.md` *FM
  Algorithms*, redrawn on ALG change, carriers vs modulators colored
  differently), `adsr-graph` (per-operator envelope curve), `operator-panel`
  (the composite), and the canvas-drawn **"GEN VST" wordmark**.
- **`adsr-graph` is computed analytically in JS** from the five envelope values
  — no C++ round-trip; it redraws on any envelope `valueChangedEvent`.
- **FM-channel paging** (`05-ui-ux.md` *FM channel paging*): the UI edits one
  part at a time. FM-part-scoped relays are named **without** the `_part<n>`
  suffix (e.g. `atk_op1`). `selectChannel(n)` is a native function → the editor
  rebuilds the FM attachments so each FM relay re-binds to part `n`'s `apvts`
  parameter → rebinding pushes the new values → JS repaints every FM knob,
  slider, LED and the algorithm diagram in one batch. Global relays bind once
  and never rebind.
- `selectSection(s)` is a native function that swaps the bottom region between
  FM / SQ / D. Build the **FM** region here; SQ and D are Task 13 — leave the
  pill + a section-switch scaffold in place.
- The header's **oscilloscope, VU meter, voice-activity LEDs and clip LED are
  static placeholders** in this task — Task 12 feeds them live telemetry.
- The center column's **per-part polyphony controls** (view 10) are a layout
  placeholder here — Task 15 adds the live POLY/MONO/UNISON controls.
- The 7-segment patch display shows the selected part's current patch name.

## Scope

- The full FM view layout and styling per `genny-ui.md` / view 1.
- The FM-specific widgets listed above.
- Every FM control bound to its `apvts` parameter via the Task 10 binding layer.
- `selectChannel` paging: native function + attachment rebuild + batch repaint.
- The `algo-diagram` for all 8 algorithms with carrier/modulator coloring.
- The `selectSection` scaffold (FM region live; SQ/D placeholder).

## Out of scope

- Live telemetry for the oscilloscope/VU/voice-LED/clip widgets → Task 12.
- SQ and D section contents → Task 13. Modals → Task 13.
- The live polyphony controls → Task 15.
- The patch browser modal → Task 14 (the INSTRUMENTS/PRESETS lists here are the
  quick-access lists; the folder icon opening the modal is Task 14).

## Implementation steps

1. Build the four-region FM layout, replacing Task 03's static placeholders
   with the real structured layout per `genny-ui.md`.
2. Implement the FM-specific widgets (`seg-display`, `algo-buttons`,
   `algo-diagram`, `adsr-graph`, `operator-panel`, the wordmark).
3. Bind every FM control to its parameter; bind global controls once.
4. Implement `selectChannel` paging end-to-end (native function + editor
   attachment rebuild + JS batch repaint).
5. Wire the `algo-diagram` and `adsr-graph` to redraw on `valueChangedEvent`.
6. Add the `selectSection` native function and the FM/SQ/D pills (FM live).

## Deliverables

`ui/src/views/fm-view.*`, `ui/src/widgets/*` (the FM-specific widgets),
updates to `ui/index.html`, `ui/src/*` entry/layout, and
`src/PluginEditor.{h,cpp}` (the `selectChannel` / `selectSection` native
functions and the FM attachment rebuild).

## Verification

1. Build + load in a DAW. The FM view visually matches `genny-ui.md` — dark
   chassis, green LCD insets, blue knobs, red readouts, the four operator
   panels aligned on a shared baseline grid.
2. Every FM knob, slider, button and selector edits the **correct** `apvts`
   parameter (cross-check against the DAW automation lane) and audibly changes
   the sound.
3. CHANNELS 1–6: selecting a different channel **re-pages** the editor to that
   part — every FM control repaints to that part's values in one batch; editing
   then affects that part only. Editing part 2 does not disturb part 1.
4. Clicking an ALGORITHM button changes the `alg` parameter and **redraws the
   algorithm diagram**, recoloring carriers vs modulators.
5. Moving an envelope knob redraws that operator's ADSR graph immediately
   (analytic, no audible/visible C++ round-trip lag).
6. The 7-segment display shows the current part's patch name.
7. `pluginval --strictness-level 8` passes; editor open/close is clean.

## Done when

- [ ] The full FM view is built and matches the `genny-ui.md` spec.
- [ ] All FM controls are bound and audibly correct.
- [ ] FM-channel paging switches parts and batch-repaints correctly.
- [ ] The algorithm diagram and ADSR graphs redraw live.
- [ ] `pluginval` passes.
