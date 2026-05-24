# Patch System

## Tagging — file extension is the tag

Under [ADR-0025](adr/0025-tagged-preset-browser.md), every patch file is
tagged with the mode it belongs to, and the tag is derived from the file's
extension — no sidecar, no metadata header. The browser uses the tag to
filter the visible patches and to auto-switch the instance's mode when a
patch is loaded.

| Extension | Tag | Format |
|---|---|---|
| `.tfi`, `.vgi`, `.dmp`, `.y12`, `.opm` | **FM** | See FM format sections below |
| `.vgm`, `.vgz` | **FM** | VGM bank import (extracts FM patches) |
| `.psg` | **SQ** | New v2 JSON — schema below |
| `.gdac` | **D**  | New v2 JSON — schema below |

`.wav` is **not** a recognised patch tag in v2 — D mode is an audio FX,
not a sampler ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).

The lookup table lives in `src/PatchSystem.{h,cpp}` as `tagFromExtension()`
and `kSupportedPatchExtensions`. Both the file picker and the drag-and-drop
handler consume them so the supported set stays in one place.

---

## FM Patch Data Model

The internal `Patch` struct stores all YM2612 parameters for **the FM
patch** as plain integers matching hardware register ranges. Under v2
([ADR-0021](adr/0021-three-mode-single-engine-ui.md)) the processor holds
**one** `Patch` instance — the v1 six-part array is retired with
`PartManager`:

```cpp
struct Patch {
    // Channel-level (per voice)
    uint8_t alg;          // 0–7: algorithm
    uint8_t fb;           // 0–7: S1 self-feedback
    uint8_t lr;           // bits: bit1=L, bit0=R (stereo output enables)
    uint8_t ams;          // 0–3: amplitude mod sensitivity
    uint8_t pms;          // 0–7: phase mod sensitivity (vibrato depth)
    uint8_t lfo_enable;   // 0/1: LFO on (stored per-patch, applied globally)
    uint8_t lfo_rate;     // 0–7: LFO frequency select

    // Per-operator (index 0=OP1/S1, 1=OP2/S2, 2=OP3/S3, 3=OP4/S4)
    uint8_t mul[4];       // 0–15: integer frequency multiple (used in INT_MUL mode)
    uint8_t dt[4];        // 0–6: detune
    uint8_t tl[4];        // 0–127: total level (hardware attenuation — 0 = loudest; UI exposes the inverted "level" — see 02-fm-synthesis.md § UI level vs hardware attenuation)
    uint8_t ks[4];        // 0–3: key scale
    uint8_t ar[4];        // 0–31: attack rate
    uint8_t dr[4];        // 0–31: first decay rate
    uint8_t sr[4];        // 0–31: second decay / sustain rate
    uint8_t rr[4];        // 0–15: release rate
    uint8_t sl[4];        // 0–15: sustain level (hardware attenuation, like tl[])
    uint8_t ssg[4];       // 0 or 8–15: SSG-EG (values 1–7 are invalid)
    uint8_t amon[4];      // 0/1: amplitude mod enable per operator

    // ---- v2 additions (modelled on RYM2612; default for legacy formats) ----
    // FREQ CTRL MODE — see 02-fm-synthesis.md § FREQ Control Mode
    uint8_t freq_ctrl_mode;  // 0=INT_MUL (default), 1=FLOAT_MUL, 2=AUTO_RETRIG
    uint16_t retrig_rate;    // 0–1023: TimerA value for AUTO_RETRIG (default 500)
    float    mul_float[4];   // 0.5–15.99: per-op float multiplier (FLOAT_MUL / AUTO_RETRIG); default mirrors mul[]
    uint8_t  fixed[4];       // 0/1: per-op fixed-frequency flag; default 0
    float    freq_fixed_hz[4]; // 20.0–20000.0 Hz: per-op absolute frequency when fixed[op]=1; default 440.0

    // Per-op modulation depth (RYM2612 manual page 10)
    // The earlier per-op `mw[4]` field was removed during the
    // post-mockup review — modwheel is global-only in v2; per-op
    // modulation goes via velocity only.
    float    vel[4];         // 0.0–1.0: per-op velocity → TL depth; default 0.0
    // Hardware emulation + UI master controls
    // (The v2 first-pass `mw_to_pms` modwheel-to-PMS depth scaler was
    //  removed during the post-mockup review — modwheel routes to the
    //  LFO PMS field at full depth, no adjustable scaler.)
    float    fm_dac_prescaler; // 0.0–1.0: YM2612 DAC prescaler depth; default 0.0
                               //   (see 02-fm-synthesis.md § DAC Prescaler (FM mode))
    float    channel_tl;       // 0.0–1.0: UI-only master multiplier across
                               //   all 4 operator TLs; default 1.0
                               //   (see 02-fm-synthesis.md § Channel TL)

    std::string name;     // display name (from filename or DMP internal)
};
```

