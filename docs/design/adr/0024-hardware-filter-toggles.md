# ADR-0024: Output character — independent Filter and Ladder toggles

- **Status:** Accepted
- **Date:** 2026-05-24
- **Related:** [ADR-0021](0021-three-mode-single-engine-ui.md), [ADR-0011](0011-resampling-strategy.md), `docs/design/02-fm-synthesis.md`

## Context

The bare YM2612 + SN76489 + DAC output of an emulator sounds cleaner than the
audio a real Sega Genesis produces. Two analog stages on the console shape
the "Genesis sound" listeners recognise:

1. **Output filtering** — the Model-1 console's analog low-pass after the
   YM3438/YM2612 DAC. This is a gentle high-frequency roll-off plus subtle
   resonance from the discrete amp circuit. It's the warmth/dullness that
   distinguishes hardware recordings from raw emulator output.
2. **Ladder effect** — the YM2612's discrete-channel summing exhibits a
   non-linear stepwise transfer at low signal levels (the famous "ladder"
   distortion). It's a quantisation-style artefact independent of the
   analog filter, and it's what makes some Genesis basses sound gritty even
   on clean recordings.

These are **distinct DSP stages** that produce audibly different effects.
Inphonik's RYM2612 exposes each as an independent toggle, and the user
explicitly chose RYM2612-parity in this regard: support both, switchable
separately.

## Decision

Gen VST exposes **two independent output-character toggles** in the header:

| Toggle | apvts param | Default | Behaviour when on |
|---|---|---|---|
| **Output Filtering** | `output_filter` | **On** | Apply the Genesis Model-1 analog low-pass (RC-modelled, ≈ -3 dB knee near 3.4 kHz, mild Q) + DAC reconstruction characteristic to the mix bus. |
| **Ladder Effect** | `ladder_effect` | **On** | Apply the YM2612's discrete-channel stepwise nonlinearity at the per-channel sum, before the mix bus. |

Both default **on** because that's the sound users expect when they reach for
a Genesis emulator; producers wanting pristine emulator output flip them off.

**Visual rendering.** Output Filtering is rendered as a **2-position
physical-switch widget** with the labels `LEGACY` and `CRYSTAL CLEAR`,
matching the convention on the Inphonik RYM2612 and PCM2612 panels:

- `LEGACY` position → `output_filter == true` → Model-1 stage on
- `CRYSTAL CLEAR` position → `output_filter == false` → bypass

The underlying apvts parameter stays a bool; only the on-screen
labelling differs from a generic on/off toggle. Ladder Effect is a
single on/off LED rocker (no two-state labelling), since the RYM2612
panel shows it that way as well.

**Scope:**

- The emulated console is **Model-1 only** (the warmer one). A Model-2 / VA
  / clean-discrete option is **not** in v2 scope.
- The Ladder effect applies in **FM mode and D mode** (both pass audio
  through the YM2612 chip's analog output stage on real hardware — FM via
  channels 1–5 + 6, D via channel 6's DAC). The toggle is greyed out in
  SQ mode only (the SN76489 has its own output pin, not the YM2612 ladder
  DAC); the `ladder_effect` param exists but has no audible effect there.
- Output Filtering applies in **all three modes** — the Model-1 analog
  stage is downstream of every chip on the real hardware (including the
  DAC path that D mode emulates).
- Both toggles operate on the **mix bus before** the single resample pass
  ([ADR-0011](0011-resampling-strategy.md)); they're cheap DSP that fits in
  the per-block render path.

**Implementation:**

- Output Filtering — a one-pole RC low-pass at native rate followed by a
  light shelf to model the amp colouration. Coefficients fixed; not
  user-tunable.
- Ladder Effect — a static lookup-table or polynomial nonlinearity applied
  per-channel at the FM voice sum stage **and** at the D-mode decimator
  output (the same lookup, applied after the 8-bit quantizer). Calibrated
  against published measurements of the YM2612 ladder DAC.

The exact filter coefficients and ladder curve live in
`docs/design/02-fm-synthesis.md` (DSP detail), not in this ADR.

## Consequences

- New global apvts params `output_filter` (bool, default true) and
  `ladder_effect` (bool, default true).
- The v2 FM render path adds the ladder stage at the per-voice sum and the
  output-filter stage at the mix bus, behind cheap branches that early-out
  when the toggle is off (DSP bypassed entirely, not "process and multiply
  by 1").
- VGM logging (Task 29 v1) records register writes only, not the post-DSP
  audio — `output_filter` and `ladder_effect` do not appear in the VGM
  stream. Hardware playback of the logged VGM will naturally have its own
  filter and ladder characteristic; the toggles do not need to be
  represented in the file.
- Tests cover each stage in isolation: a known input through the filter
  off → filter on → ladder off → ladder on combinations, with golden
  spectra / waveforms checked against measured Model-1 reference clips.

## Alternatives considered

- **One combined "Hardware" toggle** — simpler UI, but conflates two
  audibly distinct effects. Users who want the ladder grit on clean
  high-end (or vice versa) couldn't do it. Rejected for RYM2612 parity.
- **Continuous "Hardware Amount" knob** — feels controllable but is
  meaningless DSP-wise (you can't half-apply a ladder nonlinearity in a way
  that maps to a real-world parameter). Rejected.
- **Per-mode separate toggles** — overengineering. The filter and ladder
  have natural per-mode behaviour (ladder is FM-only) without needing
  per-mode duplicates of the controls.
- **Defaults off** — would feel "wrong" out of the box; the Genesis sound
  *is* the filtered + ladder sound. Rejected.
