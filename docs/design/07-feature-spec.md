# Feature Specification

## Three-Mode Architecture

Gen VST v2 is a **single-engine instrument** ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).
Each plugin instance runs exactly one of three modes:

| Mode | Engine | UI inspiration |
|---|---|---|
| **FM** | Single-patch YM2612 FM, 16-voice ymfm pool | Inphonik RYM2612 |
| **SQ** | Single-patch SN76489 PSG (3 tone + 1 noise) | Modern subtractive synth |
| **D** | Audio-input bitcrush DSP (PRESCALER + MONO + DRY/WET) | Inphonik PCM2612 Retro Decimator Unit |

To play multiple Genesis timbres in a project, instantiate the plugin once
per timbre. Per-instance mode is selected via the `mode_select` apvts
parameter — manually via the header selector or automatically when a tagged
preset is loaded ([ADR-0025](adr/0025-tagged-preset-browser.md)).

---

## Genny VST FM Parity Checklist

All FM features present in Genny v1.5 are matched in FM mode:

- [ ] YM2612 FM synthesis — single patch played polyphonically on the
  16-voice pool
- [ ] All 8 FM algorithms
- [ ] Per-operator controls: DT, MUL, TL, KS, AR, DR, SR, RR, SL, SSG-EG, AMON
- [ ] Per-channel controls: ALG, FB, L/R output enable, AMS, PMS
- [ ] Global LFO: enable toggle, rate selector (8 values)
- [ ] TFI patch load
- [ ] Folder-tree patch browser ([ADR-0006](adr/0006-folder-tree-patch-browser.md)), tagged ([ADR-0025](adr/0025-tagged-preset-browser.md))
- [ ] MIDI note-on / note-off
- [ ] MIDI velocity → TL scaling (configurable on/off)
- [ ] MIDI pitch bend
- [ ] Polyphonic FM (multiple simultaneous FM notes)

Multitimbrality and DAC-as-sample-channel are explicitly **not** in v2 —
both are replaced by the multi-instance + audio-FX D mode design above.

---

## Extensions Beyond Genny

### Patch Formats
- [ ] VGI patch import (adds AMS/FMS fields missing from TFI)
- [ ] VGI patch export
- [ ] DMP patch import (DefleMask format, version 11 — [ADR-0012](adr/0012-dmp-version-scope.md))
- [ ] Y12 patch import (flat single-channel register dump)
- [ ] OPM patch import (text-based VOPM / YM2151)
- [ ] VGM bank import (extracts FM patches from .vgm/.vgz register streams)
- [ ] Drag-and-drop: accept tagged patch files dropped onto plugin window
- [ ] Bulk folder import: drop a folder → register it as a custom patch root

### Mode-switch / Tagged presets (v2 additions)
- [ ] `mode_select` apvts param (FM | SQ | D)
- [ ] Manual mode selector in header
- [ ] Auto mode-switch when a tagged preset is loaded
- [ ] `.psg` preset format (SQ mode)
- [ ] `.gdac` preset format (D mode)
- [ ] Unified tagged preset browser with `All / FM / SQ / D` filter chips

### Polyphony (FM mode)
- [ ] FM polyphony beyond the YM2612's hardware 6 voices — single-patch,
  16-voice pool
- [ ] `poly_voices` apvts param — numeric voice count 1–16 (default 16);
  surfaced as the `POLY` stepper on the FM mode panel, mirroring the
  RYM2612 `POLY N` field
- [ ] `note_mode` apvts param — `RETRIG` vs `LEGATO` always-visible
  toggle on the FM panel; semantics in [`02-fm-synthesis.md`](02-fm-synthesis.md)
  § *Voice handling — LEGATO and RETRIG*
- [ ] `pitch_bend_range` apvts param — ±1..±12 semitones (default ±2);
  surfaced as the `RANGE` stepper on the FM panel (promoted from
  Settings)
- [ ] `unison_detune_cents` apvts param — 0..50 ¢ unison spread applied
  to voices triggered by the same note; lives in Settings (replaces the
  earlier draft Unison sub-mode)
- [ ] LRU voice stealing across the pool; release-phase voices preferred
  for stealing

