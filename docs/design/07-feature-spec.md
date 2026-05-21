# Feature Specification

## Genny VST Feature Parity Checklist

All features present in Genny v1.5 must be matched:

- [ ] YM2612 FM synthesis — all 6 channels
- [ ] SN76489 PSG — 3 tone channels + 1 noise channel
- [ ] All 8 FM algorithms
- [ ] Per-operator controls: DT, MUL, TL, KS, AR, DR, SR, RR, SL, SSG-EG, AMON
- [ ] Per-channel controls: ALG, FB, L/R output enable, AMS, PMS
- [ ] Global LFO: enable toggle, rate selector (8 values)
- [ ] TFI patch load
- [ ] Patch browser with bank/patch list
- [ ] MIDI note-on / note-off
- [ ] MIDI velocity → TL scaling (configurable on/off)
- [ ] MIDI pitch bend
- [ ] Channel 6 DAC mode (PCM via register `0x2A`)
- [ ] Polyphonic FM (multiple simultaneous FM notes)

---

## Extensions Beyond Genny

### Patch Formats
- [ ] VGI patch import (adds AMS/FMS fields missing from TFI)
- [ ] VGI patch export
- [ ] DMP patch import (DefleMask format, version 8 and 11)
- [ ] Drag-and-drop: accept `.tfi`/`.vgi`/`.dmp` dropped onto plugin window
- [ ] Bulk folder import: drop a folder → create new user bank

