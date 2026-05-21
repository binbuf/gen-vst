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
- [ ] Folder-tree patch browser ([ADR-0006](adr/0006-folder-tree-patch-browser.md))
- [ ] MIDI note-on / note-off
- [ ] MIDI velocity → TL scaling (configurable on/off)
- [ ] MIDI pitch bend
- [ ] Channel 6 DAC mode (PCM via register `0x2A`)
- [ ] Polyphonic FM (multiple simultaneous FM notes)

---

## Multitimbral Architecture

Gen VST is a **six-part multitimbral** instrument ([ADR-0013](adr/0013-multitimbral-voice-model.md)):

- **6 FM parts**, each with its own patch and assigned MIDI channel.
- **16-voice shared pool** — FM polyphony is 16 notes shared across all active
  parts, not a per-part limit.
- **PSG** — 3 tone slots + 1 noise slot, each on its own MIDI channel.
- **DAC** — a dedicated sample channel on its own MIDI channel ([ADR-0014](adr/0014-special-channel-features.md)).

The MIDI-channel → destination binding table is user-configurable and persisted.

---

## Extensions Beyond Genny

### Patch Formats
- [ ] VGI patch import (adds AMS/FMS fields missing from TFI)
- [ ] VGI patch export
- [ ] DMP patch import (DefleMask format, version 11 — [ADR-0012](adr/0012-dmp-version-scope.md))
- [ ] Drag-and-drop: accept `.tfi`/`.vgi`/`.dmp` dropped onto plugin window
- [ ] Bulk folder import: drop a folder → register it as a custom patch root

### Polyphony
- [ ] 16-voice FM polyphony — a shared pool across 6 multitimbral parts ([ADR-0013](adr/0013-multitimbral-voice-model.md)), vs. Genny's hardware-limited 6
- [ ] Configurable voice count (8, 12, 16)
- [ ] Poly mode (default): LRU voice stealing
- [ ] Mono mode: last-note priority, configurable legato vs. retrigger
- [ ] Unison mode: N voices playing same note with per-voice detune spread
- [ ] Per-voice envelope retrigger option in Mono mode

### FM Features
- [ ] Channel 3 special mode (per-operator pitch for 4 independent pitches) — *post-MVP ([ADR-0014](adr/0014-special-channel-features.md))*
- [ ] SSG-EG for all 8 looping envelope shapes

### MIDI
- [ ] MIDI CC automation for all FM parameters (full map below)
- [ ] Sustain pedal (CC 64): hold voices through note-off
- [ ] All Sound Off (CC 120)
- [ ] Reset All Controllers (CC 121)
- [ ] All Notes Off (CC 123)
- [ ] Program Change: load the Nth factory patch into the part on that MIDI channel
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
- [ ] Per-part patch + MIDI-channel assignments persisted in DAW project

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
| 72 | AMS | 0–3 | per-part |
| 73 | PMS | 0–7 | per-part |
| 80 | AMON OP1 | 0/127 | |
| 81 | AMON OP2 | 0/127 | |
| 82 | AMON OP3 | 0/127 | |
| 83 | AMON OP4 | 0/127 | |
| 84 | DAC Enable | 0/127 | |
| 85 | PSG Mix Level | 0–127 | |
| 120 | All Sound Off | — | Standard (immediate silence) |
| 121 | Reset All Controllers | — | Standard |
| 123 | All Notes Off | — | Standard (with release) |

A CC affects the FM part bound to the MIDI channel it arrives on (not the UI-selected part). All CC parameters are also exposed as JUCE `AudioProcessorParameter` entries in `apvts` for full DAW automation lane support.

---

## Polyphony Modes

Polyphony mode is a **per-part** setting — each of the 6 FM parts is independently
Poly, Mono or Unison. Voices are drawn from the shared 16-voice pool
([ADR-0013](adr/0013-multitimbral-voice-model.md)).

### Poly (Default)

Standard polyphonic mode. Up to N simultaneous FM voices (configurable 8/12/16, default 16).

Voice stealing: LRU (Least Recently Used). The voice with the longest elapsed time since its note-on is stolen first. Voices in release phase are preferred for stealing over voices still in their sustain/decay phase.

### Mono

Single voice. New note-on either:
- **Retrigger:** send key-off to current voice, wait one block, send key-on with new note
- **Legato:** skip key-off; update frequency registers only; envelope continues from current level

Configurable via a "Mono Mode" toggle in the UI.

### Unison

All N voices play the same pitch simultaneously, each detuned by a per-voice
**F-number offset**. (Not the YM2612 DT register — DT is a coarse 3-bit detune and
cannot express cents; fine unison spread must be applied to the F-number.) Offsets
fan out symmetrically:

```
Voice 0: no offset
Voice 1: +spread × 1
Voice 2: -spread × 1
Voice 3: +spread × 2
Voice 4: -spread × 2
...
```

Spread is a plugin parameter in cents (0–50); each voice's F-number is computed for
its detuned pitch. Larger spread = a wider, more chorused unison.

