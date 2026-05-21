# FM Synthesis — YM2612 / ymfm Reference

## Hardware Overview

The **Yamaha YM2612** (OPN2 — FM Operator Type-N, 2nd generation) is the FM sound chip in the Sega Genesis/Mega Drive.

- 6 FM channels, 4 operators each (24 operators total)
- 8 algorithms defining operator modulation routing
- Two register banks: Bank 0 (channels 1–3), Bank 1 (channels 4–6)
- Stereo output (left/right independently enabled per channel)
- Single global LFO for vibrato/tremolo
- Channel 6 can switch to 8-bit PCM DAC mode

**NTSC clock:** 7,670,454 Hz (Genesis master 53,693,175 Hz ÷ 7).
**Native output rate:** `ym2612::sample_rate(7670454)` ≈ **53,267 Hz**.

Gen VST uses ymfm's `ym2612` class — the discrete YM2612 with its characteristic
9-bit DAC "ladder effect" — rather than `ym3438` (the cleaner ASIC revision). The
NTSC system is modelled; PAL (a different master clock) is out of scope.

---

## ymfm OPN2 API

Include path: `#include "ymfm_opn.h"` (also requires `ymfm.h`, `ymfm_fm.h`, `ymfm_fm.ipp`).

```cpp
// One interface implementation per ymfm instance
class GenVstYmfmInterface : public ymfm::ymfm_interface {
    // No-op stubs — YM2612 does not use ADPCM external memory
    uint8_t ymfm_external_read(ymfm::access_class, uint32_t) override { return 0; }
    void ymfm_external_write(ymfm::access_class, uint32_t, uint8_t) override {}
};

// Usage
GenVstYmfmInterface intf;
ymfm::ym2612 chip(intf);

chip.reset();
uint32_t nativeRate = chip.sample_rate(7670454);  // ~53267

// Register write: offset selects bank+port
// 0 = Bank0 address, 1 = Bank0 data
// 2 = Bank1 address, 3 = Bank1 data
chip.write(0, 0x28);   // address: Key On/Off register
chip.write(1, 0xF0);   // data: all operators on, channel 0

// Generate audio
ymfm::ym2612::output_data out;
chip.generate(&out);   // 1 sample by default
// out.data[0] = left int32, out.data[1] = right int32

// Convert to float
float left  = static_cast<float>(out.data[0]) / 32768.0f;
float right = static_cast<float>(out.data[1]) / 32768.0f;

// Batch generation
std::array<ymfm::ym2612::output_data, 128> batch;
chip.generate(batch.data(), 128);
```

Convenience wrappers for Bank 0: `write_address(addr)`, `write_data(data)`.
Convenience wrappers for Bank 1: `write_address_hi(addr)`, `write_data_hi(data)`.

---

## Global Registers (Bank 0 Only)

### `0x22` — LFO Control

| Bits | Field | Description |
|------|-------|-------------|
| 3    | LFOEN | 1 = LFO enabled. Must be 1 for AMS/FMS to have any effect |
| 2:0  | LFRQ  | LFO frequency select (see table below) |

| LFRQ | Frequency |
|------|-----------|
| 0    | 3.82 Hz   |
| 1    | 5.33 Hz   |
| 2    | 5.77 Hz   |
| 3    | 6.11 Hz   |
| 4    | 6.67 Hz   |
| 5    | 9.23 Hz   |
| 6    | 10.00 Hz  |
| 7    | 69.22 Hz  |

### `0x27` — Mode / Channel 3 Special

| Bits | Field | Description |
|------|-------|-------------|
| 7:6  | CH3   | Channel 3 mode: 00=normal, 01=special/extended, 11=CSM |
| 5:4  | RST   | Timer reset (bit5=B, bit4=A) |
| 3:2  | EN    | Timer enable (bit3=B, bit2=A) |
| 1:0  | LOAD  | Timer load (bit1=B, bit0=A) |

**Special mode** (01): Channel 3's four operators can play at independent pitches using registers `0xA8`–`0xAF`. Useful for chord/arpeggio effects without consuming multiple channels. Channel 3 special mode is **deferred to post-MVP** — see [ADR-0014](adr/0014-special-channel-features.md).

### `0x28` — Key On / Off

