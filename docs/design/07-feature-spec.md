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

- [x] YM2612 FM synthesis — single patch played polyphonically on the
  16-voice pool *(mvp2/05)*
- [x] All 8 FM algorithms *(mvp2/05)*
- [x] Per-operator controls: DT, MUL, TL, KS, AR, DR, SR, RR, SL, SSG-EG, AMON *(mvp2/05)*
- [x] Per-channel controls: ALG, FB, L/R output enable, AMS, PMS *(mvp2/05)*
- [x] Global LFO: enable toggle, rate selector (8 values) *(mvp2/05)*
- [x] TFI patch load *(mvp2/09)*
- [x] Folder-tree patch browser ([ADR-0006](adr/0006-folder-tree-patch-browser.md)), tagged ([ADR-0025](adr/0025-tagged-preset-browser.md)) *(mvp2/09)*
- [x] MIDI note-on / note-off *(mvp2/05)*
- [x] MIDI velocity → TL scaling (configurable on/off) *(mvp2/05)*
- [x] MIDI pitch bend *(mvp2/05)*
- [x] Polyphonic FM (multiple simultaneous FM notes) *(mvp2/05)*

Multitimbrality and DAC-as-sample-channel are explicitly **not** in v2 —
both are replaced by the multi-instance + audio-FX D mode design above.

---

## Extensions Beyond Genny

### Patch Formats
- [x] VGI patch import (adds AMS/FMS fields missing from TFI) *(mvp2/09)*
- [x] VGI patch export *(mvp2/09)*
- [x] DMP patch import (DefleMask format, version 11 — [ADR-0012](adr/0012-dmp-version-scope.md)) *(mvp2/09 + mvp2/10 for DMP PSG per ADR-0026)*
- [x] Y12 patch import (flat single-channel register dump) *(mvp2/09)*
- [x] OPM patch import (text-based VOPM / YM2151) *(mvp2/09)*
- [x] VGM bank import (extracts FM patches from .vgm/.vgz register streams) *(mvp2/09)*
- [x] Drag-and-drop: accept tagged patch files dropped onto plugin window *(mvp2/09)*
- [x] Bulk folder import: drop a folder → register it as a custom patch root *(mvp2/09)*

### Mode-switch / Tagged presets (v2 additions)
- [x] `mode_select` apvts param (FM | SQ | D) *(mvp2/02)*
- [x] Manual mode selector in header *(mvp2/08)*
- [x] Auto mode-switch when a tagged preset is loaded *(mvp2/09)*
- [x] `.psg` preset format (SQ mode) *(mvp2/09 + mvp2/10)*
- [x] Unified tagged preset browser with `All / FM / SQ` filter chips
      (D mode has no preset format —
      [ADR-0025](adr/0025-tagged-preset-browser.md)) *(mvp2/09)*

### Polyphony (FM mode)
- [x] FM polyphony beyond the YM2612's hardware 6 voices — single-patch,
  16-voice pool *(mvp2/05)*
- [x] `poly_voices` apvts param — numeric voice count 1–16 (default 16);
  surfaced as the `POLY` stepper on the FM mode panel, mirroring the
  RYM2612 `POLY N` field *(mvp2/05)*
- [x] `note_mode` apvts param — `RETRIG` vs `LEGATO` always-visible
  toggle on the FM panel; semantics in [`02-fm-synthesis.md`](02-fm-synthesis.md)
  § *Voice handling — LEGATO and RETRIG* *(mvp2/05)*
- [x] `pitch_bend_range` apvts param — ±1..±12 semitones (default ±2);
  surfaced as the `RANGE` stepper on the FM panel (promoted from
  Settings) *(mvp2/05)*
- [x] LRU voice stealing across the pool; release-phase voices preferred
  for stealing *(mvp2/05)*
