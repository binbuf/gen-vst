# Task 25 — Header polish: True Stereo toggle & meter restyle

> **Depends on:** Task 22 (rack model); Tasks 12 (telemetry) and 11 (header layout) for the existing meter bay.
> **Design references:** `docs/genny-ui.md` (header bar), `docs/design/08-ui-views.md` (view 1 header meter bay).

## Objective

Match Genny's header strip: wordmark + small **TRUE STEREO** label with toggle indicator + LED activity bar + full-width 7-seg patch-name display. The current header already has the meter bay and 7-seg display; this task adds the True Stereo control and aligns visuals with the Genny screenshots.

## Context & key constraints

- **True Stereo toggle** is a global apvts toggle (`true_stereo`, default `on`). When off, `processBlock` sums L+R to mono before write-out.
- **Visual indicator** matches Genny's compact `X` glyph + small status dot next to the "TRUE STEREO" label.
- **Voice-activity LED row** (16 LEDs, present since Task 12) stays. Clip LED stays. No telemetry-rate change.
- **Don't touch the 7-seg display** beyond verifying its bracket glyphs (those land in Task 26 polish).
- **Mono sum** is a one-liner in `processBlock`: when `true_stereo == false`, `out.copyFrom(left + right) * 0.5` to both channels.

## Scope

- `src/PluginProcessor.cpp` — new `true_stereo` apvts toggle; mono-sum in `processBlock`.
- `ui/index.html` — insert a small TrueStereo cell between `#wordmark` and `.meter-bay`.
- `ui/src/widgets/true-stereo-toggle.js` — new canvas widget: label "TRUE STEREO", an `X`/arrow glyph that toggles, a small red status dot.
- `ui/src/views/fm-view.js` — instantiate the toggle, bind via `bindToggle('true_stereo')`.
- `ui/src/styles/chassis.css` — sizing/positioning for the new cell.

## Out of scope

- A real M/S decoder; mono sum is acceptable per Genny.
- Animating the LED bar beyond what telemetry already drives.
- Header bracket glyphs around the 7-seg display (Task 26).

## Implementation steps

1. **Engine** — add `true_stereo` param in `createParameterLayout()`; read in `processBlock`; on `false`, sum to mono.
2. **Widget** — `true-stereo-toggle.js` with click-to-toggle, pixel-art `X`/arrow swap, red dot indicator.
3. **Wire-up** — instantiate in `fm-view.js`; bind via `binding.js`.
4. **Styles** — position the new cell; verify the meter bay still fits at 960px.

## Deliverables

- `src/PluginProcessor.cpp` — new param, mono-sum branch.
- `ui/index.html` — new cell.
- `ui/src/widgets/true-stereo-toggle.js` — new.
- `ui/src/views/fm-view.js` — wiring.
- `ui/src/styles/chassis.css` — minor.

## Verification

1. Build succeeds; `ctest` all green (no new tests required unless the toggle exposes a testable engine state — a tiny test for `processBlock` mono-sum is welcome but optional).
2. Standalone:
   - Toggle TRUE STEREO off; play a stereo source → both output channels are identical (sum of L+R, halved).
   - Toggle on → stereo restored.
   - Voice-activity LEDs still pulse per voice; clip LED still flashes on overload.
3. Save preset, reload → toggle state persists.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] TRUE STEREO toggle cell is visible in the header.
- [ ] Toggling collapses output to mono and back.
- [ ] State persists.
- [ ] Header layout still fits 960px.
- [ ] `pluginval` SUCCESS.
