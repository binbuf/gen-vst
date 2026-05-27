# PSG Synthesis — SN76489 Reference

## Hardware Overview

The **Texas Instruments SN76489** is the PSG (Programmable Sound Generator) in the Sega Genesis. In the Genesis hardware it is integrated into the Yamaha VDP chip (315-5313) as a clone with a specific LFSR configuration.

- 3 square-wave tone channels (ch0, ch1, ch2)
- 1 noise channel (ch3) — LFSR-based
- Mono output (hardware); soft-panned into stereo in software mix
- No envelope generator — instant attack/release via volume register

**NTSC clock:** 3,579,545 Hz (Genesis master 53,693,175 ÷ 15).

**Sega VDP variant specifics:**
- Feedback polynomial: `0x0009` (LFSR taps at bits 0 and 3)
- Shift register width: 16 bits
- Initial LFSR state: `0x8000`

---

## ymfm Does NOT Include SN76489

`ymfm_misc.h` provides a `ym2149` (AY-3-8910 / SSG) emulator, but **not
SN76489**. A separate library is required.

Gen VST uses **`ValleyBell/libvgm`** (`emu/cores/sn764xx.c`) — LGPL,
GPL-compatible, battle-tested across the VGM ecosystem, and covering the Sega VDP
variant. See [ADR-0009](adr/0009-sn76489-library.md) for the decision and the
alternatives weighed (`vgmrips/vgmplay`, a custom core). Only the SN76489 core is
compiled in, wrapped behind the `SN76489Wrapper` interface below so the backend
stays swappable.

### Wrapper Interface

Regardless of backend, wrap in a clean C++ interface:

```cpp
class SN76489Wrapper {
public:
    void reset();
    void write(uint8_t data);
    void generate(int16_t* buf, int numSamples);
    void setClock(uint32_t hz);
    void setSampleRate(uint32_t hz);
};
```

`SN76489Wrapper` adapts libvgm's `sn764xx` core to this interface; the core's
exact entry points are pinned when the submodule is added. The wrapper is the
**only** code that touches libvgm directly — `SN76489Engine` and the rest of the
plugin depend on `SN76489Wrapper` alone.

The core is initialized with the host sample rate (`setSampleRate`) and resamples
**internally**, so the PSG path does not pass through the FM mix-bus resampler
([ADR-0011](adr/0011-resampling-strategy.md)).

---

## Register Protocol

The SN76489 has a single write port (no address/data split). Each byte is either a LATCH byte or a DATA byte:

**LATCH byte** (bit 7 = 1):
```
bit 7  : 1 (identifies as LATCH)
bit 6:5: channel select (00=ch0, 01=ch1, 10=ch2, 11=ch3/noise)
bit 4  : register type (0=frequency/noise, 1=volume)
bit 3:0: low 4 bits of data
```

**DATA byte** (bit 7 = 0):
```
bit 7  : 0 (identifies as DATA)
bit 5:0: upper 6 bits of frequency (completes the 10-bit value)
```

The latched channel is retained from the previous LATCH byte. Always write a LATCH byte before a DATA byte to set the target.

---

## Tone Channels (ch0–ch2)

### Frequency Register

10-bit divider value N, split across a LATCH byte (low 4 bits) and an optional DATA byte (high 6 bits).

**Output frequency:**
```
f = clock / (32 × N)      where clock = 3,579,545 Hz
```

| N value | Frequency | MIDI approx |
|---------|-----------|-------------|
| 10      | 11,186 Hz | B8          |
| 254     | 440 Hz    | A4 (middle A) |
| 1000    | 112 Hz    | A2          |
| 1023    | 109 Hz    | ~A2         |

N=0 and N=1 produce frequencies well above 20 kHz (ultrasonic / effectively silent).

**MIDI note → N:**
```cpp
double freq = 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
int N = static_cast<int>(std::round(3579545.0 / (32.0 * freq)));
N = std::clamp(N, 2, 1023);
```