### Polyphony
- [ ] 16-voice FM polyphony (vs. Genny's hardware-limited 6)
- [ ] Configurable voice count (8, 12, 16)
- [ ] Poly mode (default): LRU voice stealing
- [ ] Mono mode: last-note priority, configurable legato vs. retrigger
- [ ] Unison mode: N voices playing same note with spread DT offset
- [ ] Per-voice envelope retrigger option in Mono mode

### FM Features
- [ ] Channel 3 special mode (per-operator pitch for 4 independent pitches)
- [ ] SSG-EG for all 8 looping envelope shapes

### MIDI
- [ ] MIDI CC automation for all FM parameters (full map below)
- [ ] Sustain pedal (CC 64): hold voices through note-off
- [ ] All Sound Off (CC 120)
- [ ] Reset All Controllers (CC 121)
- [ ] All Notes Off (CC 123)
- [ ] Program Change: load patch by bank index
- [ ] Aftertouch (channel pressure): optional map to LFO depth or carrier TL

### PSG Features
- [ ] Per-channel MIDI channel assignment (configurable)
- [ ] PSG pitch bend
- [ ] PSG velocity → attenuation mapping
- [ ] Per-PSG-channel soft panning (L/R gain)
- [ ] PSG mix level control (relative to FM output)

### UI
- [ ] Patch preview: test note on patch selection
- [ ] Per-operator inline ADSR curve preview
- [ ] Interactive algorithm diagram (click operator to select it)
- [ ] Oscilloscope waveform display (bottom strip)
- [ ] Voice activity LEDs (per FM voice, shows key-on state)

### DAC
- [ ] DAC rate selection: 8,000 / 11,025 / 22,050 Hz
- [ ] WAV sample loader for DAC playback
- [ ] Phase-accurate DAC write timing

### State
- [ ] Full DAW state save/restore (`getStateInformation`/`setStateInformation`)
- [ ] Bank/preset name persisted in DAW project

---

## MIDI CC Map

Scaling formula: `hardware_val = round(cc_val × max_val / 127.0f)`

| CC | Parameter | Hardware Range | Notes |
|----|-----------|---------------|-------|
| 1  | Mod Wheel → PMS (vibrato) | 0–7 | Standard modwheel |
| 7  | Master Volume | 0–127 | Standard volume |
| 10 | Pan (L/R) | 0–63=L, 64=C, 65–127=R | Standard pan |
| 14 | Algorithm (ALG) | 0–7 | |
| 15 | Feedback (FB) | 0–7 | |
| 16 | TL OP1 | 0–127 | |
| 17 | TL OP2 | 0–127 | |
| 18 | TL OP3 | 0–127 | |
| 19 | TL OP4 | 0–127 | |
| 20 | MUL OP1 | 0–15 | |
| 21 | MUL OP2 | 0–15 | |
| 22 | MUL OP3 | 0–15 | |
| 23 | MUL OP4 | 0–15 | |
| 24 | DT OP1 | 0–6 | |
| 25 | DT OP2 | 0–6 | |
| 26 | DT OP3 | 0–6 | |
| 27 | DT OP4 | 0–6 | |
| 28 | AR OP1 | 0–31 | |
| 29 | AR OP2 | 0–31 | |
| 30 | AR OP3 | 0–31 | |
| 31 | AR OP4 | 0–31 | |
| 32 | DR OP1 | 0–31 | |
| 33 | DR OP2 | 0–31 | |
| 34 | DR OP3 | 0–31 | |
| 35 | DR OP4 | 0–31 | |
| 36 | SR OP1 | 0–31 | |
| 37 | SR OP2 | 0–31 | |
| 38 | SR OP3 | 0–31 | |
| 39 | SR OP4 | 0–31 | |
| 40 | RR OP1 | 0–15 | |
| 41 | RR OP2 | 0–15 | |
| 42 | RR OP3 | 0–15 | |
| 43 | RR OP4 | 0–15 | |
| 44 | SL OP1 | 0–15 | |
| 45 | SL OP2 | 0–15 | |
| 46 | SL OP3 | 0–15 | |
| 47 | SL OP4 | 0–15 | |
| 48 | KS OP1 | 0–3 | |
| 49 | KS OP2 | 0–3 | |
| 50 | KS OP3 | 0–3 | |
| 51 | KS OP4 | 0–3 | |
| 64 | Sustain Pedal | 0/127 | Standard |
| 70 | LFO Enable | 0/127 | |
| 71 | LFO Rate | 0–7 | |
| 72 | AMS (active channel) | 0–3 | |
| 73 | PMS (active channel) | 0–7 | |
| 80 | AMON OP1 | 0/127 | |
| 81 | AMON OP2 | 0/127 | |
| 82 | AMON OP3 | 0/127 | |
| 83 | AMON OP4 | 0/127 | |
| 84 | DAC Enable | 0/127 | |
| 85 | PSG Mix Level | 0–127 | |
| 120 | All Sound Off | — | Standard (immediate silence) |
| 121 | Reset All Controllers | — | Standard |
| 123 | All Notes Off | — | Standard (with release) |

All CC parameters are also exposed as JUCE `AudioProcessorParameter` entries in `apvts` for full DAW automation lane support.

---

## Polyphony Modes

### Poly (Default)

Standard polyphonic mode. Up to N simultaneous FM voices (configurable 8/12/16, default 16).

Voice stealing: LRU (Least Recently Used). The voice with the longest elapsed time since its note-on is stolen first. Voices in release phase are preferred for stealing over voices still in their sustain/decay phase.

### Mono

Single voice. New note-on either:
- **Retrigger:** send key-off to current voice, wait one block, send key-on with new note
- **Legato:** skip key-off; update frequency registers only; envelope continues from current level

Configurable via a "Mono Mode" toggle in the UI.

### Unison

All N voices play the same pitch simultaneously. Each voice has a DT offset from a configurable spread table:

```
Voice 0: no offset
Voice 1: +spread × 1
Voice 2: -spread × 1
Voice 3: +spread × 2
Voice 4: -spread × 2
...
```

Spread is a plugin parameter (in cents, 0–50). Larger spread = more detuned unison / chorus effect.

### Chord (Optional / Post-MVP)

Split MIDI note range into zones, each triggering a different FM channel configuration. Useful for playing a single key to trigger a full chord.

---

## Pitch Bend

- Bend range: configurable ±1, ±2, ±7, ±12 semitones (default ±2)
- Implementation: `semitone_offset = (bend_value / 8192.0f) × bend_range_semitones`
- Recalculate F-number and BLK for all active voices on the bent MIDI channel
- Applies to FM voices only by default; enable separately for PSG voices

---

## DAC Mode Specification

When DAC mode is enabled:
1. Write `0x2B = 0x80` to enable DAC on YM2612 channel 6
2. The voice allocator excludes channel 6 from FM allocation
3. `DACPlayer::process(numSamples)` runs each block, writing 8-bit PCM samples to `0x2A` at the configured rate

**Phase-accurate timing:**
```cpp
// In DACPlayer:
double samplesPerDacWrite = hostSampleRate / dacRate;  // e.g., 48000 / 8000 = 6.0
double sampleAccumulator = 0.0;

void process(int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        sampleAccumulator += 1.0;
        if (sampleAccumulator >= samplesPerDacWrite) {
            sampleAccumulator -= samplesPerDacWrite;
            uint8_t sample = getNextSample();
            chip.write(0, 0x2A);
            chip.write(1, sample);
        }
    }
}
```

**PCM source:** Load a WAV file via `juce::AudioFormatManager` / `juce::AudioFormatReader`. Loop or one-shot playback. Limited to 8-bit resolution (hardware limitation).

---

## State Persistence

All `apvts` parameters are automatically serialized by JUCE. Custom fields appended to the XML:

```xml
<GenVstState bankName="SoR Koshiro" patchIndex="3" polyMode="poly" voiceCount="16"
             bendRange="2" psgMidiCh0="11" psgMidiCh1="12" psgMidiCh2="13" psgMidiNoise="14">
  <!-- apvts parameter tree follows -->
</GenVstState>
```

`setStateInformation` restores all parameters, then calls `patchSystem.loadByIndex()` to re-apply the saved patch's register values to the active voice.

---

## Open Questions (Consolidated)

1. **SN76489 library choice:** `vgmrips/vgmplay`, `ValleyBell/libvgm`, or custom. Decide before writing `SN76489Engine`.

2. **ymfm instance count:** 16×1-channel instances vs. 3×6-channel instances for 18 voices. Profile CPU cost of 16 instances at 44,100 Hz before committing.

3. **Resampling strategy:** `juce::ResamplingAudioSource` (easy, good quality) vs. polyphase FIR (better quality, more code). Start with JUCE resampler.

4. **Mono legato behavior:** On note-on with an active note, does the envelope restart (retrigger) or continue (legato)? Expose both options.

5. **MDDC patch licensing:** Contact community maintainers before bundling named game patches in a distributed binary.

6. **DMP v11 byte offsets:** Verify against Furnace source code (`src/format/dmp.cpp`) during implementation — community documentation has discrepancies.

7. **PSG noise → MIDI note mapping:** Fixed note-range mapping (as specified in [03-psg-synthesis.md](03-psg-synthesis.md)) vs. direct UI parameter control. Default to direct UI control for simplicity.

8. **Foleys GUI Magic adoption:** Defer to post-MVP. Start with manual LookAndFeel.

9. **Fixed vs. resizable window:** Fixed at 900×600 for MVP. Add resize support after layout is stable.

10. **Aftertouch mapping:** Channel pressure → LFO depth or carrier TL. Expose as a configurable routing option rather than hardcoding.
