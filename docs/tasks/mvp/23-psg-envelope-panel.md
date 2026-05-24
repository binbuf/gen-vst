# Task 23 — PSG bottom panel: envelope view + software ADSR

> **Depends on:** Task 22 (rack model + per-instrument routing).
> **Design references:** `docs/design/03-psg-synthesis.md`, `docs/design/08-ui-views.md` (view 2), `docs/genny-ui.md` (bottom row layout).

## Objective

Match Genny's SQ section: the bottom panel becomes an FM-style envelope view per PSG channel, backed by a **software amplitude ADSR** computed in `SN76489Engine` and applied as 0..1 scaling against the 4-bit attenuation register write. The SN76489 has no envelope hardware; Genny synthesizes one in software and we follow.

## Context & key constraints

- **Software ADSR only.** Hardware register writes still go through the existing libvgm `sn764xx` core; we only add an amplitude multiplier per channel in `SN76489Engine` block-render.
- **Stage names match FM** (`ATK / DR1 / SUS / DR2 / RR`) so the panel can reuse the existing `ui/src/widgets/operator-panel.js` widget unchanged. Document in code that these are software stages, not YM2612 EG stages.
- **Per-channel state.** 4 envelope generators (tone 1, tone 2, tone 3, noise) — each driven by its part's note-on/off via MidiRouter, computing amplitude per block.
- **Zipper noise.** Block-rate updates may zipper; if audibly bad, sub-divide the block at envelope inflection points. Verify on the standalone.
- **Existing VOL/PAN/BEND cards retire.** `VOL` is now the envelope's `LEV` peak; `PAN` moves to the per-instrument routing strip from Task 22; `BEND` is the row's enable toggle (already in routing or kept as a per-channel toggle if no per-part `bend` exists).
- **Section header band keeps `PSG MIX` and `LAYER`** per view 2.
- **Noise panel keeps `TYPE` / `RATE` / `AUTO`** below the envelope (re-laid-out, not removed).

## Scope

- New per-channel apvts params (loop-generated for n ∈ {1, 2, 3, N}):
  - `psg_atk_<n>`, `psg_dr1_<n>`, `psg_sus_<n>`, `psg_dr2_<n>`, `psg_rr_<n>`,
    `psg_detune_<n>`, `psg_freq_<n>`, `psg_vel_<n>`, `psg_ksr_<n>`, `psg_ssg_<n>`.
  - Reuse Task 22's `balance_part<n>` (PAN) and per-part bend toggle.
- `src/SN76489Engine.{h,cpp}` — add per-channel `PsgEnvelope` state (ATK→DR1→SUS→DR2→RR), advance per block, multiply against the channel's base volume → 4-bit attenuation.
- `tests/PsgEnvelopeTests.cpp` — new test file (mirror `tests/PsgDacTests.cpp` style).
- `ui/src/views/sq-view.js` — replace card layout with 4 operator-panel-style panels (3 tone + 1 noise) using the existing `ui/src/widgets/operator-panel.js` + `ui/src/widgets/adsr-graph.js`. Noise panel adds the `SHFT / PERIODIC / SN76489` strip + repositioned `TYPE / RATE / AUTO`.
- `docs/design/08-ui-views.md` view 2 — update to the envelope layout.

## Out of scope

- Per-operator detuning beyond the single `psg_detune` cents param.
- Velocity → attenuation curves beyond a single linear `psg_vel` scalar.
- PSG aftertouch routing (post-MVP).

## Implementation steps

1. **Params** — extend `createParameterLayout()` with the 10 per-channel params × 4 channels via the existing loop pattern.
2. **Engine** — add `PsgEnvelope` struct to `SN76489Engine.cpp` (state machine with stage timers, level, rates). Hook `noteOn`/`noteOff` calls to enter `ATK`/`RR`. In the block-render loop, multiply the channel's current volume by `envelope.amplitude()` before computing the 4-bit attenuation.
3. **Tests** — `PsgEnvelopeTests.cpp`:
   - Default ATK=0, RR=0 → step response (immediate full level on note-on, immediate silence on note-off).
   - Long ATK → amplitude ramps from 0 to peak over the expected sample count.
   - Long RR → amplitude decays after note-off; envelope retains state until silent.
   - Note-on during release → re-trigger from current amplitude (no click).
4. **UI** — refactor `ui/src/views/sq-view.js` to render 4 panels with `operator-panel.js` + `adsr-graph.js`. Mount on `#bottom-sq`. Wire each control with the new param IDs.
5. **Noise extras** — add the `SHFT` knob, `PERIODIC` toggle, and `SN76489` branding-label strip; keep `TYPE / RATE / AUTO` as a row beneath. `SHFT` and `PERIODIC` are existing PSG noise params — verify the IDs.
6. **Style** — apply the same FM-panel chassis (dark) and envelope-LCD-green palette as the FM operator panels; no new CSS variables needed.
7. **Docs** — `docs/design/08-ui-views.md` view 2 rewrite.

## Deliverables

- `src/PluginProcessor.cpp` — new per-channel PSG params.
- `src/SN76489Engine.{h,cpp}` — PsgEnvelope state + amplitude multiply.
- `tests/PsgEnvelopeTests.cpp` — new.
- `ui/src/views/sq-view.js` — rewritten.
- `ui/src/styles/sections.css` (or `chassis.css`) — minor panel sizing tweaks if needed.
- `docs/design/08-ui-views.md` — view 2 rewrite.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green; `PsgEnvelopeTests` passes.
3. Standalone:
   - Select an SQ row from the Task 22 rack → SQ envelope panel shows.
   - Set ATK high on tone 1, play a sustained note → audible attack ramp; no click on note-on.
   - Set RR high, release the note → audible decay tail.
   - Switch to noise → same envelope behavior; `SHFT`/`PERIODIC` toggles audibly change noise character; `TYPE`/`RATE`/`AUTO` still work.
   - Listen for zipper noise on slow ATK sweeps; if perceptible, sub-divide blocks at the inflection.
4. Save preset, reload — every envelope param round-trips.
5. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] Four envelope panels (3 tone + 1 noise) render with the operator-panel widget.
- [ ] PsgEnvelope amplitude multiplies the 4-bit attenuation; per-channel ADSR is audible.
- [ ] `PsgEnvelopeTests` covers step/ramp/decay/retrigger.
- [ ] Noise panel keeps `TYPE`/`RATE`/`AUTO` + new `SHFT`/`PERIODIC`/`SN76489` strip.
- [ ] State persists across save/reload.
- [ ] `pluginval` SUCCESS.
