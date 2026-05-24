# Task 03 — DSP foundations: mode dispatch, audio input bus & decimator / filter / ladder

> **Milestone:** Three modes audible — D mode passes audio through the
> decimator; OutputFilter + LadderEffect DSP stages exist and are wired
> behind the global toggles; FM + SQ still produce sound via their
> existing engines.
> **Depends on:** Task 02.
> **Design references:** `docs/design/01-architecture.md` (primary —
> *Mode dispatch*, *Render Pipeline*, *Threading Model*),
> `docs/design/02-fm-synthesis.md` (*Ladder Effect DSP*, *Output Filtering
> DSP*), `docs/design/07-feature-spec.md` (*D Mode Specification*),
> ADR-0021, ADR-0024, ADR-0011.

## Objective

Stand up the v2 DSP foundations behind the mode-dispatch path that Task 02
stubbed out:

- The plugin **declares an audio input bus** and the host can route audio
  into it.
- A new `DspDecimator` runs in D mode (sample-and-hold + 8-bit
  quantisation) and produces an audible crush effect on the input signal.
- A new `OutputFilter` applies the Model-1 RC-LPF + shelf to the mix bus
  in every mode when the `output_filter` global toggle is on.
- A new `LadderEffect` applies the YM2612 stepwise nonlinearity at the FM
  per-voice sum (FM mode) and at the post-decimator D output (D mode)
  when the `ladder_effect` global toggle is on.

After this task, **D mode is audibly working** when a user routes audio
into the plugin in a host that supports instrument-with-audio-input
(Reaper is the verified target — Logic and Pro Tools quirks are flagged
in the *Open questions* below).

## Context & key constraints

- **Audio input bus** (`01-architecture.md` *JUCE AudioProcessor
  Architecture*, ADR-0021 *Audio input bus*): declare a stereo input
  bus in `getBusesProperties()`; allow `MainInput` to be mono or stereo
  via `isBusesLayoutSupported`. The bus is silent / unread in FM and SQ
  modes; in D mode it is the source.
- **Mode dispatch** (`01-architecture.md` *Mode dispatch*): read
  `mode_select` once at the top of `processBlock`; switch into one of
  three render paths; the other two engines do **no** work. A brief
  fade-out / fade-in is applied across mode boundaries to avoid clicks.
- **D-mode signal flow** (`07-feature-spec.md` *D Mode Specification*):
  `input → optional MONO collapse → DspDecimator → optional Ladder →
  DRY/WET blend with the unprocessed input → optional OutputFilter →
  output`.
- **DspDecimator** (`07-feature-spec.md`): sample-and-hold at a divisor
  of the host sample rate. `prescaler = 0.0` keeps every sample
  (host rate, identity); `prescaler = 1.0` keeps roughly one sample in
  sixteen (heavy crush). Picked mapping: `holdSamples = round(1 + 15
  × prescaler)` — produces 1..16 sample hold, monotonic. The held value
  is then **8-bit quantised**: `quantized = round(sample × 128) / 128`
  with clipping at ±1. Implemented in `src/DspDecimator.{h,cpp}`.
- **OutputFilter** (`02-fm-synthesis.md` *Output Filtering DSP*,
  ADR-0024): one-pole RC low-pass at ≈3.4 kHz `−3 dB` knee followed by a
  light high-shelf taming the upper midrange. Fixed coefficients (not
  user-tunable). Implemented in `src/OutputFilter.{h,cpp}`. Bypass when
  `output_filter` is false — `if (!enabled) return;` at the top of
  `process`, no DSP runs at all.
- **LadderEffect** (`02-fm-synthesis.md` *Ladder Effect DSP*, ADR-0024):
  piecewise-linear lookup over a 512-entry table. Curve: linear from
  `−256..−1` and from `0..+255`; the gap between `−1` and `0` is **eight
  times** what a linear DAC would produce. Calibrated against the
  measurements in jsgroth's "Emulating the YM2612: Part 5" series and
  the SpritesMind hardware-test threads. The lookup is a compile-time
  constant array; populating it at startup is acceptable. Implemented in
  `src/LadderEffect.{h,cpp}`. **Applies in FM and D modes**; greyed out
  in SQ (the SN76489 has its own output pin — the toggle exists for
  parameter symmetry but has no audible effect).
- **No heap allocation in `processBlock`**. All scratch buffers
  pre-allocated in `prepareToPlay`.
- **CC 86 / 87** (`07-feature-spec.md` *MIDI CC Map*) target the
  `output_filter` and `ladder_effect` apvts params — those connections
  are wired in Task 05 (FM CC dispatch) but the **apvts params exist
  now**; nothing in this task blocks the CC wiring later.
- **Telemetry** — D mode's level meters and the header NOTE ON LED both
  need data. Push `noteOn` true when any FM voice is active or any SQ
  channel is gating; in D mode push `noteOn` true when the input level
  exceeds a small threshold (≈ −60 dBFS), so the LED reads as
  "signal present" in D mode. Level meters were already cut to L/R peak
  in Task 02 — keep them; both FM/SQ output and D output write to them.

