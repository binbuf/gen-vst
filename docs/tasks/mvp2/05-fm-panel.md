# Task 05 — FM panel (+ FREQ CTRL MODE register paths)

> **Milestone:** FM mode plays — MIDI notes drive the full RYM2612-style
> FM panel UI; INT_MUL / FLOAT_MUL / AUTO_RETRIG all work; LEGATO and
> RETRIG note-mode work; UI level vs HW attenuation is inverted at the
> apvts→register layer; per-op VEL → TL modulation is audible; the
> FM DAC prescaler colours the voice rendering.
> **Depends on:** Task 04.
> **Design references:** `docs/design/08-ui-views.md` view 2 (primary —
> the complete FM panel layout + control table),
> `docs/design/02-fm-synthesis.md` (*FREQ Control Mode*,
> *UI level vs hardware attenuation*, *Voice handling — LEGATO and
> RETRIG*), `docs/design/07-feature-spec.md` (FM features checklist),
> `docs/design/04-patch-system.md` (`Patch` struct + v2 fields),
> ADR-0010 (per-voice ymfm instance, channel choice), ADR-0021.

## Objective

Assemble the FM mode panel from Task 04's widget library, wire every
control to apvts, and finish the engine-side work the v2 design adds on
top of the existing FM voice pool:

- **FREQ CTRL MODE** — implement the three register-write paths
  (INT_MUL on the existing channel-0 model; FLOAT_MUL on Channel 3
  Special; AUTO_RETRIG on Channel 3 CSM + TimerA).
- **Per-op VEL → TL** modulation depth (`vel[op]`) — routed through
  the CC / pitch-bend / aftertouch dispatch. The earlier per-op
  `mw[op]` route was removed during the post-mockup review — modwheel
  is a global-only input now; per-op modulation goes via velocity.
  Modwheel (CC 1) routes into the LFO `PMS` field at fixed full depth
  (the earlier `mw_to_pms` depth scaler was also dropped).
- **FM DAC PRESCALER** knob (`fm_dac_prescaler`, 0.0–1.0) wired
  through the FM voice-render path (see `02-fm-synthesis.md`
  § *DAC Prescaler (FM mode)*). Shares the `DspDecimator` code path
  with the D-mode `prescaler` param. **The control widget lives in the
  header** (see `08-ui-views.md` view 1) — Task 08 mounts it there;
  this task wires it to the FM-mode DSP. In SQ and D mode the same
  header knob is greyed out (no effect on PSG; D mode uses its own
  panel-resident prescaler).
- **CH VOL (channel TL master)** knob (`channel_tl`, 0.0–1.0). Above
  the operator grid with fan-out connector lines to each op's TL knob.
  UI-only convenience — folded into the per-op TL on the register-write
  path. Default 1.0.
- **Note-mode LEGATO / RETRIG** — LEGATO skips the key-off/key-on pair
  and only updates the frequency registers on a re-keyed voice.
- **UI level vs HW attenuation** — `tl[op]` / `sl[op]` apvts values are
  *level* (max = loudest); the apvts→register layer inverts to
  attenuation before any `chip.write` call. **`TL` is now the leftmost
  knob in each operator row** (a regular `.knob` with a slightly larger
  / darker body, matching the RYM2612 reference); not a right-margin
  vertical slider.
- **POLY stepper** (1–16), **RANGE stepper** (±1–±12), **global PB /
  MW horizontal level meters** in the LFO/global block (telemetry of
  incoming CC values; one meter per row, full-width, not per-op).
- **8-button algorithm picker (`algo-grid`)** + larger algorithm-mini
  topology diagram — all 8 algorithms visible all the time; the
  selected one is `.is-active`; the topology tile redraws to match.

After this task: loading a TFI factory patch and playing notes through
the host plays through the v2 FM panel; every knob, switch, badge, and
LCD on the panel is bound and audible.

## Context & key constraints

- **Single FM patch, 16-voice pool** (ADR-0021). The legacy v1 per-part
  apparatus is already gone (Task 02). All 16 voices play the one active
  patch.