### Write Sequence

```cpp
// Write 10-bit frequency for ch0:
uint8_t low4  = N & 0x0F;
uint8_t high6 = (N >> 4) & 0x3F;
sn.write(0x80 | (0 << 5) | (0 << 4) | low4);  // LATCH: ch0, freq, low nibble
sn.write(high6);                                // DATA: upper 6 bits
```

---

## Volume Register (All Channels)

Volume is set by a LATCH byte with type bit = 1.

```cpp
// Set volume for ch0 (atten 0–15):
sn.write(0x80 | (0 << 5) | (1 << 4) | (atten & 0x0F));
```

| Attenuation | Level | dB |
|-------------|-------|----|
| 0           | Max   | 0 dB |
| 1           | –2 dB | |
| …           | …2 dB/step | |
| 15          | Silent | –∞ |

**MIDI velocity → attenuation:**
```cpp
int atten = static_cast<int>(std::round((1.0f - velocity / 127.0f) * 15.0f));
```

Note-off is implemented by setting attenuation to 15 (hardware has no key-off concept).

---

## Noise Channel (ch3)

The noise channel register is a LATCH byte for ch3, type=frequency (bit4=0):

```
bit 7  : 1 (LATCH)
bit 6:5: 11 (ch3)
bit 4  : 0 (noise/freq register)
bit 3  : noise type: 0=periodic, 1=white noise
bit 2:1: shift rate (00, 01, 10, 11)
bit 0  : unused (0)
```

### Shift Rates

| Bits 2:1 | Mode | Approx frequency |
|----------|------|-----------------|
| 00       | Fixed high (N/512) | ~6,991 Hz |
| 01       | Fixed mid (N/1024) | ~3,496 Hz |
| 10       | Fixed low (N/2048) | ~1,748 Hz |
| 11       | Linked to ch2 frequency | Variable |

Shift rate 11 uses the tone channel 2's frequency divider as the LFSR clock, enabling synchronized noise sweep effects.

### Noise Type

- **Periodic** (bit 3 = 0): deterministic buzz. Same tap feedback as tone; produces a pitched buzzy sound.
- **White noise** (bit 3 = 1): LFSR random noise using feedback `0x0009` (Sega variant).

Writing any value to the noise register resets the LFSR to the initial state `0x8000`, producing a consistent noise character at the start of each note.

### Volume

Noise volume uses the same 4-bit attenuation register as tone channels:
```cpp
sn.write(0x80 | (3 << 5) | (1 << 4) | (atten & 0x0F));
```

### Noise Control — Direct UI Parameters

Shift rate (2 bits) and noise type (periodic/white) are exposed as **direct UI
parameters**, not derived from MIDI pitch. This gives users explicit control and
keeps the noise channel predictable; both are automatable `apvts` parameters like
everything else.

An **optional** auto-mode maps MIDI note → shift rate as a convenience, for
players who want to "play" the noise channel from a keyboard:

| MIDI note range | Shift rate |
|-----------------|------------|
| 0–37            | 10 (low)   |
| 38–73           | 01 (mid)   |
| 74–127          | 00 (high)  |

Auto-mode is off by default — the direct parameters are the primary interface.

---

## MIDI Routing in v2

Under [ADR-0021](adr/0021-three-mode-single-engine-ui.md), each plugin
instance runs exactly one mode. **SQ mode** receives MIDI on the host
channel — the plugin is not channel-filtered, and there is no per-PSG-slot
MIDI channel binding. Routing across the four PSG channels (3 tone slots +
1 noise) happens **inside the SQ engine** via the allocation rules below
(round-robin LRU across tone slots; last-note priority on noise).

To play several PSG timbres in a project, instantiate the plugin multiple
times — each instance is its own SQ mode patch. The "PSG layered on every
FM note-on" option (v1's Option B) is **removed** in v2; FM and SQ are
separate modes and never share an instance.