- [x] `mod_wheel_value` and `pitch_bend_value` apvts params — display-only
  mirrors of the live MIDI stream, written by `PluginProcessor` on every
  inbound CC 1 / pitch-bend message. The FM `GLOBAL IN` PB + MW wheels
  and the SQ `GLOBAL IN` PB wheel bind to these via the normal relay
  layer (no separate telemetry event — see `05-ui-ux.md` *C++→JS
  telemetry push*). *(mvp2/05)*

### FM Features
- [ ] Channel 3 special mode (per-operator pitch as a top-level UI editor) — *post-MVP ([ADR-0014](adr/0014-special-channel-features.md))*
- [x] SSG-EG for all 8 looping envelope shapes (UI exposes them with named labels: Repeat, Hold, Alternate, Inv. Repeat, etc., not raw `8`–`15`) *(mvp2/05)*
- [x] **FREQ CTRL MODE** — three-state selector (`INT_MUL` / `FLOAT_MUL` / `AUTO_RETRIG`); RYM2612 manual page 11. `FLOAT_MUL` and `AUTO_RETRIG` use Channel 3 Special / CSM internally — see [`02-fm-synthesis.md`](02-fm-synthesis.md) § *FREQ Control Mode*. *(mvp2/05)*
- [x] **FIXED per-operator** toggle — when active in `FLOAT_MUL` / `AUTO_RETRIG`, the operator runs at an absolute Hz value (`freq_fixed_hz[op]`) instead of `note × mul_float[op]`. Greyed out in `INT_MUL`. *(mvp2/05)*
- [x] **RETRIG RATE** (TimerA value, 0–1023) — visible/editable only when `freq_ctrl_mode == AUTO_RETRIG`; writes YM2612 registers `0x24` / `0x25`. *(mvp2/05)*
- [x] **MW → PMS routing** — modwheel (CC 1) routes into the LFO `PMS` field at fixed full depth (no adjustable knob). RYM2612 manual page 10. (Modwheel is the **only** instrument-level MW route in v2; both the earlier per-operator `mw[op]` TL-modulation column and the global `mw_to_pms` depth knob were removed during the post-mockup review — the per-patch `PMS` knob already covers the "amount of vibrato" axis.) *(mvp2/05)*
- [x] **DAC PRESCALER (FM mode)** knob (`fm_dac_prescaler`, 0.0–1.0) — YM2612 internal clock prescaler / DAC sample-rate divider, modelled per [`02-fm-synthesis.md`](02-fm-synthesis.md) § *DAC Prescaler (FM mode)*. Shares the `DspDecimator` code path with D mode's `prescaler` param but stores state independently so a mode switch doesn't blow user tuning. Mirrors the `DAC PRESCALER` knob on the RYM2612 reference panel. **Lives in the persistent header next to VOL** (per RYM2612 reference) with **mode-aware binding** — FM targets `fm_dac_prescaler`, D targets `prescaler`, SQ greys it (PSG bypasses DAC). The D panel itself carries **no** prescaler knob; the header widget is the sole UI surface for both prescaler params (see `08-ui-views.md` views 1 + 4). *(mvp2/05 DSP + mvp2/08 header)*
- [x] **CH VOL (channel TL master)** knob (`channel_tl`, 0.0–1.0) — UI-only convenience that multiplies into all 4 operator TLs on the register-write path. Sits above the operator grid with connector lines fanning down to each op's TL knob, mirroring the RYM2612 reference. Not a YM2612 hardware register; the multiplier is applied per-op before the `attenuation = 127 - level` flip. Default 1.0 (no master attenuation). *(mvp2/05)*
- [x] **UI level vs HW attenuation** — `TL` / `SL` knobs and value readouts are *levels* (max = loudest, 0 = silent); the apvts → register layer inverts to hardware attenuation. See [`02-fm-synthesis.md`](02-fm-synthesis.md) § *UI level vs hardware attenuation*. *(mvp2/05)*
- [x] **HARDWARE STRICT** authenticity toggle — Settings-level opt-in
  modelled on the RYM2612 manual's *For the Purists* page. When on:
  clamps `poly_voices` to 6; restricts `FLOAT_MUL` / `AUTO_RETRIG` to a
  single voice (extra voices fall back to `INT_MUL`); forces
  `output_filter` and `ladder_effect` on and locks their header
  toggles. Default off. Bound to apvts param `hardware_strict`. *(mvp2/08)*

