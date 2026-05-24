# Architecture

## System Overview

Gen VST is a JUCE-based plugin emulating the Sega Genesis sound hardware. It
is a **three-mode single-engine instrument** ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)):
each plugin instance runs exactly one of three modes — **FM**, **SQ**, or
**D** — selected via the `mode_select` apvts parameter and persisted with
the project.

| Mode | Engine | Role |
|---|---|---|
| **FM** | `ymfm::ym2612` 16-voice pool | Single-patch FM synth (RYM2612-style) |
| **SQ** | libvgm `sn764xx` core | Single-patch PSG synth — 3 tone + 1 noise channels |
| **D** | Pure DSP — `DspDecimator` + Ladder | PCM2612-style audio FX: takes the audio input bus and runs it through sample-rate decimation + 8-bit quantization + MONO + DRY/WET |

To play multiple Genesis timbres in a project, the user instantiates the
plugin once per timbre — native DAW workflow, one track per sound.

Plugin formats: VST3 (Windows, macOS, Linux) and AU (macOS only).
JUCE flags: `IS_SYNTH TRUE` (the plugin can still emit instrument output),
`NEEDS_MIDI_INPUT TRUE`. **The plugin also declares an audio input bus** so
that D mode has a source to process; in FM and SQ modes the input bus is
present but unread.

Gen VST models the **NTSC** Genesis. The PAL system (a slightly different
master clock) is out of scope.

---

## Component Map

```
GenVstAudioProcessor
├── juce::AudioProcessorValueTreeState (apvts)
│   ├── mode_select  (FM | SQ | D)
│   ├── FM params  (single patch — ~50 params + fm_dac_prescaler + channel_tl)
│   ├── SQ params  (per-channel envelope + tuning)
│   ├── D params   (prescaler, mono, dry_wet)
│   └── globals    (output_filter, ladder_effect, master_volume)
├── VoiceAllocator
│   └── voice pool: ymfm::ym2612 instance[0..15]  (1 channel/instance — ADR-0010)
├── SN76489Engine
│   └── SN76489Wrapper × 4  ←  wraps libvgm sn764xx (ADR-0009)
├── DspDecimator   ←  D mode: sample-rate decimation + 8-bit quantizer
├── LadderEffect   ←  YM2612 stepwise nonlinearity (FM voice sum + D output)
├── OutputFilter   ←  Model-1 RC lowpass + amp coloration (mix bus, all modes)
├── FmMixBus + Resampler  ←  sum FM voices at native rate, one resample pass (ADR-0011)
└── GenVstAudioProcessorEditor
    └── juce::WebBrowserComponent  (see docs/design/05-ui-ux.md)
```

`GenVstAudioProcessor` does **not** subclass `juce::Synthesiser` — the
hardware-accurate register model requires direct control over voice lifetime
and register sequencing that `Synthesiser` abstracts away.