---

## PSG Voice Allocation

- **Tone ch0–ch2:** Round-robin for 3-note polyphony. 4th note steals the oldest active tone channel (LRU).
- **Noise ch3:** Monophonic, last-note priority. New note-on immediately updates noise registers and volume.

**MIDI note dispatch (single-instance composite voice).** Within one SQ
instance, incoming MIDI notes split between the 3 tone channels and the
1 noise channel by a configurable note threshold (`noise_split_note`
apvts param, default MIDI 47 = B2). Notes ≤ threshold route to noise
(monophonic, last-note priority); notes > threshold route to the tone
pool (round-robin LRU, polyphonic). This matches the chiptune-tracker
convention of using the low end of the keyboard for percussion / noise
hits while reserving the upper range for tone melodies.

Portamento/glide on PSG: if requested, linearly interpolate N-value toward target N over a configurable number of samples. The hardware responds instantly to frequency register writes, so software-side interpolation is required.

---

## Mixing the four PSG channels into stereo output (SQ mode)

The SN76489 produces mono output per channel. In SQ mode the four channels
(3 tone + 1 noise) are summed into the stereo output bus with software
panning:

```cpp
// After per-block generation:
for (int i = 0; i < numSamples; i++) {
    float psgSample = psgBuf[i] / 32768.0f * psgChannelVol;
    leftOut[i]  += psgSample * leftPsgGain[channel];
    rightOut[i] += psgSample * rightPsgGain[channel];
}
```

- **Per-channel volume:** apvts param `psg_vol_ch1..3` / `psg_vol_noise`,
  multiplied per channel.
- **Per-channel soft pan:** left/right gain pair per PSG channel
  (`psg_pan_*`); no hardware pan in SN76489, entirely software.
- **Master volume:** the global `master_volume` apvts param applies after
  the sum, before the output filter.

The v1 `psg_mix` "PSG contribution to the FM output" parameter is **retired**
— v2 has no per-instance FM+PSG mix because the two never share an instance
([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).

---

## SN76489Engine Class Sketch

```cpp
class SN76489Engine {
public:
    void prepare(double hostSampleRate);
    void reset();

    // Note dispatch.
    void noteOnTone(int midiNote, int velocity);
    void noteOffTone(int midiNote);
    void setPitchBendSemitones(int psgChannel, double semitones);

    // Per-block apvts → engine push. The processor is contractually
    // responsible for calling these every renderSqBlock so live UI
    // edits and preset loads take effect — the engine does not read
    // apvts itself.
    void setEnvelopeRates(int ch, int atk, int dr1, int sus, int dr2, int rr);
    void setEnvelopeVel  (int ch, float depth_0_to_1);
    void setChannelVolume(int ch, float gain_0_to_1);
    void setChannelPan   (int ch, float pan_minus1_to_plus1);
    void setGlideTimeMs  (int toneCh, double ms);
    void setNoiseType    (int periodic_0_or_white_1);
    void setNoiseShiftRate(int rateIndex);
    void setNoiseAutoMode (bool on);

    // Audio.
    void renderAdd(float* leftOut, float* rightOut, int numSamples);

private:
    SN76489Wrapper chip;
    // per-channel state: active note, current N value, envelope follower,
    // soft-pan gains
};
```

**Per-block parameter snapshot contract.** `PluginProcessor::renderSqBlock`
must push every per-channel apvts value into the engine via the
`set*` calls above on each block — `SN76489Engine` deliberately
does not read the apvts itself (it has no apvts dependency and is
test-buildable in isolation). Skipping the push leaves the engine on
its prepare-time defaults so `.psg` preset loads or panel-knob edits
update the apvts but produce no audible change; this was the
root cause of an early-MVP "presets change sound but UI doesn't
follow" report (regression tests in
`tests/PsgEnvelopeTests.cpp` cover pan + channel-volume reach).