**Defaults on legacy-format load.** TFI / VGI / DMP / Y12 / OPM
predate every field below the dashed comment line. Loaders set
defaults that preserve legacy-faithful playback:

| Field | Default on legacy-format load |
|---|---|
| `freq_ctrl_mode` | `INT_MUL` (0) |
| `retrig_rate` | `500` |
| `mul_float[op]` | `(float)mul[op]` (mirrors integer mul) |
| `fixed[op]` | `0` (off) |
| `freq_fixed_hz[op]` | `440.0` |
| `vel[op]` | `0.0` (no velocity→TL effect; ADR-0023 / Settings toggle controls a global enable) |
| `fm_dac_prescaler` | `0.0` (no DAC prescaling; clean rendering) |
| `channel_tl` | `1.0` (no master attenuation; per-op TLs heard verbatim) |

These defaults make a legacy patch sound identical to the v1 behaviour
once loaded.

---

## TFI Format (Primary — 42 Bytes)

TFI (TFM Music Maker Instrument) is the simplest and most widely supported patch format. No magic number or header.

**Complete byte layout:**

| Offset | Field | Range |
|--------|-------|-------|
| 0x00   | Algorithm (ALG) | 0–7 |
| 0x01   | Feedback (FB) | 0–7 |
| 0x02   | OP1 MUL | 0–15 |
| 0x03   | OP1 DT  | 0–6  |
| 0x04   | OP1 TL  | 0–127 |
| 0x05   | OP1 KS  | 0–3  |
| 0x06   | OP1 AR  | 0–31 |
| 0x07   | OP1 DR  | 0–31 |
| 0x08   | OP1 SR  | 0–31 |
| 0x09   | OP1 RR  | 0–15 |
| 0x0A   | OP1 SL  | 0–15 |
| 0x0B   | OP1 SSG-EG | 0 or 8–15 |
| 0x0C–0x15 | OP2 (same 10-byte layout) | |
| 0x16–0x1F | OP3 (same 10-byte layout) | |
| 0x20–0x29 | OP4 (same 10-byte layout) | |

Operators are stored **sequentially as OP1, OP2, OP3, OP4** — this is different from the YM2612 hardware register order (S1, S3, S2, S4).

**TFI does not contain:** FMS/PMS, AMS, AMON, LFO settings. Default to 0 when loading a TFI file.

**DT encoding note:** TFI stores DT as 0–6 where values 0–3 are positive detune and 4–6 are negative. The YM2612 hardware register encodes it differently (bits 6:4 with 4=0 meaning "no detune same as 0"). The loader must be aware of which encoding the source files use; most community TFI files use the 0–6 encoding directly.

---

## VGI Format (Secondary — 43 Bytes)

VGI (VGM Music Maker) extends TFI with AMS/FMS data. One byte longer, no magic number.

**Differences from TFI:**

| Offset | Field | Description |
|--------|-------|-------------|
| 0x00   | ALG | Same as TFI |
| 0x01   | FB  | Same as TFI |
| **0x02** | **AMD/FMD** | `0b00AA0FFF` — bits 5:4 = AMS (0–3), bits 2:0 = FMS/PMS (0–7) |
| 0x03–0x2A | OP1–OP4 | Same layout as TFI but shifted by 1 byte; AMON is packed into the DR byte |

**DR byte in VGI:** `bit7 = AMON, bits4:0 = DR value`.

