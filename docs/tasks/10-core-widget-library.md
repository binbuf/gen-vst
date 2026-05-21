# Task 10 — Core interactive widget library

> **Depends on:** Task 03.
> **Design references:** `docs/design/05-ui-ux.md` (primary — *Component
> Inventory*, *Rendering & Asset Strategy* incl. *5×7 Dot-Matrix Readouts*,
> *Parameter binding*), `docs/genny-ui.md` (*Interaction Details Worth
> Encoding*), ADR-0017.

## Objective

Build the **reusable Canvas-drawn pixel-art widgets** that every view is
composed from, the JS helper layer that binds them to JUCE parameter relays,
and a **widget gallery** dev page to develop and verify them in isolation.

## Context & key constraints

- All custom widgets are **Canvas 2D**, drawn under the binding pixel-art rules
  established in Task 03 (`05-ui-ux.md` *Pixel-Art Style Rules*): 1× authoring
  grid, `imageSmoothingEnabled = false`, no `border-radius`, hard-border bevels,
  no gradients (ordered dithering), 8px grid. Colors come from the
  `genny-ui.md` palette CSS custom properties — never ad-hoc hex.
- **Parameter binding** (`05-ui-ux.md` *Parameter binding*):
  - knob / slider → `WebSliderRelay` + `WebSliderParameterAttachment`
  - toggle / LED button → `WebToggleButtonRelay` + `WebToggleButtonParameterAttachment`
  - selector / combo → `WebComboBoxRelay` + `WebComboBoxParameterAttachment`
  The relay **name equals the `apvts` parameter ID**. On the JS side, a widget
  subscribes to its relay's `valueChangedEvent` — that event drives all live
  redraws.
- **Knob behaviour** (`genny-ui.md` *Interaction Details*): blue skeuomorphic
  rotary, ~270° sweep, rest at 7 o'clock; vertical click-drag (up = increase);
  `Shift` = fine; double-click = reset to default.
- **`led-readout` is not a font.** It is a 5×7 dot-matrix widget drawn from a
  built-in glyph table — the full spec (glyph table, `DOT_SIZE`/`DOT_PITCH`,
  right-alignment, lit + unlit dot rendering, optional bloom) is in `05-ui-ux.md`
  *5×7 Dot-Matrix Readouts*. Supported glyphs: digits `0–9`, letters `O`/`F`
  (for `OFF`), `-`, and blank. The unlit dot grid stays faintly visible.
- The UI scales only by **integer** factors (ADR-0017) — widgets must stay
  crisp at 1×/2×/3×. (The scale-selection mechanism itself is Task 17.)

## Scope

The **core reusable** widgets (the view-specific ones are built with their
views):

- `knob` — blue rotary; drag / shift-fine / double-click-reset.
- `slider` — horizontal groove + chunky blue cap, with an attached `led-readout`.
- `led-readout` — the 5×7 dot-matrix value display (glyph table + render
  algorithm from `05-ui-ux.md`).
- `step-field` — numeric value with up/down arrows (used for MIDI ch, transpose).
- `toggle` / LED button — on/off control.
- `section-tabs` / `pill-buttons` — segmented selectors (FM/SQ/D, PRESETS/IMPORT).
- `lcd-list` — green-LCD scrollable list with a pixel scrollbar and inverse-video
  selection (used by Instruments, Presets, and the patch browser).

Plus:

- A small JS helper layer for relay creation + two-way binding so each widget
  binds in one call.
- A **widget gallery** dev page showing every widget bound to scratch
  parameters, for isolated development and verification.

## Out of scope

- View-specific widgets — `seg-display`, `vu-meter`, `algo-diagram`,
  `algo-buttons`, `adsr-graph`, `oscilloscope`, `operator-panel`, the canvas
  wordmark → Task 11.
- `notification-toast` → Task 13. The DAC `waveform-display` → Task 13.
- Assembling widgets into the real views → Tasks 11/13.

## Implementation steps

1. Build the relay-binding JS helper layer over the `window.__JUCE__` module.
2. Implement each core widget as a Canvas component honoring the pixel-art
   rules; wire each to its relay type and `valueChangedEvent`.
3. Implement `led-readout` strictly per the `05-ui-ux.md` 5×7 algorithm.
4. Build the widget gallery page (reachable in the dev-server build) with every
   widget bound to scratch `apvts` parameters.

## Deliverables

`ui/src/widgets/*` (one module per widget), `ui/src/binding.js` (or similar
relay helper), `ui/src/gallery.*` (the dev gallery page), updates to
`ui/index.html` / the editor relay registration as needed for the scratch
params.

## Verification

1. Dev-server build (`-DGENVST_DEV_SERVER=ON`, `npm run dev`): open the widget
   gallery — every widget renders, pixel-snapped, with no anti-aliasing, no
   rounded corners, no blurred shadows; colors match the `genny-ui.md` palette.
2. Each bound widget is **two-way**: changing it moves its scratch parameter
   (visible in the DAW automation lane), and changing the parameter from the DAW
   repaints the widget.
3. Knob: vertical drag changes value; `Shift`-drag is finer; double-click
   resets to default; the indicator dot sweeps ~270° and rests at 7 o'clock.
4. `led-readout`: shows right-aligned digits, `OFF`, and negative values; the
   unlit dot grid is visible behind lit characters.
5. `lcd-list`: scrolls with the pixel scrollbar; the selected row is
   inverse-video.
6. Set window scale to 1×, 2×, 3× — every widget stays crisp (no fractional
   blur).

## Done when

- [ ] All core widgets implemented as Canvas pixel-art components.
- [ ] Each binds two-way through the correct relay/attachment type.
- [ ] `led-readout` follows the 5×7 dot-matrix spec exactly.
- [ ] The widget gallery renders and exercises every widget.
- [ ] Widgets stay crisp at integer scales 1×/2×/3×.
