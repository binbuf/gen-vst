# Task 07 — D panel

> **Milestone:** D mode plays — audio routed into the plugin processes
> through the full PCM2612-style D panel UI; PRESCALER, MONO, DRY/WET,
> and stereo input level meters all live.
> **Depends on:** Task 04 (widget library) and Task 03 (D-mode DSP).
> **Design references:** `docs/design/08-ui-views.md` view 4 (primary —
> centered decimator knob + meters + MONO + DRY/WET),
> `docs/design/07-feature-spec.md` (*D Mode Specification*),
> `docs/design/01-architecture.md` (*Render Pipeline — D mode*),
> ADR-0021.

## Objective

Assemble the D mode panel from Task 04's widget library on top of the
DSP foundations from Task 03. The decimator knob, MONO toggle, DRY/WET
knob, and stereo input level meters all bind to apvts / telemetry.

This is the smallest of the three per-mode-panel tasks because the DSP
already works (Task 03 wired `prescaler` / `mono` / `dry_wet` straight
to apvts); this task is purely the UI assembly + the level-meter
telemetry hookup for the input side.

After this task: the user routes audio into the plugin in Reaper, sets
`mode_select = D`, and turns the decimator knob to hear the bit-crush
effect through a properly laid-out PCM2612-style panel.

## Context & key constraints

- **Layout** (`08-ui-views.md` view 4): centered large `decimator-knob`
  with PCM2612-style matte-black body. A stereo level-meter band sits
  below it with a centered `MONO` toggle between the L and R bars. A
  `DRY/WET` knob lives below the meter band. The wide chassis bands on
  either side of the centered column carry the same brushed-metal
  treatment as the rest of the v2 chassis, with an optional
  `RETRO DECIMATOR` wordmark filling the empty space above the
  decimator knob.
- **No MIDI controls.** D mode ignores MIDI (`07-feature-spec.md`).
- **No sample loader, no WAV button.** ADR-0021 — D mode is audio FX,
  not a sampler.
- **Level meters are input-side** — they show the audio coming into
  the plugin (pre-decimation). Telemetry already pushes `peakL`,
  `peakR` from the audio thread (Task 03); for D mode the values
  should reflect the **input** bus's peaks, not the output. Add an
  `inputPeakL` / `inputPeakR` field to `Telemetry` and update
  `processBlock`'s D-mode path to write them; the event push surfaces
  both. The two output-side meter peaks live in the header status bar
  (Task 08).
- **DAC PRESCALER knob** is the `decimator-knob` variant (96 px,
  matte-black, no top sheen) from Task 04.
- **DRY/WET knob** is the regular `knob` widget at default size.
- **MONO toggle** is the `toggle-switch` widget; lit when on.
- The Output Filter + Ladder Effect toggles live in the **header**
  (Task 08), not on this panel — view 4 explicitly notes this divergence
  from the PCM2612 hardware artwork.

## Scope

- New `ui/src/views/d-view.js`. Builds the panel HTML per view 4; mounts
  and binds:
  - `DAC PRESCALER` decimator-knob → `prescaler`.
  - `MONO` toggle → `mono`.
  - `DRY/WET` knob → `dry_wet`.
  - Stereo input level-meter band — two `level-meter` widgets bound to
    the `inputPeakL` / `inputPeakR` fields on the `meterData` event.
- Optional centered `RETRO DECIMATOR` text on the chassis above the
  knob; pure HTML/CSS (no separate font asset).
- `main.js` mounts `d-view` when `mode_select == D`.
- C++:
  - Extend `Telemetry` with `inputPeakL` / `inputPeakR` (lock-free
    atomics, same pattern as `peakL` / `peakR`).
  - In D-mode `processBlock`, before the decimator runs, write the
    pre-process L/R peaks to the new telemetry fields.
  - Extend the editor's `meterData` event payload to include the
    input peaks: `{ peakL, peakR, inputPeakL, inputPeakR, noteOn }`.

## Out of scope

