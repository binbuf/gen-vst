# ADR-0011: Chip-to-host resampling strategy

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [01-architecture.md](../01-architecture.md), [03-psg-synthesis.md](../03-psg-synthesis.md), [ADR-0010](0010-ymfm-instance-model.md), [ADR-0014](0014-special-channel-features.md)

## Context

ymfm generates audio at the YM2612 native rate (~53,267 Hz). The host DAW runs at
44,100, 48,000, 88,200 or 96,000 Hz, so resampling is required. All 16 FM voice
instances ([ADR-0010](0010-ymfm-instance-model.md)) — and the dedicated DAC
instance ([ADR-0014](0014-special-channel-features.md)) — generate at the **same**
native rate.

Two questions: **where** resampling happens (per voice, or once on the mix), and
**which** interpolation quality to use.

## Decision

**Sum first, resample once.** All FM voice outputs, plus the DAC instance, are
summed at the ~53,267 Hz native rate into a single FM mix bus, which is then
resampled to the host rate in **one pass**. Resampling is a linear operation, so
summing then resampling is equivalent to resampling every voice then summing — at
roughly **1/16 the cost**.

**Interpolator.** Use the `juce::Interpolator` family — start with
`juce::LagrangeInterpolator` (good quality, cheap, and a push/`process`-based API
that fits the per-sub-block render loop). Upgrade to
`juce::WindowedSincInterpolator` only if profiling shows audible aliasing.

`juce::ResamplingAudioSource` is **not** used: it is a pull-model `AudioSource`
and does not fit the push-model, per-MIDI-event sub-block render loop in
`01-architecture.md`.

## Consequences

- One resampler instance for the whole FM mix bus, not 16.
- Per-voice pitch comes from the YM2612 F-number registers, **not** from a
  resampling ratio — so voices genuinely do not need independent resamplers. (The
  earlier "per-voice resampler for independent pitch variation" rationale was
  incorrect and is dropped.)
- The SN76489 PSG resamples **internally** via its core's `sampleRate` init
  argument (see `03-psg-synthesis.md`), so the PSG path does not pass through the
  FM mix-bus resampler. The FM and PSG signal paths differ in this respect.
- The resampler is (re)initialized in `prepareToPlay` on host sample-rate changes.

## Alternatives considered

- **Per-voice resamplers (16 instances)** — the earlier proposal; ~16× the cost
  for no quality gain, since all voices share one native rate. Rejected.
- **Linear interpolation only** — trivial, but audibly aliases above ~20 kHz; it
  is the quality floor, while Lagrange is the baseline.
- **Polyphase FIR (hand-rolled)** — best quality; deferred —
  `juce::WindowedSincInterpolator` covers the same need without custom code.
