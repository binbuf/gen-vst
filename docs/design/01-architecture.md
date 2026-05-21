# Architecture

## System Overview

Gen VST is a JUCE-based VST3/AU instrument plugin emulating the Sega Genesis sound hardware:

- **YM2612** (OPN2) — 6-channel FM synthesis, 4 operators per channel
- **SN76489** (PSG) — 3 square-wave tone channels + 1 noise channel

Gen VST is a **six-part multitimbral instrument** (see [ADR-0013](adr/0013-multitimbral-voice-model.md)):
6 FM parts, each with its own patch and MIDI channel, plus the PSG and a DAC
sample channel. FM polyphony is a shared pool of 16 voices — see *Parts and
Voices* below.

Plugin formats: VST3 (Windows, macOS, Linux) and AU (macOS only).
JUCE flags: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`.

Gen VST models the **NTSC** Genesis. The PAL system (a slightly different master
clock) is out of scope.

---

## Component Map

```
GenVstAudioProcessor
├── juce::AudioProcessorValueTreeState (apvts)  ← 6 parts' FM params + PSG + DAC + global
├── PartManager
│   └── Part[0..5]   ←  patch + assigned MIDI channel, one per FM part
├── VoiceAllocator
│   ├── voice pool: ymfm::ym2612 instance[0..15]  (1 channel/instance — ADR-0010)
│   └── (pool LRU steal, part↔MIDI-channel binding, register write sequencing)
├── SN76489Engine
│   └── SN76489Wrapper  ←  wraps libvgm sn764xx (ADR-0009)
├── DACPlayer
│   └── dedicated 17th ymfm::ym2612 instance  ←  DAC on its channel 6 (ADR-0014)
├── FmMixBus + Resampler  ←  sum voices at native rate, one resample pass (ADR-0011)
└── GenVstAudioProcessorEditor
    └── juce::WebBrowserComponent  (see docs/design/05-ui-ux.md)
```

`GenVstAudioProcessor` does **not** subclass `juce::Synthesiser` — the
hardware-accurate register model requires direct control over voice lifetime and
register sequencing that `Synthesiser` abstracts away.

---

## Parts and Voices

Gen VST separates **parts** (timbres) from **voices** (sounding notes). See
[ADR-0013](adr/0013-multitimbral-voice-model.md).

- **Parts.** There are 6 FM parts. Each part owns one `Patch` and one assigned
  MIDI channel. The 6 parts are the `CHANNELS 1–6` the UI edits one at a time.
  The PSG (4 slots) and the DAC are addressed on their own MIDI channels — see
  [03-psg-synthesis.md](03-psg-synthesis.md) and
  [ADR-0014](adr/0014-special-channel-features.md).
- **Voice pool.** There are 16 `ymfm::ym2612` instances, each using only channel 0
  ([ADR-0010](adr/0010-ymfm-instance-model.md)). Voices are **not** statically
  owned by parts.
- **Allocation.** On note-on: the part bound to the event's MIDI channel is
  identified → a free voice is taken from the pool (LRU steal if none free) → the
  voice is loaded with that part's patch register values and keyed on. The voice
  records which part and note it serves.
- **Polyphony.** 16 notes total, shared across all active parts. This is the
  "16 voices, beyond Genny's hardware 6" target — the *pool size*, not a per-part
  limit. Per-part voice caps are a possible post-MVP refinement.

Voice stealing: global LRU across the pool. Voices in release phase are preferred
for stealing over voices still in sustain/decay.

The PSG has its own allocation (3 tone slots round-robin + 1 noise slot,
last-note priority), described in [03-psg-synthesis.md](03-psg-synthesis.md).

---

## Clocks and Sample Rates

| Chip     | Input clock (NTSC) | Native output rate |
|----------|--------------------|--------------------|
| YM2612   | 7,670,454 Hz       | ~53,267 Hz         |
| SN76489  | 3,579,545 Hz       | ~223,722 Hz        |

Both clocks are derived from the Genesis master clock (53,693,175 Hz / 7 and / 15
respectively).

`ym2612::sample_rate(7670454)` returns the exact native rate. The host DAW may run
at 44,100, 48,000, 88,200 or 96,000 Hz — resampling is required (see *Resampling*).

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
    PartManager    partManager;     // 6 FM parts: patch + MIDI channel each
    VoiceAllocator voiceAllocator;  // 16-voice ymfm pool
    SN76489Engine  psgEngine;
    DACPlayer      dacPlayer;       // dedicated ymfm instance
    // single FM mix-bus resampler
};
```

`apvts` is initialized in the constructor via `createParameterLayout()`. All
plugin parameters — per-part FM operator/channel values, LFO, PSG, DAC — live in
the `apvts` tree and are therefore automatically DAW-automatable and
state-persistent.

---

## Parameter System

The `apvts` holds a **full FM parameter set for each of the 6 parts**. Parameter
IDs follow the pattern `<name>_op<1-4>_part<1-6>` for per-operator params,
`<name>_part<1-6>` for per-part channel params, and `<name>` for globals
(e.g. `psg_mix`, `dac_enable`). This is ~50 FM params per part (≈300 FM
parameters), plus PSG, DAC and global params — within VST3/AU limits.

Parameters are declared once in `createParameterLayout()` and accessed at
audio-thread speed via a raw pointer cache:

```cpp
// cached per part:
param_tl_op1[part] = apvts.getRawParameterValue("tl_op1_part" + juce::String(part + 1));

// In processBlock (no locks, no heap allocation):
float tl = *param_tl_op1[partIndex];
```

