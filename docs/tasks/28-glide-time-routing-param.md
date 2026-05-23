# Task 28 — Glide time per-instrument routing parameter (DEL)

> **Depends on:** Task 22 (rack routing model + per-part params), Task 15 (mono/legato glide mode).
> **Design references:** `docs/genny-ui.md` (center column routing strip — "DEL" slider), `docs/design/07-feature-spec.md` (portamento/glide).
> **Note:** If Task 24 stamped a `28-scala-tuning.md`, renumber this task to 32 or the next available slot.

## Objective

Add a **glide time** (portamento time) slider to each instrument's routing strip, matching Genny's **DEL** slider in the center column. `docs/genny-ui.md` specifies DEL as "horizontal slider with a red readout `0`" alongside MIDI, TRANSPOSE, RNG, and PAN. Task 22 implemented all those controls except DEL. Genny's live parameter #45 is named "Delay" with value range 0+; this maps to portamento/glide time in ms.

Glide already works in mono/legato mode (Task 15) but there is no user-facing time control — the glide rate is instant. This task adds the `glide_time_partN` param and wires it into `VoiceAllocator`'s pitch-slide logic.

## Context & key constraints

- **Mono mode only.** Glide time has no effect when the part is in Poly mode. In Unison mode, apply the same glide to all stacked voices. The UI slider is always visible but the tooltip (or a disabled appearance) should clarify it only activates in Mono/Unison.
- **Units.** Express as milliseconds, range 0–2000 ms, default 0 (off / instant). At 0 ms the behavior is identical to the current implementation.
- **Pitch slide implementation.** On note-on in legato mode, `VoiceAllocator` should compute a per-block pitch increment from the current frequency to the target frequency such that the transition completes in `glide_time` ms. Use linear interpolation over the note period, updating the YM2612 F-number per block.
- **PSG and DAC glide.** PSG tone channels also support pitch slide (SN76489 frequency register is directly writable per block). Add `glide_time_psg_<ch>` params for the 3 tone channels. DAC has no pitch — omit.
- **Keep routing strip compact.** DEL slider should visually match the existing DETUNE and BALANCE sliders in the rack strip (same height, same readout style). Red LED readout showing ms value when non-zero, "OFF" at 0.

## Scope

- New APVTS params:
  - FM: `glide_time_part<1–6>` — `AudioParameterInt`, 0–2000 ms, default 0.
  - PSG tones: `glide_time_psg_<ch1,ch2,ch3>` — same range/default.
  - No DAC glide param.
- `src/VoiceAllocator.cpp` — on legato/mono note-on, if `glide_time > 0`, compute per-block F-number delta from previous note's frequency to target. Apply until target reached or next note-on.
- `src/SN76489Engine.cpp` — same linear interpolation for PSG tone frequency register writes when `glide_time_psg_chN > 0`.
- `ui/src/views/routing-controls.js` — add a DEL slider row to the per-instrument routing strip, below DETUNE and above BALANCE (matching Genny's visual order). Reuse the existing slider widget. Bind to `glide_time_partN` / `glide_time_psg_chN` by part type.
- `ui/src/styles/chassis.css` — minor height adjustment if the routing strip needs to grow.

## Out of scope

- Exponential or logarithmic glide curves (linear only for MVP).
- Glide in Poly mode (pitch overlap is undefined; defer to post-MVP).
- DAC pitch glide (DAC plays back a fixed-rate sample; no pitch register).
- Glide time in unison voice stacking beyond applying the same time to all voices.

## Implementation steps

1. **Params** — add `glide_time_partN` (FM, 6 entries) and `glide_time_psg_chN` (PSG, 3 entries) in `createParameterLayout()`.
2. **FM glide** — in `VoiceAllocator`: when `poly_mode == Mono` and `mono_glide == Legato` and `glide_time > 0`, record the previous voice's current F-number at note-on. Compute delta per block (sample-accurate target / `glide_time_samples`). In the block-render path, advance F-number toward target and write the YM2612 frequency registers each block until converged.
3. **PSG glide** — in `SN76489Engine::noteOn` for tone channels: same linear interpolation on the SN76489 frequency divider value, written per block.
4. **UI** — DEL row in `routing-controls.js`. Show `"OFF"` in the LED readout when value is 0; show ms value otherwise. Bind via the existing `bindSlider` pattern.
5. **Docs** — update `docs/design/07-feature-spec.md` portamento section: glide time range, units, and mono-only caveat.

## Deliverables

- `src/PluginProcessor.cpp` — 9 new glide-time params (6 FM + 3 PSG).
- `src/VoiceAllocator.cpp` + `.h` — per-block F-number interpolation.
- `src/SN76489Engine.cpp` — per-block frequency interpolation for tone channels.
- `ui/src/views/routing-controls.js` — DEL slider row.
- `ui/src/styles/chassis.css` — minor sizing if needed.
- `docs/design/07-feature-spec.md` — portamento/glide update.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green. Add at least one unit test in `VoiceAllocatorTests.cpp`: glide_time=0 → immediate pitch change; glide_time=100ms → pitch reaches target within expected block count.
3. Standalone:
   - Set an FM part to Mono + Legato, set DEL to 500 ms. Play two legato notes → audible pitch slide taking ~500 ms.
   - Set DEL to 0 → no slide (instant).
   - Switch to Poly mode → slide does not occur regardless of DEL setting.
   - Repeat for PSG tone channel (Mono + Legato + DEL 500 ms → pitch slides between tones).
   - DEL readout shows "OFF" at 0 and a ms value otherwise.
4. Save preset, reload → glide-time values round-trip.
5. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] DEL slider visible in the routing strip for each instrument row.
- [ ] FM glide interpolates F-number linearly over the configured ms in Mono/Legato mode.
- [ ] PSG tone glide interpolates frequency divider over the configured ms.
- [ ] DEL has no effect in Poly mode.
- [ ] "OFF" readout at 0 ms; ms readout above 0.
- [ ] State persists across save/reload.
- [ ] `VoiceAllocatorTests` glide assertions pass.
- [ ] `pluginval` SUCCESS.