- `.gdac` preset format + the tagged preset browser — Task 09.
- The `RETRO DECIMATOR` wordmark as a custom font / SVG. Keep it
  plain — IBM Plex Mono Bold per `09-visual-spec.md` typography table.
- Output Filter / Ladder Effect toggles on the panel — they live in
  the header in v2 (Task 08).
- Any sample-loading or MIDI-trigger surface (ADR-0021).

## Implementation steps

1. **`Telemetry` extension**:
   - Add `std::atomic<float> inputPeakL{0.f}, inputPeakR{0.f};`.
   - Add `void recordInputPeaks(float l, float r) noexcept;` that
     stores the values (a simple max-with-decay is fine; or just the
     latest peak per ~33 ms window — match the existing output-meter
     pattern).
2. **`processBlock` D-mode path**: before running the input-copy or
   decimator, compute L / R peak over the block and call
   `telemetry.recordInputPeaks(...)`. FM and SQ modes can leave the
   input peaks at 0 — the meters then sit at 0 in those modes (or
   the SQ / FM panels could ignore the input-peak fields entirely
   since they don't render input meters).
3. **Editor `meterData` event**: extend the payload to include
   `inputPeakL` / `inputPeakR`. The level-meter widget gains the
   ability to subscribe to a specific field on the event (or two
   meters are subscribed to `inputPeakL` and `inputPeakR`
   respectively via an `eventField` option on the widget).
4. **`ui/src/views/d-view.js`** — build the panel HTML:
   - A centered column with the decimator-knob mounted via
     `mount(host, { id: 'prescaler', binding: bindSlider('prescaler') })`.
   - A horizontal band below it with two `level-meter` widgets and the
     `MONO` toggle between them. The level meters subscribe to
     `inputPeakL` / `inputPeakR`.
   - A `DRY/WET` knob below the meter band.
   - Optional `RETRO DECIMATOR` heading above the decimator knob,
     styled with the wordmark token from `design-system.css`.
5. **`main.js`** — mount `d-view` on `mode_select == D` (the
   FM/SQ/D mode switch already implemented in Tasks 05–06).

## Deliverables

- New `ui/src/views/d-view.js`.
- Updated `ui/src/main.js` (mode-dispatch case for D).
- Updated `src/Telemetry.{h,cpp}` (input-peak fields).
- Updated `src/PluginProcessor.cpp` (D-mode path writes input peaks).
- Updated `src/PluginEditor.cpp` (event payload includes input peaks).
- Optionally update `ui/src/widgets/level-meter.js` if a new
  `eventField` option is needed.

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green.
2. **Reaper smoke test**:
   - Audio track with white noise as the source.
   - Insert the VST3 on a follow-up track with the audio routed from
     the first track as input.
   - Set `mode_select = D`. The D panel renders per view 4.
   - The two stereo level meters bounce with the input signal.
   - Turn `DAC PRESCALER` from 0 to 0.7 — the output is audibly
     decimated; sweeping the knob smoothly increases the crush.
   - Toggle `MONO` on — both meters track the L+R sum (you can verify
     by panning the source hard L; the R meter should now equal the
     L meter when MONO is on).
   - Turn `DRY/WET` to 0 — only the original input passes; to 1 — only
     the decimated signal passes; halfway — both blend audibly.
3. **`mode_select` round-trip** — switch FM → D → SQ → D. The D panel
   re-mounts cleanly each time; the meter values reset to 0 on entry
   and resume bouncing within ~30 ms.
4. **`output_filter` / `ladder_effect` parity** — toggling those
   global params from the host's generic editor audibly changes the
   D output (Task 03 already wired them through; verify they're not
   broken).
5. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.

## Done when

- [ ] D panel renders per `08-ui-views.md` view 4.
- [ ] DAC PRESCALER, MONO, DRY/WET all bind and audibly affect the
      decimator behaviour.
- [ ] Stereo input level meters track the input bus's L / R peaks.
- [ ] No regressions in FM or SQ modes (the new telemetry fields are
      additive).
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