| Bits | Field | Description |
|------|-------|-------------|
| 7:4  | OPS   | Operator enable mask: bit7=S4, bit6=S3, bit5=S2, bit4=S1. Set to 0xF0 for all operators |
| 2:0  | CH    | Channel select. Channels 1–3 → values 0–2; channels 4–6 → values 4–6 (value 3 is invalid) |

Key-on sequence: write with OPS=0x00 (key-off), then write with OPS=0xF0 (key-on). This reliably retriggers the envelope.

### `0x2A` — DAC Data

8-bit PCM sample value. ymfm internally XOR's with 0x80 to convert between the hardware's unsigned format and internal signed representation. Write raw signed bytes (or pre-XOR if needed). Must be written at the desired DAC sample rate (typically 8,000–22,050 Hz) by triggering writes from the audio block render loop.

### `0x2B` — DAC Enable

| Bits | Field | Description |
|------|-------|-------------|
| 7    | DACEN | 1 = Channel 6 outputs DAC register; FM synthesis on ch6 is silenced |

Write `0x80` to enable DAC, `0x00` to restore FM on channel 6. In Gen VST the DAC runs on a **dedicated `ymfm` instance** reserved for it ([ADR-0014](adr/0014-special-channel-features.md)), so no FM voice is ever displaced.

### `0x2C` — DAC LSB (test register)

Bit 3 provides the 9th bit of the DAC word for extended precision. Rarely used in practice.

---

## Per-Operator Registers (`0x30`–`0x9F`)

Operators are named S1, S2, S3, S4. Their **register offsets are non-sequential**:

| Operator | Offset |
|----------|--------|
| S1       | +0x00  |
| S3       | +0x04  |
| S2       | +0x08  |
| S4       | +0x0C  |

This is a common source of bugs — S2 and S3 are swapped relative to their numbers.

Address formula: `base_reg + (channel % 3) + operator_offset`. For Bank 1 (ch4–6), write to the Bank 1 port instead of Bank 0.

### `0x30` — DT / MUL

| Bits | Field | Description |
|------|-------|-------------|
| 6:4  | DT    | Detune. Values 0–3 = none to +fine, 4 = same as 0, 5–7 = −fine |
| 3:0  | MUL   | Frequency multiple. 0=×0.5, 1=×1, 2=×2 … 15=×15 |

### `0x40` — TL (Total Level)

| Bits | Field | Description |
|------|-------|-------------|
| 6:0  | TL    | Attenuation in 0.75 dB steps. 0=loudest (0 dB), 127≈−95 dB (silence) |

For **carrier** operators, TL directly controls output volume. For **modulator** operators, TL controls modulation depth. Typical patch editing reduces modulator TL to reduce brightness/FM sidebands.

### `0x50` — KS / AR

| Bits | Field | Description |
|------|-------|-------------|
| 7:6  | KS    | Key Scale (0–3). Higher = faster envelopes at higher pitches |
| 4:0  | AR    | Attack Rate (0=no attack, 31=instant) |

KS adds `(key_code >> (3 - KS))` to all rate values, making envelopes velocity/pitch-sensitive.

### `0x60` — AMON / DR

| Bits | Field | Description |
|------|-------|-------------|
| 7    | AMON  | 1 = LFO amplitude modulation affects this operator |
| 4:0  | DR    | First Decay Rate (0=no decay, 31=fastest) |

### `0x70` — SR (Second Decay / Sustain Rate)

| Bits | Field | Description |
|------|-------|-------------|
| 4:0  | SR    | Rate of level decrease after sustain level is reached. 0=hold, 31=fastest |

### `0x80` — SL / RR

| Bits | Field | Description |
|------|-------|-------------|
| 7:4  | SL    | Sustain Level. 0=sustain at peak, 15=sustain at −45 dB |
| 3:0  | RR    | Release Rate (0=no release, 15=fastest) |

### `0x90` — SSG-EG

| Bits | Field | Description |
|------|-------|-------------|
| 3    | SSGE  | 1 = SSG-EG envelope mode active |
| 2:0  | SSGM  | Shape mode (0–7) |

**Shapes** (SSGE=1, AR must be 31):

| SSGM | Shape | Description |
|------|-------|-------------|
| 0–3  | /‾ × | One-shot attack or decay variants |
| 4    | \\\  | Descending sawtooth, loops |
| 5    | \/   | Triangle, loops |
| 6    | \‾   | Descending sawtooth, hold |
| 7    | \/   | Triangle (half), hold |

---

