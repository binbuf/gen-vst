# Task 07 — SN76489 PSG & DAC engine

> **Milestone:** All three sound chips produce sound.
> **Depends on:** Task 06.
> **Design references:** `docs/design/03-psg-synthesis.md` (primary),
> `docs/design/07-feature-spec.md` (*DAC Mode Specification*, *PSG Features*),
> `docs/design/02-fm-synthesis.md` (*DAC Mode*),
> `docs/design/01-architecture.md` (*Component Map*, *Render Pipeline*),
> ADR-0009, ADR-0011, ADR-0014.

## Objective

Add the two remaining sound sources: the **SN76489 PSG** (3 tone + 1 noise) and
the **DAC** PCM sample channel. After this task all three Genesis sound sources
work and the audio engine is complete.

## Context & key constraints

This task has two clearly separable halves — implement and verify them in order.

### Part A — SN76489 PSG

- Use **`ValleyBell/libvgm`** `emu/cores/sn764xx.c` (ADR-0009). Compile **only**
  the `sn764xx` core plus the minimal libvgm shared headers it depends on into
  the plugin target — **not** all of libvgm. Pin that exact source/header
  subset now. Add it to `src/CMakeLists.txt` per `06-build-system.md`.
- **Sega VDP variant:** feedback polynomial `0x0009`, 16-bit shift register,
  initial LFSR state `0x8000`, NTSC clock 3,579,545 Hz.
- Wrap the core behind the **`SN76489Wrapper`** C++ interface defined in
  `03-psg-synthesis.md` — `SN76489Wrapper` is the **only** code that touches
  libvgm. Map the core's real entry points when pinning the submodule.
- The PSG core is initialized with the **host sample rate** and resamples
  **internally** (ADR-0011) — it does **not** pass through the FM mix-bus
  resampler. Mix the (mono) PSG output into the stereo bus after the FM path.
- `SN76489Engine` (`03-psg-synthesis.md` *SN76489Engine Class Sketch*):
  3 tone channels round-robin (4th note steals oldest, LRU), 1 noise channel
  monophonic last-note priority; velocity→attenuation; per-channel pitch-bend
  **opt-in**; per-channel soft pan (L/R gain pair — no hardware pan); a global
  `psg_mix` level.
- **Noise control is via direct UI parameters** (`03-psg-synthesis.md` *Noise
  Control*): shift rate (2-bit) and type (periodic/white) are direct automatable
  params, not derived from pitch. The MIDI-note→shift-rate auto-mode is an
  optional toggle, **off by default**.
- Register protocol: single write port, LATCH vs DATA bytes — see
  `03-psg-synthesis.md` *Register Protocol*. MIDI note → N divider and velocity
  → attenuation formulas are in that doc.

### Part B — DAC sample channel

- DAC playback uses a **dedicated 17th `ymfm::ym2612` instance** (ADR-0014),
  separate from the 16-voice pool, reserved for DAC. It enables DACEN
  (`0x2B = 0x80`) on **its own channel 6** and is fed 8-bit PCM via `0x2A`.
- `DACPlayer` (`07-feature-spec.md` *DAC Mode Specification*): **phase-accurate
  write timing** via the `samplesPerDacWrite` accumulator in that doc.
- WAV loading: `juce::AudioFormatManager` / `AudioFormatReader`; convert to
  **8-bit PCM** (hardware limit). Hold the converted PCM in memory. (Embedding it
  in plugin state is Task 16 — not here.)
- DAC rate selectable **8000 / 11025 / 22050 Hz**; one-shot / loop mode.
- The DAC instance runs at the YM2612 native rate and is **summed into the FM
  mix bus before the single resample pass** (ADR-0011, ADR-0014).
- DAC is triggered via a dedicated MIDI channel.

### Routing & parameters

- Extend Task 06's routing table: PSG tone slots → default MIDI channels
  **11/12/13**, PSG noise → **14**, DAC → **16** (all user-configurable later).
- Extend `createParameterLayout()` with PSG params (per-channel volume, pan,
  bend-enable; noise type/rate/auto; `psg_mix`; PSG-layer toggle) and DAC params
  (enable, rate, mode, level). `psg_mix` and `dac_enable` are the globals
  referenced elsewhere in the design.

## Scope

- libvgm `sn764xx` pinned and added to the build.
- `SN76489Wrapper`, `SN76489Engine` — full PSG path mixed into the output.
- DAC: the 17th `ymfm` instance, `DACPlayer`, WAV→8-bit PCM load, phase-accurate
  playback, rate/mode controls, MIDI trigger, summed into the FM mix bus.
- Routing-table entries for PSG (11–14) and DAC (16).
- PSG + DAC parameters in `createParameterLayout()`.

## Out of scope

- The SQ (PSG) and D (DAC) **UI sections** → Task 13.
- Embedding the DAC PCM in plugin state → Task 16.
- The MIDI routing editor UI → Task 13.

## Implementation steps

1. Pin the libvgm `sn764xx` subset; add it to `src/CMakeLists.txt`.
2. Implement `SN76489Wrapper` over the core (Sega VDP LFSR config).
3. Implement `SN76489Engine` (allocation, velocity, bend, pan, mix, noise
   params); mix the PSG into the output bus.
4. Add the 17th `ymfm` instance + `DACPlayer`; enable DACEN on its channel 6.
5. Implement WAV load → 8-bit PCM and phase-accurate playback; sum the DAC
   instance into the FM mix bus before resampling.
6. Extend the routing table and `createParameterLayout()`.

## Deliverables

`src/SN76489Wrapper.{h,cpp}`, `src/SN76489Engine.{h,cpp}`,
`src/DACPlayer.{h,cpp}`, updates to `src/PluginProcessor.{h,cpp}`,
`src/CMakeLists.txt`.

## Verification

**Part A — PSG:**
1. `pluginval --strictness-level 8` passes after the libvgm core is added.
2. Standalone: notes on MIDI channels 11/12/13 produce square-wave tones; three
   notes sound together; a 4th steals the oldest tone channel.
3. A note on channel 14 produces noise; switching the noise **type** parameter
   (periodic/white) and **shift rate** audibly changes it; the auto-mode toggle
   is off by default.
4. Per-channel pan moves a PSG channel across the stereo field; `psg_mix`
   balances PSG level against FM.

**Part B — DAC:**
5. Load a short WAV via the dev path; a note on MIDI channel 16 plays the sample
   as recognizable 8-bit PCM.
6. Switching DAC rate (8000/11025/22050) changes the playback character;
   one-shot vs loop behaves correctly.
7. FM, PSG, and DAC can all sound **simultaneously** without dropouts; the DAC
   does not consume an FM voice (play 16 FM notes + DAC together).
8. `pluginval` passes with all three sources active.

## Done when

- [ ] libvgm `sn764xx` core (only) compiled in; `SN76489Wrapper` isolates it.
- [ ] PSG: 3 tone + 1 noise, allocation, velocity, pan, mix, direct noise params.
- [ ] DAC: dedicated 17th instance, WAV→8-bit PCM, phase-accurate, rate/mode.
- [ ] PSG/DAC routed on channels 11–14 / 16; params in `apvts`.
- [ ] FM + PSG + DAC all play together; `pluginval` passes.