### Output character (all modes, v2 additions per [ADR-0024](adr/0024-hardware-filter-toggles.md))
- [x] **Output Filtering** toggle — Model-1 RC lowpass + amp coloration on mix bus *(mvp2/03 DSP + mvp2/08 header)*
- [x] **Ladder Effect** toggle — YM2612 stepwise nonlinearity (FM voice sum + D-mode output; greyed out in SQ) *(mvp2/03 DSP + mvp2/08 header)*
- [x] **FM idle-silence clamp** — `renderFmBlock` short-circuits the
      ymfm voice-render + Ladder chain when no voice is keyed-on or in
      release tail (`VoiceAllocator::hasAudibleVoice() == false`). ymfm's
      idle output isn't a hard zero (internal phase accumulators tick
      regardless of envelope state); without the clamp the LSB-level
      residue gets amplified by the 8-bit Ladder quantizer into audible
      background hiss between notes. SQ is unaffected — Ladder is
      FM-only.

### MIDI (FM and SQ modes; D mode ignores MIDI)
- [x] MIDI CC automation for all parameters (full map below) *(mvp2/05)*
- [x] Sustain pedal (CC 64): hold voices through note-off — **FM only**; SQ engine has no sustain hook so the pedal silently passes through in SQ mode *(mvp2/05)*
- [x] All Sound Off (CC 120) *(mvp2/05)*
- [x] Reset All Controllers (CC 121) — resets MW / pitch-bend mirrors and channel pressure to 0, releases sustain pedal, zeros active-voice bend. Does not reset apvts patch params (those persist as patch state).
- [x] All Notes Off (CC 123) *(mvp2/05)*
- [x] Program Change: load the Nth patch of the **current mode** (sorted across all roots; D mode ignored; mode never auto-switches — ADR-0025)
- [x] Aftertouch (channel pressure): default LFO depth (PMS); off / carrier TL alternates *(mvp2/08)*

### SQ Features
- [x] Per-channel envelope (Task 23 software ADSR) *(mvp2/06)*
- [x] PSG pitch bend (`SN76489Engine::pitchBend()`; depth shared with FM
      via the global `pitch_bend_range` apvts param). The SQ panel
      surfaces a read-only `PB` wheel visualizer in its `GLOBAL IN`
      block — see `08-ui-views.md` view 3. *(mvp2/06)*
- [x] PSG velocity → attenuation mapping *(mvp2/06)*
- [x] Per-PSG-channel soft panning (L/R gain) *(mvp2/06)*
- [x] Noise channel MIDI routing — configurable note-range split
      (`noise_split_note`, default MIDI 47 = B2). Notes ≤ split route
      to noise; notes > split route to the tone pool.

### D Mode (PCM2612-style audio FX)
- [x] Audio input bus on plugin *(mvp2/03)*
- [x] `prescaler` DSP — sample-rate decimation 0.0..1.0; UI surface is
      the **header** DAC PRESCALER knob (mode-aware binding — see
      `08-ui-views.md` view 1) *(mvp2/07 DSP + mvp2/08 header)*
- [x] `mono` toggle — collapse L/R before decimation *(mvp2/07)*
- [x] `dry_wet` mix — 0.0..1.0 blend of decimated output with original input *(mvp2/07)*
- [x] Plays nice with the Filter + Ladder toggles (Output Filter active;
      Ladder applied; both header-toggleable) *(mvp2/07)*
- [x] Signal-presence feedback via header NOTE ON LED + the DAW's
      native track input meter (no panel-side input meters per
      `08-ui-views.md` view 4) *(mvp2/07 + mvp2/08)*