- **Layout** (`08-ui-views.md` view 2): the top of the panel carries the
  LFO / RATE / PMS / AMS knobs, the POLY/RANGE steppers, the
  LEGATO/RETRIG toggle, the envelope-curve widget (with
  `AR`/`DR`/`SL`/`SR`/`RR` segment labels + `KEY ON` / `KEY OFF`
  markers), the FREQ CTRL MODE pill, the RETRIG RATE **stepper-readout**
  (LCD + ▲/▼; visible only when AUTO_RETRIG is selected), and the OP1
  FB knob (with a short connector line to op 1 below it). The DAC
  PRESCALER knob now lives in the persistent **header** (view 1), not
  in this panel.
  The mid-row is split into three blocks: the **`GLOBAL IN`** block
  (left) holds the global PB and MW `midi-wheel` widgets (vertical
  hardware-style wheels, not LED meters); the **operator grid** holds
  the 4-row main body with a **`CH VOL` master knob above the TL
  column** (with fan-out connector lines down to each op's TL); and
  the **right margin** carries one column only — per-op `VEL` knobs.
  `TL` is the leftmost column in each operator row (knob, anchor),
  then `[N]` AM AR DR SL SR RR RS SSG-EG MUL FREQ FIX DT. The top-right
  area carries the **8-button `algo-grid` picker** (visible all the
  time) plus a separate larger `algorithm-mini` topology diagram tile.
- **FREQ CTRL MODE register paths** (`02-fm-synthesis.md`
  *FREQ Control Mode*):

  | Mode | Channel used | Frequency source | Key-on path |
  |---|---|---|---|
  | `INT_MUL` | channel 0 (the existing path) | one shared F-number per voice + `MUL` field per op | the v1 *Register Write Sequence for Note-On* |
  | `FLOAT_MUL` | channel 3 special (`0x27` bits 7:6 = `01`) | per-op F-numbers `0xA8`/`0xAC`/`0xA9`/`0xAD`/`0xAA`/`0xAE`/`0xA2`/`0xA6` | key-on via the standard `0x28` path, but targeting channel 3 (`0x28` data = `0xF2`/`0xF6` depending on bank) |
  | `AUTO_RETRIG` | channel 3 CSM (`0x27` bits 7:6 = `11`) | per-op F-numbers per FLOAT_MUL | TimerA (`0x24`/`0x25`) + `0x27` LOAD / EN / RST bits fire the internal retrigger; the standard `0x28` is **not** used |

  In all three cases the voice still lives in its own `ymfm::ym2612`
  instance; only the active channel within the instance changes.
  Operators must have non-zero RR in AUTO_RETRIG so each auto-keyed
  event has audible release.
- **`FIXED` per-operator** — when on (and mode = FLOAT_MUL /
  AUTO_RETRIG), operator `op` plays at `freq_fixed_hz[op]` Hz, not
  `note × mul_float[op]`. Greyed out in INT_MUL.
- **UI level vs HW attenuation** (`02-fm-synthesis.md`): the apvts
  `tl[op]` / `sl[op]` parameters are now exposed as *levels*. Range
  for `tl[op]` is 0..127 (level), 0 = silent, 127 = loudest; the
  register write is `attenuation = 127 - level`. Same for `sl[op]`
  with max 15. The on-disk `Patch` struct stores hardware
  *attenuation* for round-trip with TFI/VGI/DMP/Y12/OPM — only the
  apvts surface and the on-screen knob value are flipped. Patch
  loader output → apvts conversion does the flip on import; apvts →
  register write does the inverse flip per voice key-on.
- **Note-mode LEGATO** (`02-fm-synthesis.md` *Voice handling*): when a
  re-keyed voice has `note_mode == LEGATO` and is the active voice
  (mono or stolen-poly), steps 1 and 5 of the key-on sequence are
  skipped — the voice writes operator/channel/frequency registers
  fresh but does **not** issue the key-off/key-on pair, so the
  envelope keeps running.
- **Per-op VEL → TL** (`02-fm-synthesis.md` *RYM2612 manual page 10*):
  on key-on, the effective TL written to the register is
  `tl_register = clamp(127 - tl_level + (127 × vel_depth ×
  (127-velocity)/127), 0, 127)`. The `vel_depth` here is the per-op
  `vel[op]` apvts param. There is **no per-op MW route** — the earlier
  draft's `mw[op]` term was removed during the post-mockup review to
  match the RYM2612 reference. Recompute on velocity changes during
  voice retrigger; no need to rewrite on CC 1 changes (modwheel
  doesn't affect per-op TL anymore).
- **Global MW → PMS** — modwheel CC writes to the `PMS` field in
  `0xB4` per channel at fixed full depth (no `mw_to_pms` scaler in
  v2). The base PMS apvts param is the patch's value; MW adds on top,
  clamped to 7.
- **POLY stepper** binds `poly_voices` (1..16). Voice allocator caps
  the active pool size each block. When HARDWARE STRICT is on
  (Task 08), this clamps to 6 — but that lives in Task 08; this task
  just binds the param.
- **RANGE stepper** binds `pitch_bend_range` (1..12).
- **Global PB / MW level meters** are read-only — the editor's
  telemetry timer pushes raw CC values; the meters render against
  `0..1`. MW is *only* a global meter in v2; there is no per-operator
  MW knob anywhere on the FM panel.
- **DAC PRESCALER** binds `fm_dac_prescaler` (0.0..1.0). Wired into the
  FM render pipeline between the FM voice sum and the ladder /
  output-filter stages (see `02-fm-synthesis.md` § *DAC Prescaler (FM
  mode)*). Shares the `DspDecimator` implementation with D mode's
  `prescaler`, but each mode stores its own param value.
- **8-button algorithm picker** binds `algorithm` (0..7) through
  `algo-grid` from Task 04. The picker is `algo-grid`; the larger
  algorithm-mini topology tile next to it is read-only and updates
  via `setAlgorithm(idx)` whenever the param changes.

## Scope

- C++:
  - `Voice` and / or `VoiceAllocator` gain a per-voice
    `freq_ctrl_mode` snapshot + per-voice `mul_float[4]`, `fixed[4]`,
    `freq_fixed_hz[4]`, `vel[4]`, and `fm_dac_prescaler` snapshots.
    (No per-op `mw[4]` — modwheel is global-only in v2. No `mw_to_pms`
    scaler — modwheel routes to LFO PMS at fixed full depth.)
  - New `FmRegisterMap` helpers for: writing channel-3 special mode
    + per-op F-numbers; writing CSM mode + TimerA; resolving an
    operator's pitch (in Hz, then to F-number+BLK) under each mode.
  - `Voice::noteOn(...)` routes through the new path; key-off,
    register update, and key-on follow the per-mode sequence.
  - `Voice::keyOnLegato(...)` (or a flag on `noteOn`) implements the
    LEGATO skip.
  - `Voice::updateModulation(midi cc, velocity)` recomputes TL
    register writes for any operator whose `vel[op]` is non-zero
    (velocity → TL depth). Called from the CC dispatch + key-on path.
    Modwheel still influences the LFO PMS depth at fixed full depth
    but is no longer per-op and no longer has an adjustable scaler.
  - `DspDecimator` is invoked on the FM voice-sum bus when
    `fm_dac_prescaler > 0`, mirroring its D-mode call site. Same
    implementation, separate param value.
  - The CC table in `PluginProcessor::handleControlChange` is brought
    forward to the v2 list in `07-feature-spec.md` *MIDI CC Map* —
    notably CC 88 / 89 / 90 wired now.
- UI (`ui/src/views/fm-view.js` — new):
  - Build the FM panel HTML per `08-ui-views.md` view 2.
  - Mount and bind every widget. Op-badge click selects which operator
    the `envelope-curve` widget tracks (local UI state).
  - The `RETRIG RATE` LCD + stepper are conditionally visible: hidden
    when `freq_ctrl_mode != AUTO_RETRIG`, greyed/hidden when in
    INT_MUL / FLOAT_MUL.
  - The `FIXED` toggle column is greyed-out when `freq_ctrl_mode ==
    INT_MUL`.
  - The `FREQ` LCD per row is state-dependent (per view 2's table).
  - The `LEGACY`/`CRYSTAL CLEAR` Output Filter switch is part of the
    header (Task 08), not this panel — do not surface it here.
- `main.js` mounts `fm-view` into `#mode-panel` when `mode_select ==
  FM`. Subscribing to `mode_select` changes is enough; the other panel
  mounts come in Tasks 06 / 07.
- Tests:
  - `tests/FmFreqCtrlModeTests.cpp` — register-write sequence for each
    of the three modes (compare written register bytes against an
    expected log for a known patch).
  - `tests/FmLegatoTests.cpp` — LEGATO note-on path skips key-off/key-on.
  - `tests/FmTlInversionTests.cpp` — apvts TL level 0 maps to register
    127, level 127 maps to register 0, level 64 maps to register 63
    (or 64 — pin the rounding).

## Out of scope

- HARDWARE STRICT enforcement (poly_voices clamp to 6, FLOAT_MUL
  single-voice fallback, force filter/ladder on) — Task 08.
- UNISON DETUNE param drive — Task 08 (the param exists from Task 02;
  this task does **not** add unison spread to the voice allocator).
- Header / Settings — Task 08. (No separate status bar — the v2
  first-pass status bar was removed during the post-mockup review.)
- Output Filter / Ladder Effect UI binding — Task 08 (the apvts params
  already drive the DSP from Task 03).
- Tagged preset browser — Task 09.

## Implementation steps

1. **Apvts → register inversion** for TL / SL — implement
   `levelToAttenuation(int level, int maxAttenuation)` in
   `FmRegisterMap` and apply it everywhere TL or SL is written from
   the apvts. Update the patch loader output → apvts conversion
   (TFI/VGI/DMP/Y12/OPM all store register attenuation on disk; the
   loaders convert to apvts level so the UI shows the inverted value
   correctly).
2. **Channel-3 Special / CSM helpers** in `FmRegisterMap`:
   - `writeChannel3Special(chip, blockData)` and
     `writeChannel3CSM(chip, blockData)` — write `0x27` with the
     correct bits 7:6.
   - `writeChannel3PerOpFrequency(chip, opIndex, freqHz, BLK)` — write
     to `0xA8`/`0xAC`/`0xA9`/`0xAD`/`0xAA`/`0xAE`/`0xA2`/`0xA6` per the
     mapping in `02-fm-synthesis.md` *Channel 3 Special Mode Frequencies*.
   - `writeTimerA(chip, retrig_rate)` — split the 10-bit value into
     high (`0x24`) and low (`0x25`).
3. **Voice key-on routing**:
   - Add `currentMode` (Mode::INT_MUL / FLOAT_MUL / AUTO_RETRIG) +
     `currentChannel` (0 in INT_MUL, 3 in FLOAT_MUL / AUTO_RETRIG) to
     `Voice`'s state.
   - On note-on: snapshot `freq_ctrl_mode` from the apvts; pick the
     target channel; write the per-mode key-on sequence.
   - INT_MUL: existing v1 path (channel 0, shared F-number, per-op
     MUL). No regression.
   - FLOAT_MUL: switch to channel 3 special; per-op F-numbers from
     `note × mul_float[op]` (or `freq_fixed_hz[op]` if
     `fixed[op]`); key-on via `0x28` with channel-3 OPS mask.
   - AUTO_RETRIG: switch to channel 3 CSM; per-op F-numbers as
     FLOAT_MUL; write TimerA; set `0x27` LOAD / EN / RST bits; skip
     the standard `0x28` key-on.
   - On note-off: per-mode key-off (channel 3 in FLOAT_MUL / AUTO_RETRIG;
     channel 0 in INT_MUL). In AUTO_RETRIG also clear the TimerA load
     bit so the retrigger stops.
4. **LEGATO note-on path**:
   - Voice gains an `bool envelopeActive` flag.
   - On note-on: if `note_mode == LEGATO && envelopeActive`, write
     only the per-op + channel + frequency registers (skip key-off,
     skip key-on). Otherwise full sequence.
   - Stealing always takes the full sequence regardless of `note_mode`
     (per the *Voice handling* table in `02-fm-synthesis.md`).
5. **Per-op VEL → TL**:
   - Voice records the velocity at key-on.
   - `Voice::recomputeTL(int op)` computes the effective register TL
     per the formula in the *Context* section, writes the `0x40+op`
     register on the voice's channel.
   - On key-on, after writing operator params, call `recomputeTL` for
     every op whose `vel[op] != 0`.
   - CC dispatch: on CC 1 change (modwheel), the active voices each
     recompute the PMS depth at full depth (no scaler); per-op TL no
     longer depends on MW.
6. **MW → PMS dispatch**:
   - On CC 1 change, the active engine recomputes the effective PMS
     per voice as `pms_register = clamp(pms_param + 7 × mwCC/127, 0,
     7)` and writes `0xB4` for each active channel. No `mw_to_pms`
     scaler in v2.
7. **CC table refresh** — bring `handleControlChange` to the v2 map
   from `07-feature-spec.md`. CC 84 / 85 are removed. CC 86 / 87
   target `output_filter` / `ladder_effect`. CC 88 / 89 target
   `freq_ctrl_mode`, `retrig_rate`. The per-op TL / MUL / DT / AR /
   DR / SR / RR / SL / KS / AMON CCs already exist — ensure they
   don't carry `_part<n>` lookups (Task 02 removed those).
8. **`fm-view.js`** — build the panel HTML following view 2 verbatim.
   Mount and bind every widget. Wire the op-badge active-row selection
   to the `envelope-curve` widget. Wire FREQ-CTRL-MODE-dependent
   visibility for RETRIG RATE and the FIXED column.
9. **`main.js`** — when `mode_select == FM` mount the FM view into
   `#mode-panel`; on mode change unmount and replace.
10. Tests as listed in *Scope*.

## Deliverables

- C++ updates: `src/Voice.{h,cpp}`, `src/VoiceAllocator.{h,cpp}` (small —
  voice-state extension), `src/FmRegisterMap.{h,cpp}` (new helpers),
  `src/PluginProcessor.cpp` (`handleControlChange` v2 CC map; per-block
  `pushPolyphonyParameters` / `pushFmModulationParameters`).
- Patch loader updates: every loader (`PatchSystem`'s `loadTFI` / `loadVGI` /
  `loadDMP` / `loadY12` / `loadOPM`) keeps its on-disk register encoding
  but the apvts-write path converts TL / SL to level via
  `levelToAttenuation`.
- UI: `ui/src/views/fm-view.js`, updated `ui/src/main.js`.
- Tests: `tests/FmFreqCtrlModeTests.cpp`, `tests/FmLegatoTests.cpp`,
  `tests/FmTlInversionTests.cpp`.

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   all tests pass, including the three new test files.
2. Dev-server FM panel: open the Standalone with
   `-DGENVST_DEV_SERVER=ON` + `npm run dev`. Set `mode_select = FM`.
   The FM panel renders per view 2. Every widget is interactive.
3. Load `bass.tfi` from the factory bank (via the host's
   generic editor on the apvts patch-path param, until Task 09's
   browser arrives — or via the existing `loadTFI` + dev-helper path).
   Play a MIDI note on the host's piano roll. The plugin sounds.
4. **INT_MUL → FLOAT_MUL** A/B: set `freq_ctrl_mode = INT_MUL` with
   `mul[0..3] = 1, 2, 3, 1`. Play C4. Note the timbre. Switch to
   `FLOAT_MUL` with `mul_float[0..3] = 1.0, 2.0, 3.0, 1.0` — should
   sound identical. Adjust one to `2.5` — the timbre shifts (an
   inharmonic partial).
5. **AUTO_RETRIG** — set `freq_ctrl_mode = AUTO_RETRIG`, `retrig_rate
   = 498` (the RYM2612 reference value). Hold a note; the operator
   envelope auto-fires at the TimerA rate; lowering `retrig_rate`
   slows the auto-fire (lower N = faster timer count, so lower N
   actually fires *faster*; verify direction against the
   `02-fm-synthesis.md` 10-bit timer description).
6. **LEGATO** — set `note_mode = LEGATO`, `poly_voices = 1`, play two
   overlapping notes (release the first after pressing the second).
   The envelope does **not** re-attack; the pitch glides only as the
   second note's F-numbers take effect. Switch to `RETRIG` and the
   second note re-attacks the envelope.
7. **TL / SL inversion** — set `tl[0]` = 127 (level) — the operator
   plays at full attenuation 0 (loudest). Set `tl[0]` = 0 — silent.
   Same direction on `sl[0]`.
8. **MW → TL** — set `mw[3] = 1.0` (carrier op modulation depth 100 %).
   Hold a note; ride CC 1 from 0 to 127. The note loudness drops as
   the modwheel rises (depth applied to the carrier's TL).
9. **VEL → TL** — set `vel[3] = 1.0`. Play soft (vel ~32) vs hard
   (vel ~120). Soft is much quieter; with `vel[3] = 0` (default) there
   is no velocity effect (assuming the global `velocity_to_tl` toggle
   is off — that's Settings/Task 08).
10. **POLY stepper** — set 16, play a chord; ramp to 1 (mono). Voice
    stealing should engage at the new cap.
11. **RANGE stepper** — set ±2, then ±12. A full-up bend in the host
    shifts the note by the configured semitone count.
12. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
    passes.

## Done when

- [ ] Every widget in `08-ui-views.md` view 2 is mounted, bound, and
      behaves per the *Behaviour* notes in that doc.
- [ ] All three FREQ CTRL MODE register paths produce audible output;
      switching modes mid-playback is glitch-free.
- [ ] LEGATO suppresses key-off/key-on on re-keyed voices.
- [ ] TL / SL apvts are *levels*; register writes are inverted
      attenuation.
- [ ] MW / VEL → TL and MW → PMS produce the expected modulation.
- [ ] The v2 CC map drives the right parameters; CC 84 / 85 are
      ignored.
- [ ] Three new unit tests pass; `ctest` is green; `pluginval` clean.