## Per-Channel Registers (`0xA0`–`0xBF`)

### `0xA0`–`0xA2` / `0xA4`–`0xA6` — Frequency

**Write HIGH before LOW.** The hardware latches the high byte and applies both simultaneously when the low byte is written.

| Register | Bits | Field |
|----------|------|-------|
| `0xA4`+ch | 5:3 | BLK (block/octave, 0–7) |
| `0xA4`+ch | 2:0 | FREQ[10:8] (upper 3 bits of F-number) |
| `0xA0`+ch | 7:0 | FREQ[7:0] (lower 8 bits of F-number) |

**F-number formula:**

```
FREQ = round(note_hz × 144 × 2^(21 − BLK) / (clock / 144))
```

Simplified for NTSC clock (7,670,454 Hz):

```
FREQ = round(note_hz × 2^20 / (7670454 / 144))
     = round(note_hz × 2^20 / 53267.0)
```

Choose BLK so that FREQ stays in range 0x000–0x7FF. Each BLK increment doubles the frequency (one octave up).

**Middle A (440 Hz):** BLK=4, FREQ≈0x28A.

**MIDI note to frequency:**
```cpp
double note_hz = 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
int blk = 4;  // start at octave 4, adjust
while (note_hz * (1 << (21 - blk)) / 53267.0 > 0x7FF && blk < 7) blk++;
while (note_hz * (1 << (21 - blk)) / 53267.0 < 0x000 && blk > 0) blk--;
int freq = static_cast<int>(std::round(note_hz * (1 << (21 - blk)) / 53267.0));
```

### `0xA8`–`0xAF` — Channel 3 Special Mode Frequencies

When `0x27` bits 7:6 = 01 (special mode), each of channel 3's operators can be pitched independently. *Channel 3 special mode is deferred to post-MVP ([ADR-0014](adr/0014-special-channel-features.md)); this register reference is retained for when it is built.*

| Register | Operator |
|----------|----------|
| `0xA8`   | S3 (operator 2) |
| `0xA9`   | S2 (operator 3) |
| `0xAA`   | S4 (operator 4) |
| `0xA2`   | S1 (operator 1, normal ch3 freq reg) |

### `0xB0` — ALG / FB

| Bits | Field | Description |
|------|-------|-------------|
| 2:0  | ALG   | Algorithm select (0–7, see Algorithms section) |
| 5:3  | FB    | S1 self-feedback (0=none, 7=maximum distortion) |

### `0xB4` — L/R/AMS/PMS

| Bits | Field | Description |
|------|-------|-------------|
| 7    | L     | 1 = left output enabled |
| 6    | R     | 1 = right output enabled |
| 5:4  | AMS   | Amplitude mod sensitivity (0=0dB, 1=1.4dB, 2=5.9dB, 3=11.8dB) |
| 2:0  | PMS   | Phase mod sensitivity (0=0¢, 1=3.4¢, 2=6.7¢, 3=10¢, 4=14¢, 5=20¢, 6=40¢, 7=80¢) |

---

## FM Algorithms

ALG selects the operator modulation topology. **M** = modulator (feeds another operator), **C** = carrier (outputs to mix bus). Operators in parentheses are summed before modulating.

| ALG | Signal Flow | Carriers | Character |
|-----|-------------|----------|-----------|
| 0   | S1→S2→S3→S4→out | S4 only | Maximum modulation depth, single voice |
| 1   | (S1+S2)→S3→S4→out | S4 only | S1 and S2 in parallel modulating S3 |
| 2   | (S1+(S2→S3))→S4→out | S4 only | S2 modulates S3; result + S1 modulate S4 |
| 3   | S1→(S2+S3)→S4→out | S4 only | S1 modulates S2 and S3 in parallel |
| 4   | (S1→S2)+(S3→S4)→out | S2, S4 | Two independent FM pairs |
| 5   | S1→(S2+S3+S4)→out | S2, S3, S4 | S1 modulates three carriers |
| 6   | (S1→S2)+S3+S4→out | S2, S3, S4 | One FM pair + two pure sine carriers |
| 7   | S1+S2+S3+S4→out | S1, S2, S3, S4 | Fully additive, four sine waves |

The UI should render a visual diagram for each algorithm (see [05-ui-ux.md](05-ui-ux.md)).

---

## Envelope Generator

Each operator has a 4-stage hardware envelope:

