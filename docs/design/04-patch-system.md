# Patch System

## Patch Data Model

The internal `Patch` struct stores all YM2612 parameters for **one FM part** as plain integers matching hardware register ranges. There are 6 parts, so the processor holds 6 `Patch` instances (see [ADR-0013](adr/0013-multitimbral-voice-model.md)):

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
  auto-loaded on every startup. Cannot be removed.
- **User root** — `<userAppData>/GenVst/patches/`. Writable; holds imported and
  user-saved patches.
- **Custom roots** — any number of folders the user registers via "Add Folder…".
  Each root's full subdirectory structure is preserved and navigable. The list of
  custom root paths is persisted in plugin state and re-scanned on next startup.
  Removing a root only unregisters it — no files are deleted from disk.

> **Cross-OS portability.** Custom-root and per-part patch paths are stored as
> **absolute filesystem paths** in plugin state. A project saved on one OS and
> reopened on another — or on a machine with a different folder layout — will not
> resolve those paths; only the factory root always resolves, because it is found
> relative to the plugin bundle. A path that fails to resolve is reported via a
> notification toast and does not block loading: the part keeps its restored
> parameter values (see [01-architecture.md](01-architecture.md) *State
> Persistence*). This is an accepted limitation for the MVP.

### UI structure

```
┌─────────────────────────────────────────────────┐
│ [ Search patches…                            🔍 ]│
│ ┌────────────────────┬──────────────────────────┐│
│ │ ▼ Factory          │ Bass Guitar              ││
│ │ ▼ extra  (custom)  │ Techno Lead              ││
│ │   ▶ 01      (842)  │ ▶ Synth Brass       ← sel ││
│ │   ▼ 02      (915)  │ Marimba                  ││
│ │     ▶ game_a (28)  │ ...                      ││
│ │     ▶ game_b (40)  │                          ││
│ │   ▶ 03      (770)  │                          ││
│ │ [+ Add Folder…]    │                          ││
│ └────────────────────┴──────────────────────────┘│
│ [Import file] [Export] [Delete]      [▶ Preview] │
└─────────────────────────────────────────────────┘
```

- **Left pane — folder tree:** every root and its subdirectories as a collapsible
  tree. Each scanned folder node shows its patch count so size is visible before
  expanding. Selecting a folder shows its patches on the right.
- **Right pane — patch list:** the `.tfi`/`.vgi`/`.dmp` files in the selected
  folder. Single-click or `Enter` loads the patch.
- **Search box:** filters by patch name across all roots; each result shows its
  folder path so duplicates from different games stay distinguishable.
- **Preview button:** sends a middle C note-on at fixed velocity for 1 second,
  then note-off.

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

1. User selects a patch → the message thread loads the file into a `Patch` struct, tagged with the target part index
2. Push the `(part, Patch)` item into a `juce::AbstractFifo`-based lock-free queue (capacity 4 is sufficient)
3. At the start of `processBlock`, drain the queue; each item updates that part's stored patch and is applied to the part's active and future voices

A load failure (`PatchLoadResult::error` set) never reaches the audio thread — it
is shown to the user via the UI notification toast (see [05-ui-ux.md](05-ui-ux.md)).

### Folders, Import & Export

- **Add Folder:** a folder picker registers a new custom root. The folder is
  scanned lazily; its directory structure becomes the navigable tree. The path is
  persisted across sessions.
- **Import file:** a file picker filtered to `*.tfi;*.vgi;*.dmp` copies a single
  patch into the writable user root.
- **Drag-and-drop:** dropping `.tfi`/`.vgi`/`.dmp` files imports them into the
  user root; dropping a *folder* registers it as a new custom root.
- **Export TFI:** construct a 42-byte buffer from the current `Patch` struct and write to file.
- **Export VGI:** construct a 43-byte buffer, pack AMS/FMS into byte 2, AMON into DR byte.
- **Delete:** removes a patch from a writable root. Disabled for the read-only
  factory root.

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
2. **User root** (writable) — `<userAppData>/GenVst/patches/`. Imported and
   user-saved patches.
3. **Custom roots** (user-added, any number) — folders registered via the
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