### Chord (Optional / Post-MVP)

Split MIDI note range into zones, each triggering a different FM part configuration. Useful for playing a single key to trigger a full chord.

---

## Pitch Bend

- Bend range: configurable ±1, ±2, ±7, ±12 semitones (default ±2)
- Implementation: `semitone_offset = (bend_value / 8192.0f) × bend_range_semitones`
- Recalculate F-number and BLK for all active voices of the part on the bent MIDI channel
- Applies to FM voices only by default; enable separately for PSG voices

---

## Program Change

A Program Change message loads a factory patch into the FM part bound to the
message's MIDI channel. Program number *N* selects the **Nth factory patch** in
sorted order — the factory root provides a stable, predictable index (the browser
itself is a folder tree with no flat index, see
[ADR-0006](adr/0006-folder-tree-patch-browser.md)). Bank Select (MSB/LSB) to
address the user or custom roots is a possible later addition.

---

## DAC Mode Specification

DAC playback runs on a **dedicated `ymfm` instance** reserved for it
([ADR-0014](adr/0014-special-channel-features.md)) — it is not part of the
16-voice pool, and no FM channel is ever excluded:
1. The DAC instance has `0x2B = 0x80` (DACEN) set on its own channel 6.
2. It is triggered via a dedicated MIDI channel, consistent with PSG routing.
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

**PCM source:** Load a WAV file via `juce::AudioFormatManager` / `juce::AudioFormatReader`. Loop or one-shot playback. Limited to 8-bit resolution (hardware limitation). The converted 8-bit PCM is **embedded in plugin state** (base64 in the state XML) so a saved project is self-contained — see *State Persistence*.

---

## State Persistence

All `apvts` parameters are automatically serialized by JUCE. Custom fields appended to the XML:

```xml
<GenVstState voiceCount="16" bendRange="2">
  <parts>
    <part index="0" midiChannel="1" patchPath="…/factory/bass.tfi"/>
    <part index="1" midiChannel="2" patchPath="…/user/lead.tfi"/>
    <!-- … parts 2–5 … -->
  </parts>
  <psg ch0="11" ch1="12" ch2="13" noise="14"/>
  <dac midiChannel="16" pcm="(base64 8-bit PCM)"/>
  <customRoots>
    <root path="…"/>
  </customRoots>
  <!-- apvts parameter tree follows -->
</GenVstState>
```

`setStateInformation` restores all `apvts` parameters, re-binds each part's MIDI
channel, reloads each part's patch by path, and restores the DAC PCM. A patch path
that no longer resolves leaves the part's restored parameter values in place and
raises a notification ([05-ui-ux.md](05-ui-ux.md)).

---

## Open Questions

Most former open questions are now resolved by ADRs (see `docs/design/adr/`):
SN76489 library ([ADR-0009](adr/0009-sn76489-library.md)), ymfm instance model
([ADR-0010](adr/0010-ymfm-instance-model.md)), resampling
([ADR-0011](adr/0011-resampling-strategy.md)), DMP version scope
([ADR-0012](adr/0012-dmp-version-scope.md)), voice model
([ADR-0013](adr/0013-multitimbral-voice-model.md)), DAC and Channel 3 special
mode ([ADR-0014](adr/0014-special-channel-features.md)), patch licensing
([ADR-0004](adr/0004-furnace-only-factory-bank.md)), UI framework
([ADR-0001](adr/0001-juce8-webview-ui.md)), window size
([ADR-0007](adr/0007-fixed-window-size.md)). The PSG noise channel is controlled
by direct UI parameters (see [03-psg-synthesis.md](03-psg-synthesis.md)).

What remains:

1. **CPU profiling pass** — confirm 16 ymfm instances at 44,100 Hz are affordable;
   revisit the instance layout if not ([ADR-0010](adr/0010-ymfm-instance-model.md)).
   A post-skeleton implementation check.
2. **DMP v11 byte offsets** — verify against the Furnace source
   (`src/format/dmp.cpp`) during implementation
   ([ADR-0012](adr/0012-dmp-version-scope.md)).
3. **VGI TL range** — confirm whether OP2–OP4 TL is 0–63 or 0–127; cross-check
   against the plutiedev TFI/VGI reference (see [04-patch-system.md](04-patch-system.md)).
4. **Mono / Unison defaults** — Mono exposes both retrigger and legato; pick the
   shipped default (proposed: retrigger). Pick the default Unison spread value.
5. **Aftertouch routing default** — channel pressure is a configurable routing
   (LFO depth or carrier TL); pick the default target.

The former UI-specific open questions are now resolved: window scaling by
[ADR-0017](adr/0017-hidpi-display-scaling.md), WebView2 runtime fallback by
[ADR-0016](adr/0016-webview2-runtime-distribution.md), and SQ/D section parity by
the full view catalog in [08-ui-views.md](08-ui-views.md). Every view, popup and
sub-window is now specified there.