```
Level (attenuation)
│
0 dB ─────┐  ← AR: attack (rises to peak, nonlinear curve)
           │
-X dB ─────┤  ← DR: first decay (falls to sustain level SL)
           │
-SL ───────┤  ← SR: sustain rate (continues falling, or holds if SR=0)
           │
           └──── RR: release (after key-off, falls to silence)
```

- **AR** (0–31): 0 = no attack (stays silent), 31 = instant attack
- **DR** (0–31): first decay rate
- **SL** (0–15): sustain level. 0 = sustain at peak; 15 = sustain at −45 dB
- **SR** (0–31): secondary decay / sustain rate. 0 = hold at SL indefinitely
- **RR** (0–15): release rate after key-off

**KS** (Key Scale, 0–3) adds a pitch-derived offset to all rate values:

```
effective_rate = base_rate * 2 + (key_code >> (3 - KS))
```

Higher KS values speed up envelopes at higher pitches, closely mimicking acoustic instrument behavior.

---

## LFO

The YM2612 has a single global LFO shared by all channels. It must be explicitly enabled:

1. Write `0x22` with LFOEN=1 and desired LFRQ.
2. Set per-channel AMS and PMS fields in `0xB4`.
3. Set per-operator AMON bits in `0x60` for amplitude modulation.

The LFO is triangular waveform only (hardware fixed). PMS controls vibrato depth; AMS × AMON controls tremolo depth per operator.

---

## DAC Mode (Channel 6 PCM)

Channel 6 can be repurposed as a PCM playback channel. This is how Sega Genesis games played drums, speech, and bass guitar samples.

```cpp
// Enable DAC
chip.write(0, 0x2B);   // address
chip.write(1, 0x80);   // DACEN = 1

// Feed PCM samples — called at ~8kHz–22kHz from processBlock
chip.write(0, 0x2A);   // address
chip.write(1, sample_byte);  // 8-bit signed PCM

// Disable DAC (restore FM on ch6)
chip.write(0, 0x2B);
chip.write(1, 0x00);
```

Phase-accurate write timing: compute the number of host samples per DAC sample, decrement a counter each block, and write when the counter reaches zero.

In Gen VST, DAC playback uses a dedicated `ymfm::ym2612` instance separate from the 16-voice pool ([ADR-0014](adr/0014-special-channel-features.md)); no channel is ever excluded from FM allocation.

---

## Register Write Sequence for Note-On

To reliably trigger a voice without envelope artifacts, always write in this order:

```cpp
// 1. Key-off (silence any previous note)
chip.write(0, 0x28);
chip.write(1, 0x00 | ch_select);   // OPS=0, channel select

// 2. Operator parameters (for each of 4 operators in order S1, S3, S2, S4):
for (int op : {0, 4, 8, 12}) {
    chip.write(bankAddr, 0x30 + op + ch_offset); chip.write(bankData, dt_mul);
    chip.write(bankAddr, 0x40 + op + ch_offset); chip.write(bankData, tl);
    chip.write(bankAddr, 0x50 + op + ch_offset); chip.write(bankData, ks_ar);
    chip.write(bankAddr, 0x60 + op + ch_offset); chip.write(bankData, amon_dr);
    chip.write(bankAddr, 0x70 + op + ch_offset); chip.write(bankData, sr);
    chip.write(bankAddr, 0x80 + op + ch_offset); chip.write(bankData, sl_rr);
    chip.write(bankAddr, 0x90 + op + ch_offset); chip.write(bankData, ssg_eg);
}

// 3. Channel parameters
chip.write(bankAddr, 0xB0 + ch_offset); chip.write(bankData, alg_fb);
chip.write(bankAddr, 0xB4 + ch_offset); chip.write(bankData, lr_ams_pms);

// 4. Frequency — write HIGH first
chip.write(bankAddr, 0xA4 + ch_offset); chip.write(bankData, freq_high_blk);
chip.write(bankAddr, 0xA0 + ch_offset); chip.write(bankData, freq_low);

// 5. Key-on
chip.write(0, 0x28);
chip.write(1, 0xF0 | ch_select);   // OPS=0xF0 (all operators), channel select
```

`bankAddr`/`bankData` = 0/1 for channels 1–3, 2/3 for channels 4–6.
`ch_select` encoding: ch1–3 → 0–2, ch4–6 → 4–6 (skip value 3).