### FM Features
- [ ] Channel 3 special mode (per-operator pitch as a top-level UI editor) — *post-MVP ([ADR-0014](adr/0014-special-channel-features.md))*
- [ ] SSG-EG for all 8 looping envelope shapes (UI exposes them with named labels: Repeat, Hold, Alternate, Inv. Repeat, etc., not raw `8`–`15`)
- [ ] **FREQ CTRL MODE** — three-state selector (`INT_MUL` / `FLOAT_MUL` / `AUTO_RETRIG`); RYM2612 manual page 11. `FLOAT_MUL` and `AUTO_RETRIG` use Channel 3 Special / CSM internally — see [`02-fm-synthesis.md`](02-fm-synthesis.md) § *FREQ Control Mode*.
- [ ] **FIXED per-operator** toggle — when active in `FLOAT_MUL` / `AUTO_RETRIG`, the operator runs at an absolute Hz value (`freq_fixed_hz[op]`) instead of `note × mul_float[op]`. Greyed out in `INT_MUL`.
- [ ] **RETRIG RATE** (TimerA value, 0–1023) — visible/editable only when `freq_ctrl_mode == AUTO_RETRIG`; writes YM2612 registers `0x24` / `0x25`.
- [ ] **MW → PMS** global depth knob (`mw_to_pms`) — scales modwheel's effect on PMS vibrato depth; default 1.0. RYM2612 manual page 10. (Modwheel is the **only** instrument-level MW route in v2; the earlier per-operator `mw[op]` TL-modulation column was removed during the post-mockup review to match the RYM2612 reference, which keeps MW as a global meter only.)
- [ ] **DAC PRESCALER (FM mode)** knob (`fm_dac_prescaler`, 0.0–1.0) — YM2612 internal clock prescaler / DAC sample-rate divider, modelled per [`02-fm-synthesis.md`](02-fm-synthesis.md) § *DAC Prescaler (FM mode)*. Shares the `DspDecimator` code path with D mode's `prescaler` param but stores state independently so a mode switch doesn't blow user tuning. Mirrors the `DAC PRESCALER` knob on the RYM2612 reference panel.
- [ ] **UI level vs HW attenuation** — `TL` / `SL` knobs and value readouts are *levels* (max = loudest, 0 = silent); the apvts → register layer inverts to hardware attenuation. See [`02-fm-synthesis.md`](02-fm-synthesis.md) § *UI level vs hardware attenuation*.
- [ ] **HARDWARE STRICT** authenticity toggle — Settings-level opt-in
  modelled on the RYM2612 manual's *For the Purists* page. When on:
  clamps `poly_voices` to 6; restricts `FLOAT_MUL` / `AUTO_RETRIG` to a
  single voice (extra voices fall back to `INT_MUL`); forces
  `output_filter` and `ladder_effect` on and locks their header
  toggles. Default off. Bound to apvts param `hardware_strict`.

### Output character (all modes, v2 additions per [ADR-0024](adr/0024-hardware-filter-toggles.md))
- [ ] **Output Filtering** toggle — Model-1 RC lowpass + amp coloration on mix bus
- [ ] **Ladder Effect** toggle — YM2612 stepwise nonlinearity (FM voice sum + D-mode output; greyed out in SQ)

### MIDI (FM and SQ modes; D mode ignores MIDI)
- [ ] MIDI CC automation for all parameters (full map below)
- [ ] Sustain pedal (CC 64): hold voices through note-off
- [ ] All Sound Off (CC 120)
- [ ] Reset All Controllers (CC 121)
- [ ] All Notes Off (CC 123)
- [ ] Program Change: load the Nth patch of the **current mode**
- [ ] Aftertouch (channel pressure): default LFO depth (PMS); off / carrier TL alternates

### SQ Features
- [ ] Per-channel envelope (Task 23 software ADSR)
- [ ] PSG pitch bend
- [ ] PSG velocity → attenuation mapping
- [ ] Per-PSG-channel soft panning (L/R gain)

### D Mode (PCM2612-style audio FX)
- [ ] Audio input bus on plugin
- [ ] `prescaler` DSP — sample-rate decimation 0.0..1.0
- [ ] `mono` toggle — collapse L/R before decimation
- [ ] `dry_wet` mix — 0.0..1.0 blend of decimated output with original input
- [ ] Stereo level meters (input)
- [ ] Plays nice with the Filter + Ladder toggles

### UI
- [ ] Live algorithm diagram (selected algorithm highlighted)
- [ ] Per-operator inline ADSR curve preview (FM mode)
- [ ] `NOTE ON` indicator LED (single LED, not 16)
- [ ] Modern hardware-VST aesthetic ([ADR-0022](adr/0022-modern-vst-aesthetic.md))

### Microtuning
- [x] Scala `.scl` import — 12-degree scales, MIDI 69 = 440 Hz root, FM + SQ share one table, path persisted in DAW project. (Shipped Task 30; retained in v2 — code is mode-agnostic.)
- [ ] `.kbm` keyboard mapping (non-standard octave size / reference note) — post-MVP.
- [ ] Per-channel independent tuning tables — post-MVP.
- [ ] MTS (MIDI Tuning Standard) Sysex — post-MVP.

### State
- [ ] Full DAW state save/restore (`getStateInformation`/`setStateInformation`)
- [ ] Mode + active patch path + apvts persisted in DAW project
- [ ] Standalone state file (`.gnvst`) for cross-session/machine portability

---

## MIDI CC Map

