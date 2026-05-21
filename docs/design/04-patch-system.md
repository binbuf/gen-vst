# Patch System

## Patch Data Model

The internal `Patch` struct stores all YM2612 parameters as plain integers matching hardware register ranges:

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
    uint8_t mul[4];       // 0–15: frequency multiple
    uint8_t dt[4];        // 0–6: detune
    uint8_t tl[4];        // 0–127: total level (attenuation)
    uint8_t ks[4];        // 0–3: key scale
    uint8_t ar[4];        // 0–31: attack rate
    uint8_t dr[4];        // 0–31: first decay rate
    uint8_t sr[4];        // 0–31: second decay / sustain rate
    uint8_t rr[4];        // 0–15: release rate
    uint8_t sl[4];        // 0–15: sustain level
    uint8_t ssg[4];       // 0 or 8–15: SSG-EG (values 1–7 are invalid)
    uint8_t amon[4];      // 0/1: amplitude mod enable per operator

    std::string name;     // display name (from filename or DMP internal)
};
```

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

> **Caution:** Some sources indicate TL range for OP2–OP4 in VGI may be 0–63 rather than 0–127. Cross-check against the reference at [plutiedev.com/format-tfi](https://www.plutiedev.com/format-tfi) during implementation.

---

## DMP Format (DefleMask Preset)

DMP files have a variable-length header and are version-dependent.

**Version detection:**

| Byte 0 (version) | Format |
|------------------|--------|
| 0–8              | Legacy layouts, differ per version |
| 11 (0x0B)        | Modern format (target this) |

**Byte layout for version 11, FM instrument:**

| Byte | Field |
|------|-------|
| 0    | Version (0x0B) |
| 1    | System (0x02 = Sega Genesis, 0x42 = Genesis extended/ch3 special) |
| 2    | Instrument type (0 = FM, 1 = PSG) |
| 3    | LFO (AMS) |
| 4    | LFO (FMS) |
| 5    | ALG |
| 6    | FB |
| 7+   | Per-operator: for each of 4 operators in order OP1–OP4: AM, AR, DR, MUL, RR, SL, TL, DT2, RS/KS, DT, D2R/SR, SSG-EG |

Reject files where byte 1 is not 0x02 or 0x42, or byte 2 is not 0. PSG instrument DMP files have an entirely different structure.

> **Note:** Verify exact byte offsets against the Furnace source code (`src/format/dmp.cpp`) during implementation — the version 11 layout has subtle differences across tools.

---

## Loading Code Sketch

```cpp
#include <filesystem>
#include <expected>
#include <fstream>
#include <string>

// All loading functions run on the message thread (never audio thread).
// Return type uses std::expected<Patch, std::string> (C++23).
// For C++17 compatibility, use std::optional<Patch> and log errors separately.

std::expected<Patch, std::string> loadTFI(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected("cannot open file");

    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
    if (bytes.size() != 42) return std::unexpected("expected 42 bytes, got " + std::to_string(bytes.size()));

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
    return p;
}

std::expected<Patch, std::string> loadVGI(const std::filesystem::path& path) {
    // same structure but check size == 43, read AMS/FMS from byte 2,
    // unpack AMON from DR byte (bit 7)
}

