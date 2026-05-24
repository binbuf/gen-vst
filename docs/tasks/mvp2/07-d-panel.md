# Task 07 — D panel

> **Milestone:** D mode plays — audio routed into the plugin processes
> through the D panel UI; DRY/WET and MONO bind to apvts; the header's
> mode-aware DAC PRESCALER drives decimation in this mode.
> **Depends on:** Task 04 (widget library) and Task 03 (D-mode DSP).
> **Design references:** `docs/design/08-ui-views.md` view 4 (primary —
> spartan DRY/WET + MONO panel; prescaler / meters / filter switches
> all live in the header), `docs/design/07-feature-spec.md`
> (*D Mode Specification*), `docs/design/01-architecture.md`
> (*Render Pipeline — D mode*), ADR-0021.

## Objective

Assemble the D mode panel from Task 04's widget library on top of the
DSP foundations from Task 03. Only two controls live on the panel —
the large central `DRY/WET` knob and the `MONO` toggle. Everything
else (prescaler, output meters, filter / ladder toggles) is on the
header (Task 08).

This is the smallest of the three per-mode-panel tasks because the DSP
already works (Task 03 wired `prescaler` / `mono` / `dry_wet` straight
to apvts) and most of the surface area moved to the header — this task
is purely the two-control panel + the mode-dispatch hook.

After this task: the user routes audio into the plugin in Reaper, sets
`mode_select = D`, turns the header's DAC PRESCALER knob to hear the
bit-crush effect, and tweaks DRY/WET + MONO on the panel.

## Context & key constraints

- **Layout** (`08-ui-views.md` view 4): centered large knob (uses the
  96 px `decimator-knob` body variant for visual continuity) bound to
  `dry_wet`, with a `MONO` toggle beneath it. The wide chassis bands
  on either side carry the same brushed-metal treatment as the rest of
  the v2 chassis, with an optional `RETRO DECIMATOR` wordmark filling
  the empty space above the knob.
- **No prescaler control on this panel.** The DAC PRESCALER knob lives
  in the header (Task 08); it binds `prescaler` in D mode and
  `fm_dac_prescaler` in FM mode. The D panel must not duplicate it.
- **No input-side level meters on this panel.** Signal-presence
  feedback is covered by (a) the header NOTE ON LED's "input audio
  exceeds threshold" behaviour in D mode (Task 03 already wires this)
  and (b) the DAW's native track-level input meter. The header's L/R
  output meters cover post-master output level in all modes.
- **No MIDI controls.** D mode ignores MIDI (`07-feature-spec.md`).
- **No sample loader, no WAV button.** ADR-0021 — D mode is audio FX,
  not a sampler.
- **DRY/WET knob** uses the 96 px `decimator-knob` variant — same
  visual recipe the central PCM2612 knob always used; the parameter
  binding is the only thing that changed (now `dry_wet`, was
  `prescaler`).
- **MONO toggle** is the `toggle-switch` widget; lit when on.
- The Output Filter + Ladder Effect toggles live in the **header**
  (Task 08), not on this panel — view 4 explicitly notes this divergence
  from the PCM2612 hardware artwork, along with the prescaler move.

## Scope

- New `ui/src/views/d-view.js`. Builds the panel HTML per view 4; mounts
  and binds:
  - Central `DRY/WET` knob (decimator-knob variant) → `dry_wet`.
  - `MONO` toggle → `mono`.
- Optional centered `RETRO DECIMATOR` text on the chassis above the
  knob; pure HTML/CSS (no separate font asset).
- `main.js` mounts `d-view` when `mode_select == D`.
- No C++ changes — Task 03 already covers the D-mode DSP, telemetry
  threshold for NOTE ON, and output-meter peaks; Task 08 covers the
  header's mode-aware DAC PRESCALER binding.

## Out of scope

- Any D-mode preset format — D has none ([ADR-0025](../../design/adr/0025-tagged-preset-browser.md));
  the 3 apvts params persist via the host's project state. The
  tagged preset browser (Task 09) only handles `.psg` + FM formats.
- The header DAC PRESCALER mode-switching binding — Task 08.
- The `RETRO DECIMATOR` wordmark as a custom font / SVG. Keep it
  plain — IBM Plex Mono Bold per `09-visual-spec.md` typography table.
- Output Filter / Ladder Effect toggles on the panel — they live in
  the header in v2 (Task 08).
- Input-side level meters / `inputPeakL` / `inputPeakR` telemetry —
  removed from v2 scope per view 4. The header NOTE ON LED + the
  DAW's native input meter cover the signal-presence question.
- Any sample-loading or MIDI-trigger surface (ADR-0021).

## Implementation steps

1. **`ui/src/views/d-view.js`** — export `mountDView(host)` and
   `unmountDView()`. Build the panel HTML:
   - A centered column with the decimator-knob mounted via
     `mount(host, { id: 'dry_wet', binding: bindSlider('dry_wet') })`.
     Apply the `decimator-knob` body class for the larger 96 px variant.
   - A `MONO` toggle below it, mounted via
     `mount(host, { id: 'mono', binding: bindToggle('mono') })`.
   - Optional `RETRO DECIMATOR` heading above the decimator knob,
     styled with the wordmark token from `design-system.css`.
2. **`main.js`** — mount `d-view` on `mode_select == D` (the
   FM/SQ/D mode switch already implemented in Tasks 05–06). Unmount
   the previous panel cleanly on mode change.

## Deliverables

- New `ui/src/views/d-view.js`.
- Updated `ui/src/main.js` (mode-dispatch case for D).
- No new C++ files (DSP + telemetry + bindings exist already).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green.
2. **Reaper smoke test**:
   - Audio track with white noise as the source.
   - Insert the VST3 on a follow-up track with the audio routed from
     the first track as input.
   - Set `mode_select = D`. The D panel renders per view 4 — just the
     central DRY/WET knob and MONO toggle, nothing else on the chassis.
   - Turn the **header** DAC PRESCALER from 0 to 0.7 — the output is
     audibly decimated; sweeping the header knob smoothly increases
     the crush (header widget is binding the D-mode `prescaler` param
     here, per Task 08).
   - Toggle `MONO` on — both header L/R output meters track the L+R
     sum (you can verify by panning the source hard L; the R meter
     should now equal the L meter when MONO is on).
   - Turn `DRY/WET` to 0 — only the original input passes; to 1 — only
     the decimated signal passes; halfway — both blend audibly.
   - The header `NOTE ON` LED lights while audio is present on the
     input (per Task 03's D-mode threshold).
3. **Mode-aware header prescaler** — switch `mode_select` from D to
   FM. The header DAC PRESCALER knob's value snaps to the active FM
   patch's `fm_dac_prescaler` (which is a different param). Switch
   back to D — it snaps back to the D-mode `prescaler` value. The two
   never bleed into each other.
4. **`mode_select` round-trip** — switch FM → D → SQ → D. The D panel
   re-mounts cleanly each time.
5. **`output_filter` / `ladder_effect` parity** — toggling those
   global params from the header audibly changes the D output
   (Task 03 wired them through; Task 08 wires the header toggles).
6. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.

## Done when

- [ ] D panel renders per `08-ui-views.md` view 4 — DRY/WET (large
      central) + MONO only; no panel-side prescaler, no panel-side
      level meters.
- [ ] DRY/WET and MONO both bind and audibly affect the output.
- [ ] The header DAC PRESCALER (wired in Task 08) drives the D-mode
      decimator when `mode_select == D`.
- [ ] No regressions in FM or SQ modes.
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