Scaling formula: `hardware_val = round(cc_val × max_val / 127.0f)`

The CC map applies to FM and SQ modes (D mode ignores MIDI). A CC affects
the **active mode's** parameters; CCs whose target doesn't apply to the
active mode are silently ignored.

| CC | Parameter | Hardware Range | Modes | Notes |
|----|-----------|---------------|-------|-------|
| 1  | Mod Wheel → PMS (vibrato) | 0–7 | FM | Standard modwheel |
| 7  | Master Volume | 0–127 | All | Standard volume |
| 10 | Pan (L/R) | 0–127 | FM, SQ | Standard pan |
| 14 | Algorithm (ALG) | 0–7 | FM | |
| 15 | Feedback (FB) | 0–7 | FM | |
| 16–19 | TL OP1–OP4 | 0–127 | FM | |
| 20–23 | MUL OP1–OP4 | 0–15 | FM | |
| 24–27 | DT OP1–OP4 | 0–6 | FM | |
| 28–31 | AR OP1–OP4 | 0–31 | FM | |
| 32–35 | DR OP1–OP4 | 0–31 | FM | |
| 36–39 | SR OP1–OP4 | 0–31 | FM | |
| 40–43 | RR OP1–OP4 | 0–15 | FM | |
| 44–47 | SL OP1–OP4 | 0–15 | FM | |
| 48–51 | KS OP1–OP4 | 0–3 | FM | |
| 64 | Sustain Pedal | 0/127 | FM, SQ | Standard |
| 70 | LFO Enable | 0/127 | FM | |
| 71 | LFO Rate | 0–7 | FM | |
| 72 | AMS | 0–3 | FM | |
| 73 | PMS | 0–7 | FM | |
| 80–83 | AMON OP1–OP4 | 0/127 | FM | |
| 86 | Output Filtering toggle | 0/127 | All | New v2 ([ADR-0024](adr/0024-hardware-filter-toggles.md)) |
| 87 | Ladder Effect toggle | 0/127 | FM, D | New v2 ([ADR-0024](adr/0024-hardware-filter-toggles.md)) |
| 88 | FREQ CTRL MODE | 0=INT_MUL, 64=FLOAT_MUL, 127=AUTO_RETRIG | FM | New v2 ([02-fm-synthesis.md](02-fm-synthesis.md) § *FREQ Control Mode*) |
| 89 | RETRIG RATE (TimerA) | 0–127 → 0–1023 (×8 + 7) | FM | New v2; only audible when CC 88 = AUTO_RETRIG |
| 90 | MW → PMS depth | 0–127 → 0.0–1.0 | FM | New v2 |
| 120 | All Sound Off | — | FM, SQ | Standard (immediate silence) |
| 121 | Reset All Controllers | — | FM, SQ | Standard |
| 123 | All Notes Off | — | FM, SQ | Standard (with release) |

CC 84 (DAC Enable) and CC 85 (PSG Mix Level) from v1 are **removed** —
both targeted features that no longer exist in v2 (D mode is no longer a
DAC channel and SQ mode is no longer a layer on top of FM).

All CC-targeted parameters are also exposed as JUCE `AudioProcessorParameter`
entries in `apvts` for full DAW automation lane support.

---

## Polyphony Modes (FM mode only)

Polyphony is a single-instance setting under v2 — there is no per-part
notion. SQ mode has its own fixed allocation (round-robin LRU across three
tone channels + last-note priority on noise); D mode has no voices.

### Poly (Default)

Standard polyphonic mode. Up to N simultaneous FM voices (configurable
8/12/16, default 16).

Voice stealing: LRU (Least Recently Used). The voice with the longest
elapsed time since its note-on is stolen first. Voices in release phase
are preferred for stealing over voices still in their sustain/decay phase.

### Mono

Single voice. New note-on either:
- **Retrigger:** send key-off to current voice, wait one block, send key-on with new note
- **Legato:** skip key-off; update frequency registers only; envelope continues from current level

Configurable via a "Mono Mode" toggle in the header / mode panel.

#### Portamento / Glide time

A **glide-time** slider sets how long a Mono+Legato voice takes to slide
between successive notes. Range **0–2000 ms** (integer), default **0** =
instant (no slide). Glide is performed as a linear interpolation of
MIDI-note value at native-rate sample granularity, re-deriving the YM2612
F-number each block; bend rides on top of the interpolated pitch.

- **Mono+Legato only.** Retrigger ignores glide (the key-off restart
  precludes a smooth pitch slide). Poly ignores it (each voice is
  independent). Unison ignores it (each note-on allocates a fresh voice
  stack).
- **SQ mode tones** also expose a per-channel glide-time
  (`glide_time_psg_ch1..3`). When a new note arrives on an already-sounding
  tone channel, the divider register is interpolated per block over the
  configured time.
