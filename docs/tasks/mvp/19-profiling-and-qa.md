# Task 19 — CPU profiling, resampler quality & parity QA

> **Milestone:** Release-ready.
> **Depends on:** Task 07. *Recommended position:* last — run it once the whole
> MVP is built (after Task 18).
> **Design references:** ADR-0010 (CPU profiling check), ADR-0011 (resampler
> upgrade path), `docs/design/01-architecture.md` (*Resampling*, *Render
> Pipeline*), `docs/design/07-feature-spec.md` (*Genny VST Feature Parity
> Checklist*, *Extensions Beyond Genny*).

## Objective

Close the implementation-time open items and do the final QA pass: confirm the
CPU cost of the 17-instance design, settle resampler quality, tune output
headroom, and verify the full feature set against the parity checklist.

## Context & key constraints

- **CPU profiling** (ADR-0010): the cost of 16 `ymfm::ym2612` voice instances
  (plus the DAC instance and the PSG) at 44,100 Hz was left as an
  implementation-time check. Measure it. If it is too costly, the
  `VoiceAllocator` abstraction was deliberately built to hide the instance
  layout so the decision can be revisited — but **do not** change the layout
  unless the measurement actually demands it.
- **Resampler quality** (ADR-0011): `juce::LagrangeInterpolator` is the baseline.
  Upgrade the FM mix-bus resampler to `juce::WindowedSincInterpolator` **only if**
  profiling headroom allows and listening reveals audible aliasing (typically on
  bright, high-content patches). The PSG resamples internally and is not part of
  this resampler.
- **Output headroom** (`01-architecture.md` *Render Pipeline*): summing 16
  voices plus the DAC can exceed unity. Tune per-voice scaling and the master
  soft-clip guard so dense material does not clip harshly while normal playing
  is not unduly quiet. This was explicitly flagged as an implementation task.
- **Parity QA:** the `07-feature-spec.md` *Genny VST Feature Parity Checklist*
  and *Extensions Beyond Genny* list every MVP feature. Walk the entire list in
  a running plugin and confirm each item works (excluding the items the ADRs
  explicitly defer post-MVP — Channel 3 special mode, CLAP, chord mode, etc.).

## Scope

- A CPU profiling measurement of the worst-case audio load and a recorded
  conclusion.
- A resampler quality decision (keep Lagrange, or upgrade to WindowedSinc) with
  the reasoning recorded.
- Final headroom / soft-clip tuning.
- A full parity-checklist walkthrough with every MVP item verified.

## Out of scope

- Re-architecting the voice model — only if profiling proves it necessary, and
  then it is a new ADR + its own task, not part of this one.
- Post-MVP features (see the backlog in `docs/tasks/mvp/README.md`).

## Implementation steps

1. Profile the plugin under worst-case load: 16 FM voices sounding, PSG (3 tone
   + noise) active, DAC playing, parameter automation running, at 44,100 Hz.
   Record CPU usage; compare against a sane budget for the target hardware.
2. If CPU is over budget, investigate within the existing layout first; only
   consider an instance-layout change if the data demands it (ADR-0010).
3. A/B the FM resampler on bright/high-content patches; decide Lagrange vs
   WindowedSinc and record why.
4. Tune per-voice scaling and the master soft-clip guard against dense chords
   and loud patches.
5. Walk the full `07-feature-spec.md` parity + extensions lists in a DAW;
   verify every MVP item; file follow-ups for anything that fails. Two
   walkthrough cases the checklist would otherwise miss:
   - **Save → PRESETS tab:** save a patch from the FM editor → it appears in
     the PRESETS tab and on disk under `<userAppData>/GenVst/patches/saved/`.
   - **Import → IMPORT tab:** drag-and-drop a `.tfi` onto the plugin window
     → it appears in the IMPORT tab and on disk under
     `<userAppData>/GenVst/patches/imported/`.

## Deliverables

Any code changes from the resampler/headroom decisions (likely small — a
resampler swap and/or scaling constants), and a short written record of the
profiling result, the resampler decision, and the parity-checklist outcome
(in the PR description or a short notes file — do not create design docs).

## Verification

1. **CPU:** worst-case load runs without audio dropouts on the target hardware
   and within the recorded budget; no `processBlock` allocation or locking
   (`pluginval --strictness-level 10` passes).
2. **Aliasing:** bright patches across the pitch range have no objectionable
   aliasing; if WindowedSinc was adopted, CPU is still within budget.
3. **Headroom:** a dense 16-voice chord at high master gain is loud but not
   harshly clipped; a single quiet patch still has reasonable level — no
   pumping, no audible soft-clip distortion at normal levels.
4. **Parity:** every item on the `07-feature-spec.md` parity checklist and the
   MVP extensions is demonstrably working in a DAW. Re-run the full unit-test
   suite (`ctest`) and `pluginval` on all platforms — all green.
5. The plugin survives a long soak (sustained play + automation + patch changes
   for many minutes) with no crash, leak, or stuck voice.

## Done when

- [ ] Worst-case CPU is measured, within budget, and recorded.
- [ ] The resampler quality decision is made and recorded; no audible aliasing.
- [ ] Output headroom is tuned — no harsh clipping, adequate level.
- [ ] The full Genny parity checklist + MVP extensions are verified working.
- [ ] `ctest` and `pluginval` are green on all platforms; the plugin soaks
      cleanly.