## Scope

- New `src/DspDecimator.{h,cpp}` — single-class DSP module.
- New `src/OutputFilter.{h,cpp}` — single-class DSP module.
- New `src/LadderEffect.{h,cpp}` — single-class DSP module + 512-entry
  lookup constant.
- `PluginProcessor`:
  - `getBusesProperties()` declares an input bus (stereo by default;
    mono accepted via `isBusesLayoutSupported`).
  - `prepareToPlay` allocates the scratch buffers and calls
    `prepare(...)` on each DSP module.
  - `processBlock` implements the three render paths per
    `01-architecture.md` *Render Pipeline*. FM and SQ keep their
    existing render code; D mode runs the full input→decimator→
    [ladder]→dry/wet→[filter]→output chain.
  - The mode crossfade — when `mode_select` differs from the previous
    block's mode, fade out the current engine over the first half of
    the block, swap, fade in over the second half. Single fade ramp on
    the output buffer is enough; the engines do not need fade-aware
    APIs.
- Telemetry — extend the level-meter write path to cover the D-mode
  output as well; tweak the noteOn-write logic per the threshold rule
  above for D mode.
- Three new unit-test files under `tests/`: `DspDecimatorTests.cpp`,
  `OutputFilterTests.cpp`, `LadderEffectTests.cpp`.

## Out of scope

- The v2 widget library and any UI changes — Task 04+.
- D-mode preset format (`.gdac`) — Task 09.
- The header Output Filter / Ladder Effect toggle widgets — Task 08
  (the apvts params already exist from Task 02; the UI binding is the
  later task).
- FM register-write changes for FREQ_CTRL_MODE = FLOAT_MUL / AUTO_RETRIG
  — Task 05.
- HARDWARE STRICT semantics — Task 08.

## Implementation steps

1. Implement `DspDecimator`:
   - `prepare(double sampleRate, int maxBlockSize)` — store sample rate
     (unused until we extend mapping); allocate nothing else.
   - `process(juce::AudioBuffer<float>& buffer, float prescaler01)` —
     iterate per channel × per sample; maintain a `heldSample` per
     channel and a `samplesUntilNext` counter. When the counter hits
     zero, sample the current input, 8-bit quantise it via
     `std::clamp(std::round(s * 128.f), -128.f, 127.f) / 128.f`, store
     in `heldSample`, reload `samplesUntilNext = holdSamples(prescaler)`.
     Write `heldSample` to the buffer. `holdSamples(prescaler) =
     std::max(1, (int) std::round(1.0f + 15.0f * prescaler01))`.
   - `reset()` — clear `heldSample` and `samplesUntilNext` per channel.
2. Implement `OutputFilter`:
   - `prepare(double sampleRate)` — compute one-pole RC coefficients
     for the 3.4 kHz knee (`a = exp(-2π × fc / fs)`); compute shelf
     coefficients for the light upper-midrange tame (a single biquad
     with `g = +0.5 dB at 5 kHz, Q = 0.6` is a reasonable starting
     calibration — actual coefficients will be tuned against the
     measured reference; the test in step 8 freezes them).
   - `process(juce::AudioBuffer<float>& buffer, bool enabled)` — early
     return if `!enabled`. Otherwise: per channel run the one-pole LPF
     then the shelf.
   - `reset()` — clear filter state.
3. Implement `LadderEffect`:
   - A static `constexpr` or const-init 512-entry lookup array,
     indexed from `−256..+255` (offset by +256 when accessing). Values
     match the published Genesis ladder curve: linear from `−1.0`
     (index 0) to `−1/256` (index 255), then a `−1/256 → 0 → +1/256`
     plateau with the 8× gap exactly at the `−1 → 0` boundary, then
     linear from `+1/256` (index 257) to `+1.0` (index 511). Numerical
     values come from the jsgroth article; the test in step 8 captures
     four pinch points.
   - `prepare(double sampleRate)` — no-op (lookup is static).
   - `process(juce::AudioBuffer<float>& buffer, bool enabled)` — early
     return when `!enabled`. Otherwise per sample: `int idx =
     std::clamp((int) std::round(s * 256.f), -256, 255); buffer[i] =
     ladderLookup[idx + 256];`.
4. Wire the audio input bus:
   - `getBusesProperties()` returns `BusesProperties()
     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
     .withOutput("Output", juce::AudioChannelSet::stereo(), true);`.
   - `isBusesLayoutSupported(layouts)` accepts: mono input + mono
     output, stereo input + stereo output, stereo input + stereo
     output. Reject anything else.