std::expected<Patch, std::string> loadDMP(const std::filesystem::path& path) {
    // read version, reject < 8 or != 11
    // read system byte, reject if not 0x02 / 0x42
    // read type byte, reject if not 0 (FM)
    // parse v11 layout
}
```

Clamp all loaded values to their valid hardware ranges to handle corrupted files gracefully.

---

## Patch Browser Design

### UI Structure

```
┌─────────────────────────────┐
│ Bank: [SoR Koshiro     ▼ ]  │
│                             │
│ ┌─────────────────────────┐ │
│ │ Bass Guitar             │ │
│ │ Techno Lead             │ │
│ │ ▶ Synth Brass       ← sel│ │
│ │ Marimba                 │ │
│ │ ...                     │ │
│ └─────────────────────────┘ │
│ [Import] [Export] [Delete]  │
│ [▶ Preview]                 │
└─────────────────────────────┘
```

- **Bank selector:** `juce::ComboBox` listing factory banks (locked) and user banks (editable)
- **Patch list:** `juce::ListBox` with single-selection; `Enter` or single-click loads patch
- **Preview button:** sends a middle C note-on at fixed velocity for 1 second, then note-off

### Audio Thread Delivery

Patch loads must not block the audio thread. Workflow:

1. User selects patch → message thread loads file into `Patch` struct
2. Push `Patch` into a `juce::AbstractFifo`-based lock-free queue (capacity 4 is sufficient)
3. At the start of `processBlock`, drain the queue and apply the patch to all active and future voices

### Import/Export

- **Import:** `juce::FileChooser` filtered to `*.tfi;*.vgi;*.dmp`. Loaded patch is added to the "User" bank. Bank is saved as a folder in the user data directory.
- **Export TFI:** construct a 42-byte buffer from the current `Patch` struct and write to file.
- **Export VGI:** construct a 43-byte buffer, pack AMS/FMS into byte 2, AMON into DR byte.
- **Drag-and-drop:** accept `.tfi`, `.vgi`, `.dmp` files dropped directly onto the plugin window.
- **Bulk import:** accept a folder drop → scan for supported files → create a new user bank named after the folder.

---

## Bundled Patch Library Plan

### Directory Structure

```
resources/patches/
├── furnace-factory/    ← Furnace GPL presets
│   ├── bass.tfi
│   ├── brass.tfi
│   └── ...
├── sor-koshiro/        ← Streets of Rage 1/2/3 (MDDC community packs)
│   ├── sor1_bass.tfi
│   ├── sor2_lead.tfi
│   └── ...
├── sonic/              ← Sonic 1/2/3 patches
│   └── ...
├── phantasy-star-iv/   ← Phantasy Star IV patches
│   └── ...
└── user/               ← User-imported patches (not bundled; runtime directory)
```

### Sources

| Bank | Source | License status |
|------|--------|----------------|
| `furnace-factory` | `tildearrow/furnace` presets folder | GPL — compatible with project |
| `sor-koshiro` | MDDC community packs (SoR 1/2/3) | Community-tolerated; verify before commercial use |
| `sonic` | MDDC / Sonic Retro community rips | Same caveat |
| `phantasy-star-iv` | MDDC / community VGM rips via vgm2pre | Same caveat |

### Ripping Workflow (vgm2pre)

1. Log a VGM file using Kega Fusion for the target game
2. Run `vgm2pre input.vgm -f tfi -o output_dir/`
3. Curate the resulting `.tfi` files (many will be duplicates or test noise)
4. Rename files descriptively based on in-game context

### CMake Embedding

Patches are embedded via `juce_add_binary_data` for zero-install distribution:

```cmake
# Enumerate patch files automatically:
file(GLOB_RECURSE PATCH_FILES "${CMAKE_SOURCE_DIR}/resources/patches/*.tfi"
                               "${CMAKE_SOURCE_DIR}/resources/patches/*.vgi")
juce_add_binary_data(GenVstBinaryData SOURCES ${PATCH_FILES} ...)
```

Alternatively, load from the filesystem at runtime (simpler iteration, but requires installer). The `BinaryData` approach is recommended for initial release.

> **Open question:** If the bundled patch count grows large (>200 files), binary data embedding inflates the plugin binary significantly. Benchmark before deciding.

---

## Legal Notes

| Asset | License | Verdict |
|-------|---------|---------|
| ymfm library | BSD-3-Clause | Compatible with GPL project |
| JUCE | GPL v3 | Plugin must be GPL v3 |
| Furnace preset data | GPL | Compatible — include attribution |
| MDDC community packs | Unverified | Contact maintainers before commercial use; hobbyist distribution widely tolerated |
| vgm2pre output | Derivative of copyrighted game audio | Include only patches from games with community consent; avoid using game character names in preset names (e.g., "Sonic Bass" → "Genesis Bass") |

For the initial open-source release, the community-tolerated standard applies. If the project is ever monetized, audit and replace any legally ambiguous patches with original or licensed content.