### UI
- [x] Live algorithm diagram (selected algorithm highlighted) *(mvp2/05)*
- [x] Per-operator inline ADSR curve preview (FM mode) *(mvp2/05)*
- [x] `NOTE ON` indicator LED (single LED, not 16) *(mvp2/08)*
- [x] Modern hardware-VST aesthetic ([ADR-0022](adr/0022-modern-vst-aesthetic.md)) *(mvp2/01 mockup → mvp2/05-08 implementation)*
- [x] **Hover tooltips** for every interactive control (full name + one-sentence description). Gated by the global `tooltips_enabled` apvts param, surfaced as both a header `TIPS` toggle (quick access while learning the layout) and a Settings `TOOLTIPS` row (set-and-forget). Default **on**. Content schema + widget recipe per [`05-ui-ux.md`](05-ui-ux.md) *Tooltip system* and [`09-visual-spec.md`](09-visual-spec.md) § *Tooltip*. *(mvp2/04 widget + mvp2/08 settings)*
- [x] **On-screen piano roll keyboard strip** — Canvas-rendered 7-octave keyboard
  (C1–B7, MIDI 24–107) docked below the mode panel. Active MIDI notes light up
  in real time from the C++→JS telemetry `activeNotes` bitmask; clicking keys
  injects synthetic note events into the audio thread via a lock-free FIFO
  (native functions `noteOn` / `noteOff`). Toggled via `keyboard_visible` apvts
  param (surfaced in Settings). Editor window resizes: 1200×560 hidden, 1200×660
  visible. See [`08-ui-views.md`](08-ui-views.md) view 11.

### Microtuning
- [x] Scala `.scl` import — 12-degree scales, MIDI 69 = 440 Hz root, FM + SQ share one table, path persisted in DAW project. (Shipped Task 30; retained in v2 — code is mode-agnostic.)
- [ ] `.kbm` keyboard mapping (non-standard octave size / reference note) — post-MVP.
- [ ] Per-channel independent tuning tables — post-MVP.
- [ ] MTS (MIDI Tuning Standard) Sysex — post-MVP.

