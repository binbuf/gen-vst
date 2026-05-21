# Architecture

## System Overview

Gen VST is a JUCE-based VST3/AU instrument plugin emulating the Sega Genesis sound hardware:

- **YM2612** (OPN2) — 6-channel FM synthesis, 4 operators per channel
- **SN76489** (PSG) — 3 square-wave tone channels + 1 noise channel

Plugin formats: VST3 (Windows, macOS, Linux) and AU (macOS only).
JUCE flags: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`.

---

## Component Map

```
GenVstAudioProcessor
├── juce::AudioProcessorValueTreeState (apvts)
├── VoiceAllocator
│   ├── Voice[0..15]  ←  each owns a ymfm::ym2612 + ymfm_interface impl
│   └── (LRU steal, note tracking, register write sequencing)
├── SN76489Engine
│   └── SN76489Wrapper  ←  wraps chosen PSG library
├── DACPlayer           ←  feeds 8-bit PCM to YM2612 register 0x2A
├── Resampler           ←  ymfm native rate → host sample rate
└── GenVstAudioProcessorEditor
    └── (see docs/design/05-ui-ux.md)
```

`GenVstAudioProcessor` does **not** subclass `juce::Synthesiser` — the hardware-accurate register model requires direct control over voice lifetime and register sequencing that `Synthesiser` abstracts away.

---

## Clocks and Sample Rates

| Chip     | Input clock (NTSC) | Native output rate |
|----------|--------------------|--------------------|
| YM2612   | 7,670,454 Hz       | ~53,267 Hz         |
| SN76489  | 3,579,545 Hz       | ~223,722 Hz        |

Both clocks are derived from the Genesis master clock (53,693,175 Hz / 7 and / 15 respectively).

ymfm's `ym2612::sample_rate(7670454)` returns the exact native rate. The host DAW may run at 44,100, 48,000, 88,200, or 96,000 Hz — resampling is required.

---

## Polyphony Model

**FM voices:** 16 simultaneous FM notes via 16 independent `ymfm::ym2612` instances. Each instance uses only channel 0 (one voice per chip instance), so global registers (LFO, timers) do not bleed between voices. This costs more RAM and per-block CPU cycles than sharing instances but eliminates register aliasing bugs.

> **Open question:** Alternatively, use 3 instances × 6 channels = 18 voices, reducing ymfm object overhead. This complicates LFO isolation — profile before deciding.

Voice stealing: LRU (least recently used) policy. When all 16 voices are active, the oldest-playing voice is stolen.

**PSG voices:** One shared `SN76489Engine` instance with 4 slots (ch0–ch2 tone, ch3 noise). PSG uses last-note priority per slot. See [03-psg-synthesis.md](03-psg-synthesis.md) for slot assignment.

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
    VoiceAllocator voiceAllocator;
    SN76489Engine  psgEngine;
    DACPlayer      dacPlayer;
    // resampler per voice (or shared polyphase FIR)
};
```

`apvts` is initialized in the constructor via `createParameterLayout()`. All plugin parameters — FM operator values, LFO, PSG, DAC mode — live in the `apvts` tree and are therefore automatically DAW-automatable and state-persistent.

---

## Parameter System

Parameters are declared once in `createParameterLayout()` and accessed at audio-thread speed via raw pointer cache:

```cpp
// In prepareToPlay or constructor:
param_tl_op1 = apvts.getRawParameterValue("tl_op1_ch1");

// In processBlock (no locks, no heap allocation):
float tl = *param_tl_op1;
```

Parameter IDs follow the pattern `<name>_op<1-4>_ch<1-6>` for per-operator/channel params and `<name>` for globals (e.g., `lfo_enable`, `lfo_rate`).

Dirty tracking: each `Voice` holds a shadow copy of the last-written register values. `processBlock` diffs current params against shadows and only writes changed registers to ymfm. This avoids redundant writes on every block.

---

## MIDI Pipeline

MIDI is processed **sample-accurately** within each block. The `juce::MidiBuffer` is iterated in timestamp order; between consecutive MIDI events, audio is rendered for the gap duration.

```
processBlock:
  for each MIDI event at sample offset T:
    render voices for samples [lastOffset .. T)
    dispatch event:
      noteOn  → voiceAllocator.noteOn(ch, note, vel)
      noteOff → voiceAllocator.noteOff(ch, note)
      pitchBend → voiceAllocator.pitchBend(ch, value)
      CC      → handle CC map (see docs/design/07-feature-spec.md)
      progChange → patchSystem.loadByIndex(program)
  render voices for samples [lastOffset .. blockEnd)
```

Pitch bend recalculates the F-number and BLK registers for all active voices on the bent MIDI channel.

CC 64 (sustain pedal): holds voices through note-off until pedal is released.
CC 120/123: all-sound-off / all-notes-off — immediately silence and free all voices.

---

## Resampling

ymfm generates audio at ~53,267 Hz (YM2612 native). The host buffer runs at the host rate. Three strategies are viable:

| Strategy | Quality | Complexity |
|----------|---------|------------|
| Linear interpolation | Slight aliasing above ~20 kHz; inaudible for FM content | Trivial |
| `juce::ResamplingAudioSource` | Good quality, built into JUCE | Medium |
| Polyphase FIR (e.g., Genny's `Fir_Resampler`) | Best quality, minimal aliasing | Higher |

> **Open question:** Start with `juce::ResamplingAudioSource` for correctness, profile, upgrade to polyphase FIR if needed.

Each `Voice` maintains its own resampler instance to allow independent per-voice pitch variation without cross-contamination.

---

## Render Pipeline (per block)

```
processBlock(buffer, midiBuffer):
  1. Collect MIDI events with sample timestamps
  2. For each sub-block [start..end] between MIDI events:
     a. For each active FM voice:
        - Write dirty registers to ymfm
        - ymfm.generate(&out, subBlockLen)
        - Accumulate int32 output → float working buffer
     b. SN76489Engine.generate(psgBuf, subBlockLen)
        - Mix psgBuf (scaled) into working buffer
     c. DACPlayer.process(subBlockLen)  [if DAC mode active]
  3. Apply master gain, soft clip guard
  4. Write working buffer → juce::AudioBuffer<float> (L, R channels)
```

The working buffer is stack-allocated or pre-allocated in `prepareToPlay` (never heap-allocated in processBlock).

---

## Threading Model

| Thread | Responsibilities |
|--------|-----------------|
| Audio thread | `processBlock`, register writes to ymfm/SN76489, resampler |
| Message thread | Patch loading, state save/restore, UI parameter changes |

Patch loads are non-trivial (file I/O, struct construction) and happen on the message thread. The loaded `Patch` struct is passed to the audio thread via a `juce::AbstractFifo`-based lock-free single-producer/single-consumer queue. The audio thread drains this queue at the start of each block.

`apvts` parameter reads in `processBlock` use `std::atomic<float>` — safe from any thread.

No heap allocation, no mutexes, no blocking calls in `processBlock`.

---

## State Persistence

```cpp
void getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    // append custom fields: current bank name, current patch index
    auto xml = state.createXml();
    xml->setAttribute("bankName", currentBankName);
    xml->setAttribute("patchIndex", currentPatchIndex);
    juce::MemoryOutputStream stream(destData, true);
    xml->writeTo(stream);
}

void setStateInformation(const void* data, int sizeInBytes) {
    // parse XML, restore apvts state, reload patch
}
```

All `apvts` parameters are automatically round-tripped. Bank/patch selection is stored as custom XML attributes.