- **PSG noise** has no pitch; no glide param exists for it.

### Unison

All N voices play the same pitch simultaneously, each detuned by a per-voice
**F-number offset**. (Not the YM2612 DT register — DT is a coarse 3-bit
detune and cannot express cents; fine unison spread must be applied to the
F-number.) Offsets fan out symmetrically:

```
Voice 0: no offset
Voice 1: +spread × 1
Voice 2: -spread × 1
Voice 3: +spread × 2
Voice 4: -spread × 2
...
```

Spread is a plugin parameter in cents (0–50); each voice's F-number is
computed for its detuned pitch. Larger spread = a wider, more chorused
unison.

---

## Pitch Bend

- Bend range: configurable ±1, ±2, ±7, ±12 semitones (default ±2)
- Implementation: `semitone_offset = (bend_value / 8192.0f) × bend_range_semitones`
- Recalculate F-number and BLK for all active voices on bend (FM mode)
- In SQ mode, recalculate the divider register N for each active tone channel
- D mode ignores bend (no pitched content)

---

## Program Change

A Program Change message loads the Nth patch **of the currently active
mode** in sorted order from the active mode's pool. Program-change
mode-switching is **not** supported — modes are a per-instance UI/state
decision, not a real-time MIDI surface
([ADR-0025](adr/0025-tagged-preset-browser.md)).

- FM mode: PC selects from the FM-tagged patches.
- SQ mode: PC selects from the SQ-tagged (`.psg`) presets.
- D mode: PC selects from the D-tagged (`.gdac`) presets.

Bank Select (MSB/LSB) to address different roots (factory vs. user vs.
custom) is a possible later addition.

---

## D Mode Specification (PCM2612-style audio FX)

D mode is an **audio effect**, not an instrument. It takes audio on the
plugin's input bus and runs it through:

```
input → [optional MONO collapse]
      → DspDecimator (sample-rate decimation + 8-bit quantizer)
      → [optional Ladder Effect, per global toggle]
      → DRY/WET blend with the unprocessed input
      → [optional Output Filtering, per global toggle]
      → output
```

The decimator is implemented as a sample-and-hold against a divisor of the
host sample rate — *not* a real resample pass. `prescaler = 0.0` keeps
every sample (host rate); `prescaler = 1.0` keeps roughly one sample in
sixteen (heavy crush). Mapping is in `src/DspDecimator.{h,cpp}`.

**No WAV loading, no MIDI triggering, no sample slots** — that v1 model
was retired ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)). The
v1 `DACPlayer`, `DACKit`, and DAC ymfm instance are deleted.

Apvts params:
- `prescaler` (float, 0..1)
- `mono` (bool)
- `dry_wet` (float, 0..1)

`.gdac` JSON presets store exactly these three values — schema in
[04-patch-system.md](04-patch-system.md).

---

## State Persistence

All `apvts` parameters are automatically serialized by JUCE. Custom
fields appended to the XML:

```xml
<GenVstState>
  <patch path="…/factory/bass.tfi"/>     <!-- active patch (or absent) -->
  <customRoots>
    <root path="…"/>
  </customRoots>
  <!-- apvts parameter tree follows (mode_select + FM + SQ + D + globals) -->
</GenVstState>
```

`setStateInformation` restores all `apvts` parameters (which includes
`mode_select`) and reloads the active patch by path. A patch path that no
longer resolves leaves the restored parameter values in place and raises
a notification toast.

The v1 `<parts>` array, `<psg>` per-channel MIDI bindings, and `<dac>`
embedded base64 PCM are all **gone** — v2 does not persist any of them.

---

## Open Questions

Most former open questions are now resolved by ADRs (see
`docs/design/adr/`). v2-specific open items:

1. **CPU profiling pass** — confirm 16 ymfm instances at 44,100 Hz are
   affordable; revisit the instance layout if not ([ADR-0010](adr/0010-ymfm-instance-model.md)).
   A post-skeleton implementation check.
2. **Mono / Unison defaults** — Mono exposes both retrigger and legato;
   pick the shipped default (proposed: retrigger). Pick the default Unison
   spread value.
3. **Aftertouch routing default** — channel pressure is a configurable
   routing (LFO depth or carrier TL); default = **LFO depth (PMS)**
   (carried over from Task 06).
4. **Host quirks for instrument-with-audio-input** — Logic, Pro Tools,
   and some older hosts may need special handling for the audio input bus
   on what they classify as an instrument plugin. Verified in v2/02 task.
5. **Ladder effect curve calibration** — the lookup table in
   `src/LadderEffect.{h,cpp}` needs final calibration against measured
   YM2612 reference clips during Task v2/08.

Former v1 open items (multitimbral allocation, PSG layer mode, DAC
loop/one-shot semantics, etc.) are no longer relevant under the v2
single-engine model.