### State
- [x] Full DAW state save/restore (`getStateInformation`/`setStateInformation`) *(mvp2/11)*
- [x] Mode + active patch path + apvts persisted in DAW project *(mvp2/11; per-mode `<patch>` so a mode flip after restore remembers each mode's last patch label — see `PluginState.h`)*
- [ ] Standalone state file (`.gnvst`) for cross-session/machine portability — *post-MVP; the DAW project state envelope (host's `setStateInformation`) covers the in-session round-trip and is what users actually interact with via project save / template. The freestanding `.gnvst` file format is a packaging concern that needs its own ADR (filename, magic bytes, version handshake, what subset of state it captures) and is deferred to a dedicated task.*

---

## MIDI CC Map

Scaling formula: `hardware_val = round(cc_val × max_val / 127.0f)`

The CC map applies to FM and SQ modes (D mode ignores MIDI). A CC affects
the **active mode's** parameters; CCs whose target doesn't apply to the
active mode are silently ignored.

| CC | Parameter | Hardware Range | Modes | Notes |
|----|-----------|---------------|-------|-------|
| 1  | Mod Wheel → PMS (vibrato) | 0–7 | FM | Standard modwheel; also mirrored into `mod_wheel_value` apvts param for the GLOBAL IN MW wheel |
| 7  | *(intentionally ignored)* | — | — | Not routed to `master_volume` — the VOL knob is a per-instance trim and the DAW track fader already covers host-side level. Forwarding CC 7 made VOL appear to drift under controller defaults / fader automation. Host parameter automation on `master_volume` is the supported path. |
| 10 | *(no-op in v2)* | — | — | No per-instance pan apvts exists in v2 MVP (per panel design); CC 10 is silently dropped in all modes. Per-channel SQ pan and a future FM pan control would be the right vehicle — deferred to post-MVP backlog. |
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
| 64 | Sustain Pedal | 0/127 | FM | FM only — SQ engine has no sustain hold; sustain pedal CC silently passes through in SQ mode. |
| 70 | LFO Enable | 0/127 | FM | |
| 71 | LFO Rate | 0–7 | FM | |
| 72 | AMS | 0–3 | FM | |
| 73 | PMS | 0–7 | FM | |
| 80–83 | AMON OP1–OP4 | 0/127 | FM | |
| 86 | Output Filtering toggle | 0/127 | All | New v2 ([ADR-0024](adr/0024-hardware-filter-toggles.md)) |
| 87 | Ladder Effect toggle | 0/127 | FM, D | New v2 ([ADR-0024](adr/0024-hardware-filter-toggles.md)) |
| 88 | FREQ CTRL MODE | 0=INT_MUL, 64=FLOAT_MUL, 127=AUTO_RETRIG | FM | New v2 ([02-fm-synthesis.md](02-fm-synthesis.md) § *FREQ Control Mode*) |
| 89 | RETRIG RATE (TimerA) | 0–127 → 0–1023 (×8 + 7) | FM | New v2; only audible when CC 88 = AUTO_RETRIG |
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

**Unison** as a per-note voice fan-out mode is **not in v2 MVP** — the
RYM2612 reference has no unison feature, and dropping the standalone
Settings detune slider removed an ambiguous semantic. See
`docs/tasks/mvp2/README.md` *Post-MVP backlog* for the carry-forward
notes; a future unison feature needs its own enable toggle and a
re-think of POLY voice allocation when one note grabs N voices.

---

## Pitch Bend

- Bend range: configurable ±1, ±2, ±7, ±12 semitones (default ±2)
- Implementation: `semitone_offset = (bend_value / 8192.0f) × bend_range_semitones`
- Recalculate F-number and BLK for all active voices on bend (FM mode)
- In SQ mode, recalculate the divider register N for each active tone channel
- D mode ignores bend (no pitched content)

---

## Program Change

*Implemented 2026-05-27.*

A Program Change message loads the Nth patch **of the currently active
mode** in sorted order from the active mode's pool. Program-change
mode-switching is **not** supported — modes are a per-instance UI/state
decision, not a real-time MIDI surface
([ADR-0025](adr/0025-tagged-preset-browser.md)).

- FM mode: PC selects from the FM-tagged patches.
- SQ mode: PC selects from the SQ-tagged (`.psg`) presets.
- D mode: PC is ignored (no preset format — see
  [ADR-0025](adr/0025-tagged-preset-browser.md)).

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

These three apvts params persist via the host's project state and the
DAW's plugin user-preset feature; there is no dedicated D-mode preset
file format. See
[ADR-0025](adr/0025-tagged-preset-browser.md) *Alternatives considered*
for the rationale.

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
   *Status (mvp2/11):* deferred to a manual Reaper-stress measurement. The
   target is `< 30 %` peak CPU on a modern desktop for a 16-voice chord with
   FM AUTO_RETRIG + LFO + Filter + Ladder + telemetry active. Measurement
   has not yet been recorded against the final v2 build; if exceeded, the
   tuning work (ymfm batch-render, decimator vectorisation) lands as a
   post-MVP follow-up.
2. **Host quirks for instrument-with-audio-input** — Logic, Pro Tools,
   and some older hosts may need special handling for the audio input bus
   on what they classify as an instrument plugin. Verified in v2/02 task.
3. **Ladder effect curve calibration** — *Resolved 2026-05-27* — negative
   branch shifted to produce ~8× gap at zero crossing per jsgroth
   measurements; see `src/LadderEffect.cpp`.

Resolved during the post-mockup review (no longer open):
- *Mono default*: `note_mode = RETRIG`; the LEGATO/RETRIG toggle on the FM
  panel exposes both.
- *Unison*: dropped from v2 MVP (no RYM2612 reference); see post-MVP backlog.
- *Aftertouch routing default*: LFO PMS.

Former v1 open items (multitimbral allocation, PSG layer mode, DAC
loop/one-shot semantics, etc.) are no longer relevant under the v2
single-engine model.