**TL range:** 0–127 for **all four operators**, identical to TFI. Verified
against [plutiedev.com/format-tfi](https://www.plutiedev.com/format-tfi),
which describes VGI as "almost identical to TFI except an extra byte after
feedback" with no per-operator TL difference. The 0–63 figure that appears in
some secondary sources is not used.

---

## DMP Format (DefleMask Preset)

DMP files have a variable-length header and are version-dependent.

**Version detection:**

| Byte 0 (version) | Format |
|------------------|--------|
| 0–8              | Legacy layouts, differ per version |
| 11 (0x0B)        | Modern format (target this) |

**Byte layout for version 11, FM instrument** — verified against Furnace's
`DivEngine::loadDMP` (see *Furnace reference* below):

| Byte | Field | Notes |
|------|-------|-------|
| 0    | Version (`0x0B`) | reject if not exactly 11 |
| 1    | System (`0x02` = Sega Genesis, `0x42` = Genesis extended / CH3 special) | reject anything else |
| 2    | Mode (`1` = FM, `0` = STD / PSG) | reject PSG; the FM-only loader accepts mode 1 only |
| 3    | FMS (= PMS for YM2612) | 0–7 |
| 4    | FB | 0–7 |
| 5    | ALG | 0–7 |
| 6    | AMS | 0–3 |
| 7+   | Per-operator, 11 bytes each, 4 operators in order OP1–OP4: `MUL, TL, AR, DR, SL, RR, AM, RS, DT, D2R, SSG-EG` | total operator block = 44 bytes |

Total file size for v11 Genesis FM: **7 header + 44 operator = 51 bytes.**

**Per-operator notes.**
- `RS` corresponds to the patch model's `ks` (key scale, 0–3).
- The `DT` byte packs `DT2` in the upper nibble and `DT` in the lower nibble.
  `DT2` is OPM-only and is discarded for YM2612.
- `D2R` corresponds to the patch model's `sr` (second decay / sustain rate, 0–31).

Reject files where byte 0 ≠ `0x0B`, byte 1 ∉ {`0x02`, `0x42`}, or byte 2 ≠ 1.
The byte-2 sense is **opposite** of DefleMask's UI labelling — in the on-disk
format, mode 1 means FM and mode 0 means STD/PSG. The PSG-instrument body has
an entirely different structure and is never parsed by the FM loader.

> **Furnace reference.** Byte offsets above were verified against
> `tildearrow/furnace`'s `DivEngine::loadDMP` in
> `src/engine/fileOpsIns.cpp` (older revisions called this file
> `src/format/dmp.cpp`). Furnace is consulted as a **local, gitignored
> reference checkout only** — never committed to the repo or added as a
> build dependency.

---

## Y12 Format (128 Bytes)

Y12 is a flat single-channel YM2612 register dump emitted by SMPS-style
Mega Drive ROM-hacking tools (TFM Music Maker and similar). It captures the
exact register state the channel had in the game, so it is the closest format
to raw hardware state. Y12 patches are user-supplied only — never bundled as
factory content (ADR-0004).

**File size:** exactly 128 bytes. Reject any other size with a clear error
message via the UI notification toast.

**Byte layout (verified against Furnace's `DivEngine::loadY12`):**

| Offset | Field | Notes |
|--------|-------|-------|
| 0x00–0x3F | Four 16-byte operator blocks (OP1..OP4) | Each block mirrors the YM2612's per-operator register order; see below |
| 0x40 | ALG | 0–7 |
| 0x41 | FB  | 0–7 |
| 0x42 | AMS | 0–3 |
| 0x43 | PMS | 0–7 |
| 0x44 | reserved | Legacy "AMON-packed" slot; **not read** — see operator block |
| 0x45 | LFO enable / rate | bit 3 = enable, bits 0:2 = rate |
| 0x46–0x7F | reserved / pad | ignored |

**Per-operator block (16 bytes; first 7 bytes mirror YM2612 registers
0x30/0x40/0x50/0x60/0x70/0x80/0x90 for the operator; the remaining 9 bytes
are padding):**

| Byte | YM2612 reg | Bit layout |
|------|------------|------------|
| +0 | 0x30+off | DT\[6:4\] \| MUL\[3:0\] — DT is HW 0–7, converted to TFI 0–6 by the loader (see [ADR-0020](adr/0020-dt-register-encoding-y12-opm.md)) |
| +1 | 0x40+off | TL\[6:0\] (bit 7 unused) |
| +2 | 0x50+off | KS\[7:6\] \| AR\[4:0\] |
| +3 | 0x60+off | AMON\[7\] \| DR\[4:0\] — this is the authoritative AMON source |
| +4 | 0x70+off | SR\[4:0\] (AKA D2R) |
| +5 | 0x80+off | SL\[7:4\] \| RR\[3:0\] |
| +6 | 0x90+off | SSG-EG\[3:0\] |
| +7..+15 | — | padding, ignored |

> **Furnace reference.** Layout verified against `tildearrow/furnace`'s
> `DivEngine::loadY12` in `src/engine/fileOpsIns.cpp`. Furnace is consulted
> as a **local, gitignored reference checkout only** — never committed to the
> repo or added as a build dependency, matching ADR-0012's pattern. Furnace's
> own loader marks its DT transform `// ???`; Gen VST uses the project's
> canonical HW→TFI conversion instead — see ADR-0020.

**LR enables:** Y12 carries no L/R; the loader defaults to `lr=3` (both
enabled), matching the existing TFI/VGI loaders' rationale.

**Verified vs. inferred bytes.** Furnace only reads 0x00–0x41; the channel-
level AMS/PMS/LFO offsets at 0x42/0x43/0x45 are taken from this spec table
and have not been cross-checked against the TFM Music Maker source. The
loader clamps each one to its hardware range so garbage padding becomes a
benign 0 default rather than a corrupted patch (see ADR-0020).

---

## OPM Format (Text — VOPM / YM2151)

OPM is Yamaha's YM2151 line-based ASCII instrument format used by VOPM and the
YM2151 MML community. The YM2151 and YM2612 share enough register semantics
that OPM patches load meaningfully on Gen VST, with one field dropped and one
defaulted.

**File structure:** one or more `@:<num> <name>` header lines followed by these
parameter lines (whitespace-separated integers):

```
@:0 Lead 1
LFO: <LFRQ> <AMD> <PMD> <WF> <NFRQ>
CH:  <PAN> <FL> <CON> <AMS> <PMS> <SLOT> <NE>
M1:  <AR> <D1R> <D2R> <RR> <D1L> <TL> <KS> <MUL> <DT1> <DT2> <AMS-EN>
C1:  <AR> <D1R> <D2R> <RR> <D1L> <TL> <KS> <MUL> <DT1> <DT2> <AMS-EN>
M2:  <AR> <D1R> <D2R> <RR> <D1L> <TL> <KS> <MUL> <DT1> <DT2> <AMS-EN>
C2:  <AR> <D1R> <D2R> <RR> <D1L> <TL> <KS> <MUL> <DT1> <DT2> <AMS-EN>
```

**Operator mapping:** `M1 → OP1`, `C1 → OP2`, `M2 → OP3`, `C2 → OP4`. The
M/C naming describes the OPM signal flow; on the YM2612 the algorithm field
determines which operators are carriers vs. modulators.

**Field mapping per operator:**

| OPM field | Patch field | Notes |
|-----------|-------------|-------|
| `AR`  | `ar[op]`  | 0–31 |
| `D1R` | `dr[op]`  | 0–31 (first decay) |
| `D2R` | `sr[op]`  | 0–31 (second decay / sustain rate) |
| `RR`  | `rr[op]`  | 0–15 |
| `D1L` | `sl[op]`  | 0–15 (sustain level) |
| `TL`  | `tl[op]`  | 0–127 — same dB-per-step as YM2612, no rescaling |
| `KS`  | `ks[op]`  | 0–3 |
| `MUL` | `mul[op]` | 0–15 |
| `DT1` | `dt[op]`  | OPM stores the raw 3-bit YM2151 detune register (0–7). The loader converts to the patch model's TFI 0–6 encoding via the inverse of `FmRegisterMap::detuneToRegister` so the patch round-trips through `detuneToRegister` correctly. See [ADR-0020](adr/0020-dt-register-encoding-y12-opm.md) |
| `DT2` | — | **Silently dropped.** YM2151-only field; YM2612 has no DT2 register |
| `AMS-EN` | `amon[op]` | 0/1 |

**Channel-level mapping:**

| OPM field | Patch field | Notes |
|-----------|-------------|-------|
| `CON` | `alg` | 0–7 |
| `FL`  | `fb`  | 0–7 (feedback) |
| `AMS` | `ams` | 0–3 |
| `PMS` | `pms` | 0–7 |
| `LFRQ` | `lfo_rate` | 0–7 (top bits truncated if source exceeds range) |
| `AMD` / `PMD` / `WF` / `NFRQ` / `PAN` / `SLOT` / `NE` | — | OPM-specific, ignored |

**SSG-EG:** OPM has no SSG-EG field. Defaults to 0 (off) for all operators.

**LFO enable:** OPM has no explicit LFO enable. The loader sets
`lfo_enable = 1` if `LFRQ`, `AMD`, or `PMD` is non-zero, else `0`.

**Name:** parsed from the `@:<num> <name>` header line, trimmed.

**Parsing rules:**
- Read the file as text (UTF-8 / ASCII). Split on lines.
- Tokenize by whitespace. Lines starting with `//` or empty lines are skipped.
- A missing parameter line (`LFO`, `CH`, `M1`-`C2`) is a load error with a
  descriptive message.
- A line with too few integers is a load error.
- Out-of-range integers are clamped to the hardware range (same approach as
  TFI/VGI/DMP).
- Multi-instrument OPM files (more than one `@:` block) load the first block
  only; subsequent blocks are ignored. (Multi-instrument bank import for OPM
  is post-MVP.)

---

## `.psg` Format (v2 SQ Preset — JSON)

A small JSON file holding one SQ-mode patch: per-channel envelope settings,
noise type/rate, channel volumes, channel pans, and a display name. New
in v2; loaded only in SQ mode.

**Schema (single supported `version = 1`):**

```json
{
  "version": 1,
  "name": "Soft Lead",
  "channels": {
    "tone1": { "atk": 8, "dr1": 4, "sus": 12, "dr2": 0, "rr": 6,
               "vol": 1.0, "pan": -0.3, "detune": 0 },
    "tone2": { "atk": 8, "dr1": 4, "sus": 12, "dr2": 0, "rr": 6,
               "vol": 1.0, "pan":  0.3, "detune": 7 },
    "tone3": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan":  0.0, "detune": 0 },
    "noise": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan":  0.0,
               "type": "white", "rate": "mid" }
  }
}
```

- `atk / dr1 / sus / dr2 / rr` — software-ADSR stage values (Task 23
  semantics, unchanged).
- `vol` — 0.0..1.0 per-channel volume.
- `pan` — -1.0..+1.0 L/R balance.
- `detune` — semitone offset (signed integer).
- `noise.type` — `"white"` or `"periodic"`.
- `noise.rate` — `"low"`, `"mid"`, `"high"`, or `"ch2"`.

Loader/writer live in `src/PsgPreset.{h,cpp}`. Out-of-range or missing
fields are clamped to defaults; an unparseable file raises a notification
toast and does not load.

Default `.psg` presets ship under `extern/patches/sq/` (a handful of
bass / lead / arp tones — Task v2/05 seeds them).

---

## `.gdac` Format (v2 D Preset — JSON)

A tiny JSON file holding the three D-mode DSP values. New in v2; loaded
only in D mode.

**Schema (single supported `version = 1`):**

```json
{
  "version": 1,
  "name": "Crunchy Drums",
  "prescaler": 0.65,
  "mono": false,
  "dry_wet": 1.0
}
```

- `prescaler` — 0.0..1.0; 0.0 = no decimation (host rate); 1.0 = maximum
  decimation. Mapping to actual sample-and-hold divisor lives in
  `DspDecimator`.
- `mono` — collapse L/R to mono before the decimator.
- `dry_wet` — 0.0 = original audio only, 1.0 = decimated only.

Loader/writer live in `src/DacPreset.{h,cpp}`. Schema parallels `.psg` for
consistency; default presets ship under `extern/patches/d/`.

---

## VGM Bank Import

VGM (`.vgm`) and VGZ (`.vgz`, gzipped VGM) files are register-log captures of a
chip session — primarily used to archive game soundtracks for the Genesis. Bank
import opens such a file, walks the YM2612 register-write stream, and emits one
`Patch` per unique register state captured at each FM key-on event. This is the
copyright-clean path to game-original FM timbres: the user supplies the file
from public archives (vgmrips.net, Project2612), and Gen VST extracts patches
from the register log without touching ROM content.

### UX

Matches Genny VST's "Import Bank" — **one click, no second dialog**:

1. User clicks **Import Bank** on the IMPORT tab.
2. Native file picker opens (`*.vgm;*.vgz`).
3. C++ extracts all patches on a background thread and writes each as a `.tfi`
   into `<userAppData>/GenVst/patches/imported/`.
4. A toast surfaces `"Imported N patches from <filename>"` (or the parse error).
5. The IMPORT list refreshes immediately; patches are loadable right away.

No per-patch checkbox/preview modal. See [[reference-genny-vst-features]] —
this UX matches the Genny parity bar by intent, recorded in ADR-0019.

### Parser scope

The parser handles only what bank import needs from the VGM 1.50+ spec:

| Command | Bytes | Action |
|---------|-------|--------|
| `0x52 rr dd` | 3 | YM2612 port 0 write — apply to shadow state |
| `0x53 rr dd` | 3 | YM2612 port 1 write — apply to shadow state (CH4-6 registers) |
| `0x61 nn nn` | 3 | Wait N samples — advance internal sample clock |
| `0x62` | 1 | Wait 735 samples (one 60 Hz frame) |
| `0x63` | 1 | Wait 882 samples (one 50 Hz frame) |
| `0x70`–`0x7F` | 1 | Wait (n+1) samples |
| `0x66` | 1 | End of sound data — stop |
| any other | varies — skip per spec length tables | Ignore (SN76489, PCM, other chips) |

`.vgz` files are decompressed in memory via `juce::GZIPDecompressorInputStream`
before parsing. No new third-party dependency is added; the existing libvgm
submodule (used only for the SN76489 emulation core per ADR-0009) is **not**
expanded to provide VGM parsing — the dependency boundary stays narrow.

### State tracker

The parser maintains shadow register state for each of the six FM channels.
A key-on event is a write to register `0x28` whose data byte has any of the
top four bits set; the low three bits identify the channel.

On each key-on:
1. Assemble a `Patch` from the channel's current shadow register state.
2. Hash the patch's content.
3. If the hash is new, append the patch to the result list. Otherwise skip
   (dedupe).

Patches are named `"<filename-stem> #<n>"` where `n` starts at 1.

The parser ignores writes to PSG / PCM / other-chip commands but still advances
their byte cursor correctly per the VGM spec's per-command length table.

---

## Loading Code Sketch

```cpp
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

// All loading functions run on the message thread (never the audio thread).
// C++20 — no std::expected; a small result struct carries the patch or an
// error message for the UI notification toast (see 05-ui-ux.md).
struct PatchLoadResult {
    std::optional<Patch> patch;   // populated on success
    std::string          error;   // populated on failure
};

PatchLoadResult loadTFI(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return { std::nullopt, "cannot open file" };

    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
    if (bytes.size() != 42)
        return { std::nullopt, "expected 42 bytes, got " + std::to_string(bytes.size()) };

    Patch p{};
    p.alg = bytes[0] & 0x07;
    p.fb  = bytes[1] & 0x07;
    for (int op = 0; op < 4; ++op) {
        int base = 2 + op * 10;
        p.mul[op] = bytes[base+0] & 0x0F;
        p.dt[op]  = bytes[base+1] & 0x07;
        p.tl[op]  = bytes[base+2] & 0x7F;
        p.ks[op]  = bytes[base+3] & 0x03;
        p.ar[op]  = bytes[base+4] & 0x1F;
        p.dr[op]  = bytes[base+5] & 0x1F;
        p.sr[op]  = bytes[base+6] & 0x1F;
        p.rr[op]  = bytes[base+7] & 0x0F;
        p.sl[op]  = bytes[base+8] & 0x0F;
        p.ssg[op] = bytes[base+9];
    }
    p.name = path.stem().string();
    return { p, {} };
}

PatchLoadResult loadVGI(const std::filesystem::path& path);  // size == 43; AMS/FMS in byte 2; AMON in DR bit 7
PatchLoadResult loadDMP(const std::filesystem::path& path);  // version 11 only (ADR-0012); reject other versions with an error
PatchLoadResult loadY12(const std::filesystem::path& path);  // size == 128; flat single-channel register dump; lr defaults to 3 (both)
PatchLoadResult loadOPM(const std::filesystem::path& path);  // line-based ASCII; DT2 dropped; SSG-EG defaults to 0; multi-instrument files load first block only

// Bank import — only path that returns multiple patches. Used by Import Bank.
// Reads .vgm or .vgz, walks YM2612 register writes, snapshots on each key-on,
// dedupes by content hash. Names patches "<filename-stem> #<n>".
std::vector<Patch> extractFmPatches(const std::filesystem::path& vgmPath, std::string& error);
```

Clamp all loaded values to their valid hardware ranges to handle corrupted files gracefully.

---

## Patch Browser Design

The browser must work for both the small factory bank and very large custom
collections — a single custom folder can be a deeply nested tree of tens of
thousands of `.tfi` files. The design is therefore **folder-tree based**, not a
flat bank list.

### Patch roots

A **patch root** is a top-level folder the browser scans:

- **Factory root** — the bundled factory patches. Read-only, always present,
  auto-loaded on every startup. Cannot be removed. Holds the seed `.tfi`
  FM patches plus the seed `.psg` and `.gdac` presets under `sq/` and `d/`
  subfolders.
- **User-saved root** — `<userAppData>/GenVst/patches/saved/`. Writable;
  populated by save operations from any of the three modes. Auto-created
  (idempotent `fs::create_directories`) on first launch.
- **User-imported root** — `<userAppData>/GenVst/patches/imported/`. Writable;
  populated by imports and drag-and-drop. Auto-created on first launch.
- **Custom roots** — any number of folders the user registers via "Add Folder…".
  Each root's full subdirectory structure is preserved and navigable. The list of
  custom root paths is persisted in plugin state and re-scanned on next startup.
  Removing a root only unregisters it — no files are deleted from disk.

The v1 split of the main window into INSTRUMENTS / PRESETS / IMPORT lists
on different columns/tabs is **removed**. The unified preset browser modal
([ADR-0025](adr/0025-tagged-preset-browser.md)) is the single navigator
for every root and every tag.

> **Pre-existing flat user roots.** Earlier builds wrote both saves and
> imports directly to `<userAppData>/GenVst/patches/`. Such legacy patches
> stay on disk but no longer appear in the writable-root scans. They remain
> reachable by adding the legacy folder as a custom root via the patch
> browser's *Add Folder…* — a one-way, non-destructive migration.

> **Cross-OS portability.** Custom-root paths and the active patch path
> are stored as **absolute filesystem paths** in plugin state. A project
> saved on one OS and reopened on another — or on a machine with a
> different folder layout — will not resolve those paths; only the factory
> root always resolves, because it is found relative to the plugin bundle.
> A path that fails to resolve is reported via a notification toast and
> does not block loading: the instance keeps its restored apvts parameter
> values (see [01-architecture.md](01-architecture.md) *State Persistence*).
> This is an accepted limitation for the MVP.

### UI structure

```
┌─────────────────────────────────────────────────┐
│ [All] [FM] [SQ] [D]    [ Search patches…    🔍 ]│
│ ┌────────────────────┬──────────────────────────┐│
│ │ ▼ Factory          │ FM  Bass Guitar          ││
│ │ ▼ extra  (custom)  │ FM  Techno Lead          ││
│ │   ▶ 01      (842)  │ FM  ▶ Synth Brass  ← sel ││
│ │   ▼ 02      (915)  │ SQ  Pulse Arp            ││
│ │     ▶ game_a (28)  │ D   Crunchy Drums        ││
│ │     ▶ game_b (40)  │ ...                      ││
│ │   ▶ 03      (770)  │                          ││
│ │ [+ Add Folder…]    │                          ││
│ └────────────────────┴──────────────────────────┘│
│ [Import file] [Export] [Delete]                  │
└─────────────────────────────────────────────────┘
```

- **Mode filter chips (top-left):** `All / FM / SQ / D`. Default = the
  instance's current mode, so the user first sees patches for what they're
  editing. Switching to `All` shows every patch across modes.
- **Left pane — folder tree:** every root and its subdirectories as a
  collapsible tree. Each scanned folder node shows its patch count so size
  is visible before expanding. Selecting a folder shows its patches on the
  right.
- **Right pane — patch list:** every patch file in the selected folder,
  filtered by the active chip. Each row carries a small `FM` / `SQ` / `D`
  badge. Single-click or `Enter` loads the patch. **Loading auto-switches
  the instance's mode** if the patch's tag differs from the current mode
  ([ADR-0025](adr/0025-tagged-preset-browser.md)) — no confirmation modal;
  the previous patch is not auto-saved but remains untouched on disk.
- **Search box:** filters by patch name across all roots and all modes
  honouring the active mode filter; each result shows its folder path.

There is no separate "Preview" button — single-click on a patch loads it
into the instance (preview *is* load), consistent with RYM2612 and most
modern preset browsers. The browser stays open so several patches can be
auditioned in turn.

### Scanning strategy (large directory trees)

An eager flat scan of tens of thousands of files would stall the UI. Instead:

- **Lazy scan:** a folder's contents are read only when its tree node is first
  expanded. Startup scans just each root's immediate children.
- **Background indexing:** the search index (patch name → path) is built on a
  background thread after startup, so search needs no upfront full scan and never
  blocks the UI.
- **Counts:** a folder's patch count is filled in once that folder is scanned.

File enumeration and parsing always run on the message thread, never the audio
thread.

### Audio Thread Delivery

Patch loads must not block the audio thread. Workflow:

1. User selects a patch → the message thread parses the file into the
   appropriate in-memory struct (`Patch` for FM, `PsgPreset` for SQ,
   `DacPreset` for D) and tags it with the mode it belongs to.
2. If the patch's mode differs from the current mode, the message thread
   first flips `mode_select` via the apvts.
3. Push the typed-patch item into a `juce::AbstractFifo`-based lock-free
   queue (one queue per mode is sufficient; capacity 4 each).
4. At the start of `processBlock`, drain the queue for the active mode
   and apply.

A load failure (`PatchLoadResult::error` set) never reaches the audio
thread — it is shown to the user via the UI notification toast (see
[05-ui-ux.md](05-ui-ux.md)).

### Folders, Import & Export

- **Add Folder:** a folder picker registers a new custom root. The folder is
  scanned lazily; its directory structure becomes the navigable tree. The path is
  persisted across sessions.
- **Import file:** a file picker filtered to
  `*.tfi;*.vgi;*.dmp;*.y12;*.opm;*.psg;*.gdac` copies a single patch
  into the **user-imported root** (`…/patches/imported/`). The supported
  extension list lives at `kSupportedPatchExtensions` in
  `src/PatchSystem.h`; the picker and the drag-and-drop handler both
  consume it so the set stays in one place.
- **Import Bank:** a separate button with its own picker filtered to
  `*.vgm;*.vgz`. Extracts every unique FM channel state captured at each
  key-on event into the user-imported root, one `.tfi` per patch, named
  `<filename-stem> #<n>`. Implemented in `src/VgmExtract/` (ADR-0019).
  One-click flow: pick file → all patches written → toast → IMPORT list
  refreshes.
- **Drag-and-drop:** dropping any file whose extension is in
  `kSupportedPatchExtensions` imports it into the user-imported root;
  dropping a `.vgm` or `.vgz` runs Import Bank on that file; dropping a
  *folder* registers it as a new custom root.
- **Save patch:** `savePatch()` writes the current mode's patch into the
  **user-saved root** (`…/patches/saved/`). The format depends on the mode
  (FM → TFI by default; SQ → `.psg`; D → `.gdac`).
- **Export TFI** (FM mode): construct a 42-byte buffer from the current
  `Patch` struct and write to file.
- **Export VGI** (FM mode): construct a 43-byte buffer, pack AMS/FMS into
  byte 2, AMON into DR byte.
- **Export PSG** (SQ mode) and **Export DAC** (D mode): write the
  in-memory struct out as JSON via the corresponding loader's writer.
- **Delete:** removes a patch from a writable root. Disabled for the
  read-only factory root.

---

## Patch Library & Delivery

### Factory bank (shipped)

The only bank bundled with the plugin is the **Furnace factory bank**: the `.tfi`
instruments from `tildearrow/furnace` at `instruments/OPN/tfilib/` — ~39 files with
generic timbre names (`bass.tfi`, `piano.tfi`, `marimba.tfi`, …). Furnace is GPL,
compatible with this project's GPLv3 license; include Furnace attribution.

These files are committed to the repo as the **top-level `.tfi` files directly in
`extern/patches/`** (the `extra/` subfolder beside them is gitignored — see below).

No game-derived patches are shipped. Their file and directory names encode game,
publisher, and level titles (trademark and copyright exposure), so they are kept
out of the repo and out of every build artifact.

### Local test material (not shipped, not committed)

`extra/` holds a large game-derived `.tfi` collection (~30k files)
used only as developer test input — exercising the loader, browser scrolling, and
voice allocation under load. It is excluded from version control by
`extern/patches/.gitignore` and is never copied into a build artifact. Developers
load it ad hoc through the patch browser's Import / folder-drop path.

> **Build requirement:** the factory copy must enumerate only the *top-level*
> `.tfi` files in `extern/patches/` — a recursive scan would pull in the
> gitignored `extra/` test set. Use a non-recursive glob, not a directory copy.

### Delivery: install-time filesystem copy

Factory patches are **not** embedded via `juce_add_binary_data`. The data is tiny
(~42 bytes/file), but binary-data embedding generates per-file C++, scales poorly,
and forces a recompile whenever the patch set changes. Instead the factory `.tfi`
files are copied to a runtime location at build/install time and loaded from the
filesystem.

- **Plugin formats (VST3/AU):** each artifact is a bundle directory. CMake stages
  the top-level factory `.tfi` files into a clean build folder, which is then
  copied into the bundle's `Contents/Resources/patches/`.
- **Standalone:** a bare executable (no bundle on Windows). A CMake `install()`
  rule copies the factory patches into a platform data directory.

### Runtime patch roots

The plugin organises patches into **roots** (see Patch Browser Design). On startup
it loads, in order:

1. **Factory root** (read-only, always present) — the bundle's
   `Contents/Resources/patches/` for plugin formats; a platform data directory for
   Standalone. Auto-loaded every launch.
2. **User-saved root** (writable) — `<userAppData>/GenVst/patches/saved/`.
   Populated by `savePatch()` only. Auto-created on first launch.
3. **User-imported root** (writable) — `<userAppData>/GenVst/patches/imported/`.
   Populated by `importPatch()` and drag-and-drop only. Auto-created on first launch.
4. **Custom roots** (user-added, any number) — folders registered via the
   browser's "Add Folder…", with their paths persisted in plugin state and
   re-scanned on the next launch.

Each root's directory structure is preserved as a navigable tree. Scanning is lazy
(see the scanning strategy in Patch Browser Design); loading runs on the message
thread.

### CMake sketch

```cmake
# Factory bank = the TOP-LEVEL .tfi files in extern/patches/ only.
# A recursive scan would pull in the gitignored extra/ test set — don't.
file(GLOB FACTORY_PATCHES "${CMAKE_SOURCE_DIR}/extern/patches/*.tfi")

# Stage into a clean dir so the bundle copy contains only factory files
# (juce_add_bundle_resources_directory copies a whole directory tree).
set(FACTORY_STAGE "${CMAKE_BINARY_DIR}/factory-patches")
file(MAKE_DIRECTORY "${FACTORY_STAGE}")
file(COPY ${FACTORY_PATCHES} DESTINATION "${FACTORY_STAGE}")
juce_add_bundle_resources_directory(GenVst "${FACTORY_STAGE}")

# Standalone has no bundle — install the same files to a data directory.
install(FILES ${FACTORY_PATCHES} DESTINATION "${GENVST_STANDALONE_PATCH_DIR}")
```

---

## Legal Notes

| Asset | License | Verdict |
|-------|---------|---------|
| ymfm library | BSD-3-Clause | Compatible with GPL project |
| JUCE | GPL v3 | Plugin must be GPL v3 |
| Furnace `tfilib` preset data | GPL | Shipped as the factory bank — include Furnace attribution |
| Game-derived `.tfi` library | Derivative of copyrighted game audio | **Not shipped, not committed** — local developer test material only |

Only the Furnace factory bank is distributed. The game-derived collection is never
part of the repo or any release artifact, so the project carries no game-audio
redistribution exposure. Any bank added later must have a clear license before it
can be committed or shipped.

---

## Bank bundle format (`.gnbank`) — retired in v2

The v1 `.gnbank` rack bundle captured the user-curated instrument rack
(Task 22) plus each row's per-instrument routing. The rack model is
**removed** in v2 ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)) —
each plugin instance holds one patch, and routing across instances is the
DAW's job, not the plugin's.

`src/BankIO.{h,cpp}` and `tests/BankIOTests.cpp` are deleted in Task v2/02
along with `PartManager`. A future v2-tagged-bank format (mixing FM/SQ/D
presets into one shareable file) is a possible follow-up but is **not in
v2 MVP scope** — the unified preset browser handles per-file sharing
fine. Users wanting to package a curated collection can zip a folder and
share that.

---

## Plugin state file (`.gnvst`)

The *Save State* / *Load State* JSON-XML file. The bytes are the same
`juce::AudioProcessorValueTreeState` blob the plugin returns from
`getStateInformation` / consumes via `setStateInformation` — the `.gnvst`
extension just lifts that state out of the DAW project into a standalone
file the user can copy across sessions or machines.

Contents (per `src/PluginState.cpp` in v2):

- The full `apvts` parameter tree (mode, FM patch params, SQ patch params,
  D-mode DSP params, globals).
- The active patch path (one path; the patch's extension implies its tag
  and therefore the mode).
- Registered custom-root paths.

**No embedded base64 PCM** — v2 D mode does not load WAV files, so there
is no PCM to persist ([ADR-0021](adr/0021-three-mode-single-engine-ui.md)).
The v1 base64-PCM serialisation is removed from `PluginState`.
