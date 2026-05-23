# Task 26 — Genny visual polish & algorithm-diagram audit

> **Depends on:** Tasks 22, 23, 24, 25 — all preceding parity work landed.
> **Design references:** `docs/genny-ui.md`, `docs/design/08-ui-views.md`, the screenshots at `Screenshot 2026-05-23 094*.png`.

## Objective

Close the remaining cosmetic + correctness gaps observed in the 16 Genny screenshots. Specifically:

- Confirm the **algorithm diagram** redraws correctly for all 8 YM2612 routings with carriers highlighted.
- A color/palette pass to match Genny's chassis/LCD/LED/knob tones exactly.
- Add the small **bracket glyphs** around the 7-seg patch-name display.
- **DAC bottom-panel restyle** (Genny layout — HZ knob, LEV slider, DAC label, placeholder note-grid backdrop). Engine stays single-sample; multi-sample is deferred to a new Task 29 stub.
- Side-by-side punch-list comparison with the screenshots.

## Context & key constraints

- **Single-sample DAC stays.** This task only updates the panel's *visual layout*; no engine work. The note-grid is rendered as a 4×5 read-only backdrop with the prompt text "Click Note To Load Sample" sitting over it. Clicking a cell shows a "Multi-sample DAC coming soon — see Task 29" toast.
- **Algorithm diagram routing** is canonical YM2612 (8 algorithms; carriers are the operators that sum to output). Cross-reference plutiedev's YM2612 algorithm table for the exact carrier mask per algorithm.
- **Palette is the source of truth in `design-system.css`** — if a discrepancy exists between widget code and the CSS variable, fix the widget.
- **Bracket glyphs** are the small `▌` / `▐` decorations flanking the 7-seg display per `08-ui-views.md` header diagram.
- **Punch list deliverable.** This task may surface small leftover deltas that don't fit. Capture them in `docs/tasks/30-cosmetic-cleanup.md` rather than silently dropping.

## Scope

- `ui/src/widgets/algo-diagram.js` — audit + fix any algorithm routings that don't match the YM2612 table; ensure carrier highlighting works for all 8.
- `tests/AlgoMappingTests.cpp` (if a test target exists for UI-adjacent C++ data) — or a new pure-JS test under `ui/tests/` if the routing table lives on the JS side — verify algorithm → carrier-mask matches the canonical YM2612 reference.
- `ui/src/styles/design-system.css` — palette audit; reconcile any drift.
- `ui/src/widgets/seg-display.js` + `ui/index.html` — bracket glyphs flanking the 7-seg.
- `ui/src/views/d-view.js` — restyle to Genny layout: HZ knob (replace the 8000/11025/22050 stepper with a continuous knob bound to the existing rate param; clamp/snap if engine still requires fixed rates), LEV slider, "DAC" branding label, 4×5 note-grid backdrop, "Click Note To Load Sample" prompt overlay, click handler that toasts the deferral message.
- `docs/tasks/29-dac-multisample.md` — new stub task file with the multi-sample DAC plan referenced from the screenshots.
- `docs/tasks/30-cosmetic-cleanup.md` — new only if the side-by-side leaves remaining deltas.

## Out of scope

- Any engine change (DAC stays single-sample; algorithm carrier masks are read-only).
- New widgets beyond the bracket glyphs and the note-grid backdrop.
- Drag-resizing or scale changes — fixed 960×640 stays per ADR-0007.

## Implementation steps

1. **Algorithm audit** — step through algorithms 1–8 in the running plugin; for each, visually verify against the plutiedev YM2612 algorithm table and the Genny screenshots `094920`–`094958`. Fix any wrong routings in the carrier-mask table.
2. **Algorithm test** — add a small data test asserting the carrier mask for each of 8 algorithms.
3. **Palette pass** — diff `design-system.css` against `docs/genny-ui.md` Color Palette; fix any drift. Make sure no widget hard-codes a color that should reference the CSS var.
4. **Bracket glyphs** — extend `seg-display.js` (or wrap with a CSS pseudo-element) to draw `▌` left and `▐` right of the patch-name string.
5. **DAC restyle** — `d-view.js`:
   - Replace the rate stepper with a knob widget bound to the existing rate param (snap to 8000/11025/22050 internally; show interpolated label).
   - Replace single-WAV strip with a 4×5 grid backdrop using the note-label convention from the Genny screenshot `094629` (`C-3, C#-3, D-3, …`).
   - The grid is read-only; clicks anywhere emit the deferral toast.
   - Keep the existing LOAD WAV / CLEAR controls but visually subordinate them (small bottom strip) so the panel reads as Genny's multi-sample grid even though only the single-WAV path works.
6. **Side-by-side** — open the running standalone next to each screenshot; note any remaining visual delta.
7. **Stamp follow-ups** — write `docs/tasks/29-dac-multisample.md` (always); write `docs/tasks/30-cosmetic-cleanup.md` only if the side-by-side surfaces leftover items.

## Deliverables

- `ui/src/widgets/algo-diagram.js` — verified or fixed routings.
- `ui/src/widgets/seg-display.js` (or `ui/index.html` markup) — bracket glyphs.
- `ui/src/views/d-view.js` — restyled.
- `ui/src/styles/design-system.css` — palette reconciled.
- `tests/AlgoMappingTests.cpp` or `ui/tests/algo-mapping.test.js` — new.
- `docs/tasks/29-dac-multisample.md` — new.
- `docs/tasks/30-cosmetic-cleanup.md` — only if needed.

## Verification

1. Build + ctest all green.
2. Standalone — cycle through algorithm buttons 1–8: each diagram matches Genny's reference + the plutiedev carrier mask.
3. Standalone — 7-seg display flanked by the bracket glyphs.
4. Standalone — DAC panel matches Genny's layout (HZ knob, note grid, LEV slider, DAC label, prompt text).
5. Side-by-side screenshot vs `Screenshot 2026-05-23 094523.png`, `094629.png`, `094947.png` — small enough visual delta to mistake the running plugin for Genny at a glance.
6. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] All 8 algorithm diagrams match the canonical YM2612 routings.
- [ ] Palette + bracket glyphs match `genny-ui.md` and the screenshots.
- [ ] DAC panel reads as Genny's multi-sample layout (engine still single-sample).
- [ ] Task 29 (multi-sample DAC) stub stamped; Task 30 (cosmetic cleanup) stamped only if leftover deltas exist.
- [ ] `pluginval` SUCCESS.
