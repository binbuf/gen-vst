# Task 02 — ymfm single-voice audio & render pipeline

> **Milestone:** E2E #2 — the plugin produces sound.
> **Depends on:** Task 01.
> **Design references:** `docs/design/02-fm-synthesis.md` (primary),
> `docs/design/01-architecture.md` (*Resampling*, *Render Pipeline*,
> *Threading Model*), ADR-0002, ADR-0010, ADR-0011.

## Objective

Make the plugin **make a sound**. Wire one `ymfm::ym2612` instance into the
processor, drive it with a single hard-coded FM patch, render through a proper
chip-rate → host-rate pipeline, and key it from MIDI note-on/off. This retires
the ymfm audio-correctness integration risk and gives the first audible
feedback loop.

## Context & key constraints

- ymfm is BSD-3 (ADR-0002). Use the `ymfm::ym2612` class — the discrete OPN2
  with the 9-bit DAC "ladder effect" — **not** `ym3438`.
- Add the ymfm sources to the build now: compile `ymfm_opn.cpp` and
  `ymfm_misc.cpp` **inline into the plugin target** (ADR-0002), and add
  `third_party/ymfm/src` to the include dirs — exactly as
  `06-build-system.md` shows.
- Implement `GenVstYmfmInterface : public ymfm::ymfm_interface` with no-op
  external read/write stubs (see `02-fm-synthesis.md` *ymfm OPN2 API*).
- This task uses **one** ymfm instance on channel 0. The 16-voice pool is
  Task 05; do not build it here.
- **Native rate.** `chip.sample_rate(7670454)` ≈ 53,267 Hz. The host runs at
  44.1/48/88.2/96 kHz, so resampling is required (ADR-0011).
- **Render pipeline** (per `01-architecture.md`): render the voice at the native
  rate into a pre-allocated native-rate buffer, then resample to the host rate
  in a **single pass** with a `juce::LagrangeInterpolator`. Do **not** use
  `juce::ResamplingAudioSource` (pull-model — wrong fit). All working buffers are
  allocated in `prepareToPlay`; **no heap allocation, no locks** in `processBlock`.
- **Register write order matters.** The note-on register sequence in
  `02-fm-synthesis.md` (*Register Write Sequence for Note-On*) is exact: key-off
  first, then per-operator params in the hardware operator order **S1, S3, S2, S4**
  (offsets +0x00/+0x04/+0x08/+0x0C — note S2 and S3 are swapped vs their numbers),
  then channel params, then frequency **HIGH byte before LOW byte**, then key-on.
- **F-number:** `FREQ = round(note_hz × 2^20 / 53267.0)`; pick `BLK` so `FREQ`
  stays in `0x000–0x7FF`. Use the snippet in `02-fm-synthesis.md`
  (*MIDI note to frequency*).
- Introduce a **minimal `apvts`** here: a single global `master_gain` parameter
  (0.0–1.0). This is the first `AudioProcessorValueTreeState` parameter and the
  binding target for Task 03's knob. The full 6-part parameter layout is Task 05.

## Scope

- ymfm added to `src/CMakeLists.txt` (sources + include dir).
- `GenVstYmfmInterface`.
- A single `ymfm::ym2612` member on the processor, reset in `prepareToPlay`.
- One **hard-coded patch** in code — a simple, clearly audible timbre (e.g. an
  organ/sine-ish algorithm-7 or algorithm-0 patch). Hard-coded values are fine;
  Task 04 replaces this with loaded `Patch` data.
- A helper that writes the full note-on register sequence for a MIDI note, and a
  note-off (key-off) path.
- `prepareToPlay`: allocate the native-rate render buffer and the
  `LagrangeInterpolator` state; (re)init on sample-rate change.
- `processBlock`: iterate the `MidiBuffer`; on note-on/off drive the chip;
  render native-rate audio; resample to host rate; apply `master_gain` and a
  soft-clip guard; write L/R.
- `apvts` with one `master_gain` parameter; raw-pointer cache read in
  `processBlock`.

## Out of scope

- Sample-accurate sub-block MIDI splitting → Task 06 (here, block-granular
  note handling is acceptable; note it as temporary).
- 16-voice polyphony / voice allocator → Task 05 (this task is monophonic).
- Patch loading → Task 04. PSG/DAC → Task 07.
- The full parameter set → Task 05.

## Implementation steps

1. Add ymfm to `src/CMakeLists.txt` per `06-build-system.md`.
2. Implement `GenVstYmfmInterface`.
3. Add the `ymfm::ym2612` member; `reset()` it and call `sample_rate(7670454)`
   in `prepareToPlay`; cache the native rate.
4. Implement the note-on register-sequence helper and the hard-coded patch.
   Follow the operator order and frequency byte order in `02-fm-synthesis.md`
   exactly.
5. Implement the render pipeline: native-rate `generate()` loop → single-pass
   `LagrangeInterpolator` → host buffer. Pre-allocate all buffers in
   `prepareToPlay`.
6. Add the `apvts` with `master_gain`; apply it plus a soft-clip guard at the
   end of `processBlock`.
7. Handle note-on (key the chip) and note-off (key-off) from the `MidiBuffer`.

## Deliverables

`src/GenVstYmfmInterface.h`, updates to `src/PluginProcessor.{h,cpp}`,
`src/CMakeLists.txt`. (A small `src/FmRenderEngine.{h,cpp}` or similar to hold
the chip + render pipeline is encouraged but optional.)

## Verification

1. Build `windows-debug`; `pluginval --strictness-level 8` still **passes** on
   the VST3 (no audio-thread allocation, no NaNs/denormals — pluginval checks
   these).
2. Launch the **Standalone**. Play notes from the on-screen/again MIDI keyboard
   (or route a MIDI controller): each note-on produces a sustained FM tone, each
   note-off stops it. Pitch tracks the keyboard across at least 4 octaves.
3. Sanity-check pitch: A4 (MIDI 69) sounds at approximately 440 Hz (compare
   against a reference tone or a tuner).
4. Change the host sample rate (Standalone audio settings) to 44100 and 96000;
   the tone stays in tune and is not aliased/garbled at either rate.
5. In a DAW, add the VST3 to a track, draw a few MIDI notes, play back — FM
   sound is produced; automate `master_gain` and confirm the level responds.
6. Run for ~60 s holding notes — no crash, no runaway level (soft-clip guard
   holds), no audible buffer-underrun glitches.

## Done when

- [ ] ymfm compiles inline into the plugin; `pluginval` still passes.
- [ ] A MIDI note produces a sustained FM tone in the Standalone; note-off stops it.
- [ ] Pitch tracks MIDI note number; A4 ≈ 440 Hz.
- [ ] Audio is correct at 44.1 kHz and 96 kHz host rates.
- [ ] `master_gain` exists in `apvts`, is DAW-automatable, and audibly works.
- [ ] No allocation/locks in `processBlock`; no crash under sustained play.
