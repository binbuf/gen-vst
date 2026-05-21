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

`ymfm_misc.h` provides an `ym2149` (AY-3-8910 / SSG) emulator, but **not SN76489**. A separate library is required. Candidates:

| Library | License | Notes |
|---------|---------|-------|
| `vgmrips/vgmplay` `chips/sn76489.c` | Check before use | Battle-tested against VGM ecosystem; explicitly supports `FB_SEGAVDP` variant; C API matches VGM tooling |
| `ValleyBell/libvgm` `emu/cores/sn764xx.c` | LGPL | More comprehensive, covers multiple SN variants; LGPL compatible with GPL project |
| Custom implementation | MIT or public domain | ~150 lines; straightforward from public spec; ensures no license ambiguity |

> **Open question:** Decide before finalizing build system. Recommendation: start with `vgmrips/vgmplay sn76489.c` for VGM ecosystem compatibility; fall back to custom if license is unclear.

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

The vgmplay C API maps as:
```c
SN76489_Init(clock, sampleRate, feedback, shiftWidth)
SN76489_Write(chip, data)
SN76489_Update(chip, int16_t* buf, numSamples)
```

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

### Proposed MIDI Note → Noise Mapping

| MIDI note range | Shift rate | Noise type |
|-----------------|------------|------------|
| 0–37            | 10 (low)   | White      |
| 38–73           | 01 (mid)   | White      |
| 74–127          | 00 (high)  | White      |
| Even note #     | as above   | Periodic   |
| Odd note #      | as above   | White      |

> **Open question:** Alternative: expose shift rate and noise type as direct UI parameters rather than mapping from MIDI pitch, giving users full control.

---

## MIDI Routing Options

### Option A: Separate MIDI Channels (recommended)

Assign each PSG voice to a dedicated MIDI channel:

| MIDI Channel | PSG Slot | Default |
|--------------|----------|---------|
| 11           | Tone ch0 | configurable |
| 12           | Tone ch1 | configurable |
| 13           | Tone ch2 | configurable |
| 14           | Noise ch3 | configurable |

MIDI channels 1–10 remain available for FM voices.

### Option B: PSG Layer Mode

PSG voices are automatically layered on every FM note-on, creating a combined FM+PSG timbre. Less flexible but produces a richer sound with less setup.

> **Open question:** Default to Option A for explicitness; expose Option B as a "PSG Layer" toggle.

---

## PSG Voice Allocation

- **Tone ch0–ch2:** Round-robin for 3-note polyphony. 4th note steals the oldest active tone channel (LRU).
- **Noise ch3:** Monophonic, last-note priority. New note-on immediately updates noise registers and volume.

Portamento/glide on PSG: if requested, linearly interpolate N-value toward target N over a configurable number of samples. The hardware responds instantly to frequency register writes, so software-side interpolation is required.

---

## Mixing PSG into FM Output

The SN76489 produces mono output. It is mixed into the FM stereo bus:

```cpp
// After per-block generation:
for (int i = 0; i < numSamples; i++) {
    float psgSample = psgBuf[i] / 32768.0f * psgMixLevel;
    leftOut[i]  += psgSample * leftPsgGain[channel];
    rightOut[i] += psgSample * rightPsgGain[channel];
}
```

- **PSG Mix Level:** global parameter (0.0–1.0), controlling PSG contribution to the main output.
- **Per-channel soft pan:** left/right gain pair per PSG channel (no hardware pan in SN76489; entirely software).

> **Open question:** Should per-channel PSG panning be exposed as an automatable plugin parameter? Initial answer: yes, at minimum a per-channel L/R balance knob.

---

## SN76489Engine Class Sketch

```cpp
class SN76489Engine {
public:
    void prepare(double hostSampleRate);
    void reset();
    void noteOn(int psgChannel, int midiNote, int velocity);
    void noteOff(int psgChannel, int midiNote);
    void pitchBend(int psgChannel, float semitones);
    void setVolume(int psgChannel, float gain_0_to_1);
    void setNoiseMode(uint8_t registerByte);   // raw register write for noise control
    void generate(float* leftOut, float* rightOut, int numSamples);

private:
    SN76489Wrapper chip;
    float psgMixLevel;
    // per-channel state: active note, current N value, attenuation
};
```