The UI edits one part at a time; FM-channel relays are named **without** the
`_part<n>` suffix and rebind on part selection — see
[05-ui-ux.md](05-ui-ux.md) (*FM channel paging*).

Dirty tracking: each `Voice` holds a shadow copy of the last-written register
values. `processBlock` diffs the relevant part's params against each voice's
shadow and writes only changed registers to ymfm, avoiding redundant writes on
every block.

---

## MIDI Pipeline

MIDI is processed **sample-accurately** within each block. The `juce::MidiBuffer`
is iterated in timestamp order; between consecutive MIDI events, audio is rendered
for the gap duration.

```
processBlock:
  for each MIDI event at sample offset T:
    render FM mix bus + PSG + DAC for samples [lastOffset .. T)
    dispatch event by its MIDI channel:
      noteOn    → part = routeForChannel(ch);
                  voiceAllocator.noteOn(part, note, vel)   // or PSG slot / DAC trigger
      noteOff   → voiceAllocator.noteOff(part, note)
      pitchBend → voiceAllocator.pitchBend(part, value)
      CC        → apply to the part bound to ch (see docs/design/07-feature-spec.md)
      progChange→ patchSystem.loadProgram(part, program)   // Nth factory patch
  render voices for samples [lastOffset .. blockEnd)
```

Each MIDI channel maps to exactly one destination: one of the 6 FM parts, one of
the 4 PSG slots, or the DAC channel. The binding table is user-configurable and
persisted (see [07-feature-spec.md](07-feature-spec.md)).

Pitch bend recalculates the F-number and BLK registers for every active voice
belonging to the bent part.

CC 64 (sustain pedal): holds a part's voices through note-off until the pedal is
released. CC 120/123: all-sound-off / all-notes-off — immediately silence and free
all voices.

Program Change loads a factory patch into the part on that MIDI channel — the
program-number mapping is in [07-feature-spec.md](07-feature-spec.md).

---

## Resampling

All 16 FM voice instances and the dedicated DAC instance generate audio at the
YM2612 native rate (~53,267 Hz). They are summed into one **FM mix bus** at that
rate, then resampled to the host rate in a **single pass**
([ADR-0011](adr/0011-resampling-strategy.md)).

Resampling is linear, so summing then resampling is equivalent to resampling each
voice then summing — at roughly 1/16 the cost. The resampler is a
`juce::Interpolator` (`juce::LagrangeInterpolator` to start;
`juce::WindowedSincInterpolator` if profiling shows audible aliasing).
`juce::ResamplingAudioSource` is **not** used — it is a pull-model `AudioSource`
that does not fit this push-model render loop.

The SN76489 PSG resamples **internally** (its core is initialized with the host
sample rate), so the PSG does not pass through the FM mix-bus resampler.

---

## Render Pipeline (per block)

```
processBlock(buffer, midiBuffer):
  1. Drain the patch queue (apply patches loaded on the message thread).
  2. Collect MIDI events with sample timestamps.
  3. For each sub-block [start..end] between MIDI events, at the native rate:
     a. For each active FM voice: write dirty registers, ymfm.generate(),
        accumulate int32 output → native-rate FM mix buffer.
     b. DACPlayer.process(): feed PCM to the DAC instance, generate,
        accumulate into the same native-rate FM mix buffer.
  4. Resample the native-rate FM mix buffer → host rate (single pass).
  5. SN76489Engine.generate() at host rate; mix PSG (scaled) into the output.
  6. Apply master gain and a soft-clip guard.
  7. Write the working buffer → juce::AudioBuffer<float> (L, R channels).
```

All working buffers are pre-allocated in `prepareToPlay` — never heap-allocated in
`processBlock`.

Summing 16 voices plus the DAC can exceed unity; per-voice scaling and the master
soft-clip guard keep the mix bounded. Final headroom tuning is an implementation
task.

---

## Threading Model

| Thread | Responsibilities |
|--------|-----------------|
| Audio thread | `processBlock`, register writes to ymfm/SN76489, resampler |
| Message thread | Patch loading, state save/restore, UI parameter changes |

Patch loads are non-trivial (file I/O, struct construction) and happen on the
message thread. The loaded `Patch` — tagged with its target part — is passed to
the audio thread via a `juce::AbstractFifo`-based lock-free single-producer/
single-consumer queue. The audio thread drains this queue at the start of each
block.

`apvts` parameter reads in `processBlock` use `std::atomic<float>` — safe from any
thread.

No heap allocation, no mutexes, no blocking calls in `processBlock`.

---

## State Persistence

```cpp
void getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();   // all per-part FM/PSG/DAC params
    auto xml = state.createXml();
    // append custom fields:
    //   - per part: assigned MIDI channel + active patch path
    //   - registered custom patch-root folder paths
    //   - DAC: embedded 8-bit PCM (base64) so the project is self-contained
    juce::MemoryOutputStream stream(destData, true);
    xml->writeTo(stream);
}

void setStateInformation(const void* data, int sizeInBytes) {
    // parse XML, restore apvts, re-bind MIDI channels,
    // reload each part's patch by path, restore DAC PCM
}
```

All `apvts` parameters round-trip automatically. Patches are referenced **by
path**, not by a flat bank index — the browser is a folder tree
([ADR-0006](adr/0006-folder-tree-patch-browser.md)). If a saved patch path no
longer resolves on load, the part keeps its restored `apvts` register values and a
notification is shown ([05-ui-ux.md](05-ui-ux.md)). Custom root folders are
re-registered and re-scanned; a missing root is reported, not silently dropped.
