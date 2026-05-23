# Task 27 — LFO waveform selector

> **Depends on:** Task 22 (per-part APVTS layout established).
> **Design references:** `docs/design/02-fm-synthesis.md` (YM2612 LFO register), `docs/design/08-ui-views.md` (view 1 LFO section), `docs/genny-ui.md` (left column LFO panel).
> **Note:** If Task 24 stubbed VGM logging and already stamped a `27-vgm-logging.md`, renumber this task to 31 or the next available slot.

## Objective

Expose the YM2612 LFO waveform register (bits 0–1 of register 0x22) as a per-part APVTS parameter and add a four-way selector to the FM view's LFO section. Genny exposes this via the unnamed parameter block (#24–39 per `reference/genny/params_live.txt`); gen-vst currently hardwires waveform 0 (sine). All four hardware waveforms — **Saw / Square / Triangle / Noise** — must be selectable.

## Context & key constraints

- **Hardware register 0x22, bits 0–1.** The YM2612 LFO is global per chip, but the waveform is the same for all channels. Writing this register is already handled in the YM2612 init path — the new param just controls bits 0–1 there.
- **Per-part scope in APVTS but single-chip write.** Because gen-vst models six FM parts sharing one chip, any part's waveform write wins (last write wins). Document this in code. For practical use, expose the param per-part so DAW automation can change it, but note the limitation in `docs/design/02-fm-synthesis.md`.
- **Genny's LFO panel** shows three knobs (LFO rate, AMS, FMS) each with a small red power dot. No explicit waveform control is visible in the screenshot — waveform may be embedded in the rate knob or a sub-row. Match the visual layout from `docs/genny-ui.md` left column.
- **Four waveform choices.** YM2612 datasheet: 0=saw, 1=square, 2=triangle, 3=noise. Use these exact names in the UI.

## Scope

- New APVTS param per FM part: `lfo_waveform_part<1–6>` — `AudioParameterChoice`, choices `["Saw", "Square", "Triangle", "Noise"]`, default 0 (Saw).
- `src/YM2612Engine.cpp` (or the register-write path): on param change, write bits 0–1 of register 0x22 with the new waveform value. Use the existing param-change observer pattern.
- `ui/src/views/fm-view.js` — add a four-pill selector row labeled **WAVE** beneath (or adjacent to) the existing LFO rate knob. Reuse the existing pill-button widget. Bind via `bindChoice('lfo_waveform_part1')` (or active-part binding if the LFO section already switches per selected part).
- `ui/src/styles/chassis.css` — minor sizing if the LFO sub-panel needs to accommodate the new row.

## Out of scope

- Per-operator LFO depth modulation (that is AMS/PMS, already implemented).
- Changing the LFO section layout beyond adding the WAVE row.

## Implementation steps

1. **Params** — add `lfo_waveform_partN` (N=1–6) in `createParameterLayout()` using the existing per-part loop. `AudioParameterChoice` with four entries.
2. **Engine** — in the param-change handler that already writes LFO rate, add a waveform write: `ym2612_write(0x22, (lfo_enable << 3) | (lfo_rate & 0x07) | ((lfo_waveform & 0x03) << ?))`. Verify the exact bit layout against the YM2612 datasheet register 0x22 description in `02-fm-synthesis.md`.
3. **UI** — four-pill `WAVE` row in `fm-view.js`, same visual style as the ALG pills. Bind to the active-part waveform param.
4. **Docs** — one-line note in `docs/design/02-fm-synthesis.md` under LFO: "waveform bits 0–1; last-part write wins across shared chip."

## Deliverables

- `src/PluginProcessor.cpp` — 6 new `lfo_waveform_partN` params.
- `src/YM2612Engine.cpp` (or equivalent) — waveform bits in register 0x22 write.
- `ui/src/views/fm-view.js` — WAVE selector row.
- `ui/src/styles/chassis.css` — minor sizing if needed.
- `docs/design/02-fm-synthesis.md` — one-line LFO waveform note.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green (no new test file required, but if the register write is testable, add a single assertion in the existing YM2612 test suite).
3. Standalone:
   - LFO section shows a four-pill WAVE row (Saw / Square / Triangle / Noise).
   - Select Square; play a sustained note with LFO rate and AMS set high → audibly different tremolo character vs. Saw.
   - Select Noise → irregular amplitude modulation audible.
   - Switch parts; WAVE pill updates to that part's waveform.
4. Save preset, reload → waveform selection round-trips.
5. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] `lfo_waveform_partN` params registered for all 6 FM parts.
- [ ] YM2612 register 0x22 bits 0–1 reflect the selected waveform.
- [ ] WAVE pill row visible and bound in the LFO section.
- [ ] Audible difference between at least Saw and Square with LFO active.
- [ ] State persists across save/reload.
- [ ] `pluginval` SUCCESS.