5. In `prepareToPlay`, call `prepare` on the three new DSP modules and
   pre-allocate any scratch buffers (e.g. a `juce::AudioBuffer<float>
   inputCopy` for D mode's dry/wet blend, sized to `maxBlockSize × 2`).
6. Implement `processBlock` render paths per
   `01-architecture.md` *Render Pipeline*:
   - **FM mode** path is the existing one with `LadderEffect` inserted
     after the FM voice sum and `OutputFilter` inserted on the mix bus.
   - **SQ mode** path is the existing one with `OutputFilter` only
     (no ladder).
   - **D mode** path follows the signal flow in the spec; the
     `dry_wet` blend is `out = (1 − dryWet) × dryCopy + dryWet ×
     processed`.
7. Implement the mode-change crossfade. The first block where
   `currentMode != lastMode`: render with the *new* mode but apply a
   `juce::SmoothedValue<float>` 0→1 ramp over the block. Update
   `lastMode` after rendering.
8. Write the unit tests:
   - **`DspDecimatorTests.cpp`** — sample-and-hold for `prescaler=0`
     produces output ≡ input (identity), for `prescaler=0.5` the held
     period is ~8 samples, for `prescaler=1.0` the held period is ~16
     samples; 8-bit quantisation collapses sub-1/128 differences.
   - **`OutputFilterTests.cpp`** — known sine at 200 Hz passes within
     `−0.5 dB`; known sine at 10 kHz is attenuated by at least
     `−6 dB`; bypass produces identity.
   - **`LadderEffectTests.cpp`** — four pinch points on the curve:
     `f(−1.0) = −1.0`, `f(−1/256) ≈ −1/256`, `f(0) = 0`,
     `f(+1/256) ≈ +1/256`; the gap between `f(−1/256)` and `f(0)` is
     ~8× the gap between `f(−2/256)` and `f(−1/256)`. Bypass produces
     identity.

## Deliverables

- `src/DspDecimator.{h,cpp}`, `src/OutputFilter.{h,cpp}`,
  `src/LadderEffect.{h,cpp}`.
- `tests/DspDecimatorTests.cpp`, `tests/OutputFilterTests.cpp`,
  `tests/LadderEffectTests.cpp`; updated `tests/CMakeLists.txt`.
- Updated `src/PluginProcessor.{h,cpp}` (bus declaration,
  `prepareToPlay`, `processBlock` render paths, mode crossfade).
- Updated `src/CMakeLists.txt` `target_sources`.

## Verification

1. `cmake --build build/windows-debug` clean.
2. `ctest --test-dir build/windows-debug --output-on-failure` — all
   tests pass, including the three new ones.
3. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes (pluginval at level 8 exercises `processBlock` with random
   audio and MIDI; an instrument plugin declaring an audio input is the
   first thing it stresses).
4. **Reaper manual test (D mode):**
   - Insert the VST3 on an audio track that has signal (white noise or
     a drum loop). Reaper offers an "audio input from track" mode for
     instruments — enable it.
   - Set the apvts: `mode_select = D`, `prescaler = 0.7`,
     `dry_wet = 1.0`, `mono = false`, `output_filter = true`,
     `ladder_effect = true`. The host can drive these via the
     automation lane / generic-editor UI (no v2 UI yet).
   - The output should be audibly bit-crushed; switching
     `output_filter` off makes it noticeably brighter; switching
     `ladder_effect` off softens the grit at low signal levels.
5. **FM regression** — load any factory TFI (e.g. `bass.tfi`) into
   the (already-existing) FM apvts params, set `mode_select = FM`,
   play a note via the host's piano roll. Sound is the same as the
   Task 02 FM regression baseline, plus or minus the ladder/filter
   coloration (toggling them on/off audibly differs).
6. **SQ regression** — set `mode_select = SQ`, play a note;
   SN76489 tone sounds; `output_filter` toggle audibly affects
   brightness; `ladder_effect` toggle has **no** audible effect
   (greyed out for SQ by design).
7. Switching `mode_select` mid-playback does **not** click — the
   crossfade hides the boundary.

## Done when

- [ ] `DspDecimator`, `OutputFilter`, `LadderEffect` exist with unit
      tests passing.
- [ ] The plugin declares an audio input bus; pluginval level 8 passes.
- [ ] D mode audibly bit-crushes a routed input signal in Reaper.
- [ ] Toggling `output_filter` and `ladder_effect` audibly changes the
      output in FM and D modes; SQ is unaffected by the ladder toggle.
- [ ] Mode switches don't click.
- [ ] The three new DSP modules' bypass paths add no measurable cost
      (the test runs each in bypass and produces identity output).

## Open questions (resolved here)

- **Ladder curve calibration.** Initial values from jsgroth's article
  are committed; the test pins four points. A follow-on calibration
  pass against measured YM2612 reference clips is a follow-up (see
  `07-feature-spec.md` *Open Questions* #5) — re-run the test with the
  new values when the calibration set arrives.
- **Host quirks** (`07-feature-spec.md` #4). Reaper is the verified
  target. Logic and Pro Tools handling for instrument-with-audio-input
  is **not** retested here; Task 10's cross-platform QA covers Logic.