**Retired in v2** (deleted from the codebase per ADR-0021):
`PartManager`, `DACPlayer`, `DACKit`, `MidiRouter` (collapsed into the
processor's host-channel handling).

---

## Mode dispatch

`mode_select` is read once at the top of `processBlock`. The active engine
processes the entire block; the other two engines are bypassed (no register
writes, no `generate()` calls). This is a per-block branch, cheap and
predictable.

```cpp
switch (currentMode()) {
    case Mode::FM: renderFM(buffer, midi); break;
    case Mode::SQ: renderSQ(buffer, midi); break;
    case Mode::D:  renderD(buffer);        break;  // MIDI ignored in D mode
}
```

When the user switches modes (via the header selector or by loading a tagged
preset), the apvts param updates on the message thread; the audio thread
picks up the new mode on the next block. A short fade-out / fade-in is
applied to avoid clicks at the mode boundary.

---

## Voice and channel models (per mode)

### FM mode

- **16 ymfm::ym2612 instances**, each using only channel 0 ([ADR-0010](adr/0010-ymfm-instance-model.md)).
- All 16 voices play the **one** active patch — collapsed from v1's six-part
  multitimbral model. The patch lives on the processor; no PartManager.
- Polyphony: 16 voices. Voice stealing: global LRU across the pool. Voices
  in release phase are preferred for stealing.
- MIDI events from the host channel (any channel — the plugin is not
  channel-filtered) go straight to the voice allocator.

### SQ mode

- **4 SN76489 wrappers** with mute-mask isolation for per-channel pan/volume,
  as shipped in Task 07.
- 3 tone channels: round-robin LRU allocation, polyphonic across all three.
- 1 noise channel: last-note priority.
- Per-channel software ADSR envelope (Task 23) multiplied into the mix.

### D mode

- **No voice model.** D mode is purely DSP on the audio input bus.
- MIDI is ignored.
- Signal flow: input → optional MONO collapse → `DspDecimator`
  (sample-rate decimation + 8-bit quantization) → optional `LadderEffect`
  (if global toggle on) → DRY/WET blend with the unprocessed input →
  `OutputFilter` (if global toggle on) → output.

---

## Clocks and Sample Rates

| Chip     | Input clock (NTSC) | Native output rate |
|----------|--------------------|--------------------|
| YM2612   | 7,670,454 Hz       | ~53,267 Hz         |
| SN76489  | 3,579,545 Hz       | ~223,722 Hz        |

Both clocks are derived from the Genesis master clock (53,693,175 Hz / 7 and
/ 15 respectively).

D mode does not use a chip clock — its sample-rate decimation is computed
directly against the host sample rate (e.g., `prescaler = 0.5` → keep every
2nd input sample, hold value for the rest).

---

## JUCE AudioProcessor Architecture

```cpp
class GenVstAudioProcessor : public juce::AudioProcessor {
public:
    juce::AudioProcessorValueTreeState apvts;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void releaseResources() override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

private:
    VoiceAllocator voiceAllocator;  // 16-voice ymfm pool (FM mode)
    SN76489Engine  psgEngine;       // SQ mode
    DspDecimator   decimator;       // D mode
    LadderEffect   ladder;          // FM + D output stages
    OutputFilter   outputFilter;    // mix bus, all modes
    // single FM mix-bus resampler
};
```

`getBusesProperties()` declares **both** an input and an output stereo bus
(the input is silent in FM/SQ). Most hosts (Reaper, Bitwig, Logic, Cubase)
handle "instrument with audio input" natively. Host-specific quirks are
flagged for v2/02 verification.

`apvts` is initialized in the constructor via `createParameterLayout()`. All
plugin parameters live in the `apvts` tree and are therefore automatically
DAW-automatable and state-persistent.

---

## Parameter System

The v2 `apvts` holds:

- **Mode** — `mode_select` (choice: FM | SQ | D).
- **FM params** — a single patch's worth, ~70 params total. Core per-operator
  (TL/MUL/DT/AR/DR/SR/RR/SL/KS/SSG-EG/AMON), per-channel (ALG/FB/AMS/PMS),
  global LFO; plus the v2 RYM2612-modelled additions
  (`freq_ctrl_mode`, `retrig_rate`, per-op `mul_float` / `fixed` /
  `freq_fixed_hz`, per-op `vel` TL-modulation depth, the FM DAC prescaler
  `fm_dac_prescaler`, and the UI-only channel-TL master `channel_tl` —
  see [`02-fm-synthesis.md`](02-fm-synthesis.md) § *DAC Prescaler (FM
  mode)* and § *Channel TL (UI master-level convenience)*). Two earlier
  draft params were removed during the post-mockup review: the per-op
  `mw[op]` TL-modulation column (modwheel is now a global-only input,
  matching the RYM2612 reference) and the global `mw_to_pms` depth knob
  (modwheel routes to the LFO `PMS` field at full depth, no adjustable
  scaler). See
  [`04-patch-system.md`](04-patch-system.md) § *FM Patch Data Model* for
  the full field list. **No `_part<n>` suffix** — v1's multi-part naming
  is retired.
- **SQ params** — per-channel envelope (ATK/DR1/SUS/DR2/RR), tuning, volume,
  pan; noise type/rate.
- **D params** — `prescaler` (0.0–1.0), `mono` (bool), `dry_wet` (0.0–1.0).
- **Globals** — `output_filter` (bool), `ladder_effect` (bool),
  `master_volume`, plus UI-only `tooltips_enabled` (bool, default true)
  bound to the header `TIPS` toggle + the Settings `TOOLTIPS` row;
  storing it in apvts gives state-save/restore for free and the audio
  thread simply ignores it.

All three modes' parameters coexist in the apvts even though only one mode is
audible at a time. This is intentional — switching modes mid-session keeps
the inactive modes' settings around for when the user switches back.

Parameters are declared once in `createParameterLayout()` and accessed at
audio-thread speed via a raw pointer cache:

```cpp
// cached in prepareToPlay:
param_tl_op1 = apvts.getRawParameterValue("tl_op1");

// In processBlock (no locks, no heap allocation):
float tl = *param_tl_op1;
```

Dirty tracking: each FM voice holds a shadow copy of the last-written
register values. `processBlock` diffs the active patch's params against the
voice's shadow and writes only changed registers to ymfm.

---

## MIDI Pipeline

MIDI is processed **sample-accurately** within each block. The
`juce::MidiBuffer` is iterated in timestamp order; between consecutive MIDI
events, audio is rendered for the gap duration.

```
processBlock(buffer, midi):
  if currentMode == D:
    runD(buffer)                  // MIDI ignored entirely
    return

  for each MIDI event at sample offset T:
    render [lastOffset .. T) into the buffer
    dispatch event (no channel filtering — host channel is the only channel):
      noteOn    → engine.noteOn(note, vel)
      noteOff   → engine.noteOff(note)
      pitchBend → engine.pitchBend(value)
      CC        → apply per the CC map in 07-feature-spec.md
      progChange→ load the Nth tagged preset of the active mode
  render [lastOffset .. blockEnd)
```

There is no channel→destination routing table in v2. The collapsed MIDI
router is a thin shim in the processor; the v1 `MidiRouter.{h,cpp}` is
deleted.

Program Change loads the Nth patch **of the currently active mode** — see
[07-feature-spec.md](07-feature-spec.md). PC does not switch modes.

CC 64 (sustain pedal), CC 120/121/123 (all-sound-off / reset-controllers /
all-notes-off) behave as before, scoped to the active engine.

---

## Resampling

**FM mode.** All 16 voices generate at the YM2612 native rate (~53,267 Hz),
sum into one FM mix bus at that rate, then resample to host rate in a
**single pass** ([ADR-0011](adr/0011-resampling-strategy.md)) via
`juce::LagrangeInterpolator`.

**SQ mode.** The SN76489 wrappers resample **internally** (each is
initialised with the host sample rate). The PSG bypasses the FM mix-bus
resampler.

**D mode.** No chip; signal stays at host sample rate throughout. The
decimator's "sample-rate reduction" is implemented as a sample-and-hold at a
divisor of the host rate, *not* an actual resample pass.

---

## Render Pipeline (per mode, per block)

### FM mode

```
1. Drain the patch queue (apply patches loaded on the message thread).
2. For each sub-block between MIDI events, at native rate:
   - For each active voice: write dirty registers, ymfm.generate(),
     accumulate int32 output → native-rate FM mix buffer.
3. LadderEffect (if global toggle on) — per-channel stepwise nonlinearity.
4. Resample native-rate FM mix → host rate (single pass).
5. OutputFilter (if global toggle on) — Model-1 RC lowpass + amp coloration.
6. Apply master gain and a soft-clip guard.
7. Write to output buffer.
```

### SQ mode

```
1. For each sub-block between MIDI events, at host rate:
   - SN76489Engine.generate(numSamples).
2. OutputFilter (if global toggle on).
3. Master gain + soft-clip.
4. Write to output buffer.
```

### D mode

```
1. Copy input buffer → working buffer.
2. If MONO: average L/R → both channels.
3. DspDecimator.process(working) — sample-and-hold + 8-bit quantize.
4. LadderEffect (if global toggle on) — applied to the quantized signal.
5. DRY/WET blend with the original input.
6. OutputFilter (if global toggle on).
7. Master gain + soft-clip.
8. Write to output buffer.
```

All working buffers are pre-allocated in `prepareToPlay` — never
heap-allocated in `processBlock`.

---

## Threading Model

| Thread | Responsibilities |
|--------|-----------------|
| Audio thread | `processBlock`, register writes to ymfm/SN76489, decimator, ladder, filter, resampler |
| Message thread | Patch loading, state save/restore, UI parameter changes, mode switches |

Patch loads are non-trivial (file I/O, struct construction) and happen on
the message thread. The loaded patch is passed to the audio thread via a
`juce::AbstractFifo`-based lock-free single-producer/single-consumer queue.
The audio thread drains this queue at the start of each block.

Mode switches travel the same path: an apvts change on the message thread
flips `mode_select`; the audio thread reads it at the top of the next block
and fades over to the new engine.

`apvts` parameter reads in `processBlock` use `std::atomic<float>` — safe
from any thread.

No heap allocation, no mutexes, no blocking calls in `processBlock`.

---

## State Persistence

```cpp
void getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();   // mode + FM + SQ + D + global params
    auto xml = state.createXml();
    // append custom fields:
    //   - active patch path (one path; the patch's extension implies its tag)
    //   - registered custom patch-root folder paths
    juce::MemoryOutputStream stream(destData, true);
    xml->writeTo(stream);
}
```

All `apvts` parameters round-trip automatically. The active patch is
referenced **by path** ([ADR-0006](adr/0006-folder-tree-patch-browser.md));
the patch's extension determines its tag and therefore implies the mode
([ADR-0025](adr/0025-tagged-preset-browser.md)).

**No embedded base64 PCM** in v2 — D mode does not load WAV files
([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).

If a saved patch path no longer resolves on load, the instance keeps its
restored apvts values (so the previous sound is approximated) and a
notification toast is shown.
