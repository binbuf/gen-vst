# Task 10 — SQ preset ecosystem: DMP PSG import + factory `.psg` library

> **Milestone:** SQ preset ecosystem complete — users can import
> DefleMask PSG instruments and the factory SQ library covers the full
> SN76489 idiom palette.
> **Depends on:** Task 09 (browser wired, 3 smoke-test `.psg` seeds
> present), Task 06 (SQ engine audible in the plugin).
> **Design references:** `docs/design/04-patch-system.md` (*DMP Format*,
> *DMP PSG Mode*, *.psg Format*, *Factory bank*, *Patch Library &
> Delivery*), ADR-0026, ADR-0012, ADR-0025.

## Objective

Two deliverables:

1. **DMP PSG import** — extend `PatchSystem` and `DmpLoader` so that
   DMP version 11, mode 0 (STD/PSG) files resolve to tag `SQ` and load
   into SQ mode via a volume-macro-to-ADSR approximation. This gives
   users access to the DefleMask PSG community library without changing
   the SQ engine's ADSR model.

2. **Factory `.psg` library** — replace the 3 smoke-test presets seeded
   in Task 09 with a curated set of ~12 original `.psg` files covering
   the full SN76489 idiom palette. Values are tuned by ear against the
   working SQ panel (Task 06).

After this task: dropping any `.dmp` PSG instrument onto the plugin
loads it as an SQ preset with a best-effort ADSR approximation; the
factory browser shows a full SQ palette; Task 11 (parity audit) has a
complete preset set to validate against.

## Context & key constraints

### DMP PSG import

- **ADR-0026** is the governing decision. Read it in full before
  implementing.
- DMP version 11, byte 2 = 0 means PSG mode. The existing FM DMP loader
  already rejects byte 2 ≠ 1 with a hard error. This task adds a second
  loader branch for byte 2 = 0 rather than an error.
- **`tagFromFile(path)` for `.dmp`** (ADR-0026):
  - Peek byte 2 of the file; `1` → `Tag::FM` (existing path); `0` →
    `Tag::SQ` (new path); anything else → error toast.
  - All non-`.dmp` extensions continue using `tagFromExtension` (no I/O).
  - The folder-scan path is **not** allowed to open every file (too slow
    for large trees). The scan marks `.dmp` entries as `Tag::Pending`.
    Pending tags are resolved either on browse-expand (one folder at a
    time, acceptable latency) or on load attempt (always resolved before
    the load proceeds). The browser shows pending-tag DMPs with a neutral
    badge until resolved.
- **Macro → ADSR approximation** (ADR-0026 *Macro → ADSR approximation*):
  - DMP PSG volume macro: a sequence of attenuation values 0–15
    (0 = loudest, 15 = silent) plus a loop point.
  - Derive `atk`, `dr1`, `sus`, `dr2`, `rr` by analysing the curve
    shape as described in ADR-0026. Scale each value to the
    `SN76489Engine` ADSR parameter range (see `SN76489Engine.h` for the
    authoritative range constants).
  - Arpeggio and pitch macros → silently dropped; toast:
    "DMP PSG arpeggio / pitch macro ignored — only volume envelope
    imported."
  - Noise macro → `noise.type` (`white` / `periodic`) and `noise.rate`
    (`low` / `mid` / `high` / `ch2`) map directly.
  - `vol` → 1.0, `pan` → 0.0, `detune` → 0 for all channels.
- **DMP PSG byte layout** (verify against Furnace's
  `DivEngine::loadDMP`, `src/engine/fileOpsIns.cpp`, for the v11
  mode-0 body — the structure differs from the FM body):
  - Bytes 0–2: version / system / mode (same header as FM DMP).
  - Following bytes define the macro data (volume macro length + values,
    arpeggio macro length + values, etc.). Furnace is the reference;
    consult the gitignored local checkout, not the repo.
- **Toast on lossy import**: after a successful PSG DMP load, emit a
  notification toast: "Imported as SQ preset (DMP PSG approximation)."
  This sets user expectations about fidelity.

### Factory `.psg` library

- Task 09 seeded `extern/patches/sq/` with exactly 3 files
  (`default.psg`, `pulse-arp.psg`, `soft-lead.psg`) sufficient to verify
  the browser wiring. This task **replaces** those stubs with tuned
  values and adds the remaining 9 presets.
- Values are determined **by ear** against the working SQ panel (Task
  06). Tune each preset until it sounds like its described character —
  the JSON values in this doc are starting points only.
- Every preset must be an **original work** — no values derived from
  game ROMs, SMPS drivers, or other copyrighted sources. The `.psg`
  format encodes only synthesis parameters (envelope shape, volume, pan,
  detune); none of those parameter choices is protectable. Nonetheless:
  do not name any preset after a specific game or character.
- CMake's factory-patch staging already includes `extern/patches/sq/`
  recursively (Task 09). No CMake change is needed in this task.

## Scope

### C++

- **`src/PatchSystem.{h,cpp}`**:
  - Add `tagFromFile(const std::filesystem::path& path)` that reads byte
    2 of a `.dmp` file and returns `Tag::FM`, `Tag::SQ`, or an error.
    All other paths fall through to `tagFromExtension`. (Rename the old
    static helper or add an overload — either way, the public API that
    the browser and drop-handler call resolves to the right tag for every
    extension including `.dmp`.)
  - Add `Tag::Pending` to the `Tag` enum (or use `std::optional<Tag>`
    if that fits cleanly — pick one and keep it consistent). The
    folder-scan stores `Pending` for `.dmp` files; the UI badge for a
    pending-tag row is a neutral grey chip.
- **`src/DmpLoader.{h,cpp}`** (extends the existing FM DMP loader):
  - Add `loadDmpPsg(const std::filesystem::path& path)` → `PsgPreset`
    or error string. Verify the version (0x0B) and system (0x02/0x42)
    bytes exactly as the FM loader does; reject anything else with a
    clear error.
  - Implement the macro → ADSR approximation per ADR-0026.
- **`tests/DmpLoaderTests.cpp`** — extend the existing FM DMP test file:
  - Round-trip: synthesise a minimal PSG DMP byte buffer in the test
    (no real game files); parse it; verify the approximated ADSR values
    are in-range and the noise fields map correctly.
  - Verify arpeggio macro generates the toast warning (mock the toast
    path or check the returned warning string).
  - Verify a mode-1 file still loads as FM (no regression).
  - Verify an unknown mode byte returns an error.

### Factory preset files

Write or revise these 12 files in `extern/patches/sq/`. Each section
below gives the target character and a JSON starting point with
placeholder values. Tune the values against the live SQ panel.

**1. `default.psg`** — Neutral single-tone lead. Basis for the "no
preset loaded" default when manually switching to SQ mode.
```json
{ "version": 1, "name": "Default Lead",
  "channels": {
    "tone1": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 5,
               "vol": 1.0, "pan": 0.0, "detune": 0 },
    "tone2": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan": 0.0, "detune": 0 },
    "tone3": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan": 0.0, "detune": 0 },
    "noise": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan": 0.0, "type": "white", "rate": "mid" }
  }
}
```

**2. `square-bass.psg`** — Single-channel bass. Tone1 only, high volume.
Tight, punchy attack; mid sustain; moderate release. Classic SN76489
low-end character. Tune atk near 0 for punchy feel.

**3. `pulse-arp.psg`** — Classic chiptune arpeggio character. Short
envelope — fast attack, fast decay, low sustain — so rapid re-triggers
leave space between notes. Tone1 only; let the engine's note repeat
carry the arpeggio feel.

**4. `soft-lead.psg`** — The existing smoke-test preset. Tune with a
moderate attack (atk ~7–9), gentle first decay, high sustain, moderate
release. Two detuned tone channels (tone1 pan −0.2, tone2 pan +0.2,
detune ±7 semitones) for slight chorusing.
```json
{ "version": 1, "name": "Soft Lead",
  "channels": {
    "tone1": { "atk": 8, "dr1": 4, "sus": 12, "dr2": 0, "rr": 6,
               "vol": 1.0, "pan": -0.2, "detune": 0 },
    "tone2": { "atk": 8, "dr1": 4, "sus": 12, "dr2": 0, "rr": 6,
               "vol": 1.0, "pan":  0.2, "detune": 7 },
    "tone3": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan":  0.0, "detune": 0 },
    "noise": { "atk": 0, "dr1": 0, "sus": 15, "dr2": 0, "rr": 0,
               "vol": 0.0, "pan":  0.0, "type": "white", "rate": "mid" }
  }
}
```

**5. `detuned-chord.psg`** — All three tone channels, each detuned
slightly apart (+0, +7, −7 semitones) and panned across the stereo field.
Full volume; moderate envelope. Classic PSG "thick pad" sound.

**6. `bright-pluck.psg`** — Fast attack, fast dr1, low or zero sustain,
fast rr. Single tone channel. Produces a staccato pluck character — think
opening notes in a 16-bit title screen fanfare.

**7. `retro-beep.psg`** — Pure square tone. Instant attack (atk 0),
no decay, full sustain, instant release (rr 0 or 1). The simplest
possible PSG voice, useful as a reference and for simple UI sounds in
a retro game context.

**8. `chip-melody.psg`** — Tone1 + tone2 an octave apart (detune 12),
tone2 at half volume, centred pan. Medium envelope. Produces the layered
octave unison common in 16-bit melody lines.

**9. `noise-snare.psg`** — White noise percussion. Noise channel only;
tone channels silent. Very fast attack (atk 0), fast dr1, zero sustain
(sus 0), fast rr. The defining "SN76489 snare" sound. Rate: `mid` or
`high` — tune for snap vs thud character.

**10. `periodic-bass.psg`** — Periodic noise at a low rate. Produces the
pitched buzzy bass rumble familiar from SN76489-only games. Noise channel
only; `type: "periodic"`, `rate: "low"`. Full vol; moderate sustain.

**11. `noise-hats.psg`** — White noise, high rate, extremely tight
envelope (fast attack, fast decay, zero sustain, zero release). Emulates
a hi-hat or closed hat. Noise channel only.

**12. `title-screen.psg`** — Tone1 (melody) + tone2 (harmony, +5 semitones)
+ noise channel (very soft white noise for texture). Pan tone channels
slightly apart. Medium-bright envelope. Evokes the classic 8/16-bit title
theme layering without referencing any specific game.

## Out of scope

- Multi-step arpeggio playback from DMP arpeggio macros — the `.psg`
  format has a single `detune` field (static offset), not a sequence.
  Arpeggio macros are dropped with a toast (ADR-0026).
- OPM or Y12 PSG support — there is no OPM/Y12 equivalent for SN76489.
- Expanding the FM factory bank — the 39 Furnace `tfilib` TFIs remain the
  FM factory set; FM enrichment is via the VGM bank import path.

## Implementation steps

1. **Add `tagFromFile` to `PatchSystem`** — implement the `.dmp`
   content-peek per ADR-0026. Add `Tag::Pending` (or
   `std::optional<Tag>`) for deferred resolution. Verify all existing
   tests still pass (`ctest`).
2. **PSG DMP loader** — implement `loadDmpPsg` in `DmpLoader.cpp`.
   Verify the byte layout against the Furnace reference checkout
   (gitignored; see ADR-0012's pattern). Implement the macro → ADSR
   approximation per ADR-0026. Write the `DmpLoaderTests` extension.
3. **Wire pending-tag resolution into the browser expand path** — when
   a folder node is expanded in the browser, any `Tag::Pending` `.dmp`
   files in that folder are resolved (call `tagFromFile`) and the badge
   is updated in the rendered row. Cap the resolution batch per expand
   event (e.g., 50 files) to avoid UI stalls on large DMP collections.
4. **Write and tune the 12 factory presets** — open the plugin in a DAW
   (Task 06 must be complete). Write initial JSON from the starting
   points above; play MIDI through the SQ panel; tune each preset by
   ear until its character matches the description. Commit when tuned.
5. **Smoke-test** — load each of the 12 factory presets via the browser;
   confirm they load without errors, the SQ panel populates the correct
   values, and playback sounds reasonable.

## Deliverables

- `src/PatchSystem.{h,cpp}` — `tagFromFile` + `Tag::Pending`.
- `src/DmpLoader.{h,cpp}` — `loadDmpPsg` + macro → ADSR approximation.
- `tests/DmpLoaderTests.cpp` — PSG DMP round-trip + regression tests.
- `extern/patches/sq/` — 12 tuned `.psg` files (replaces Task 09's 3
  smoke-test stubs).
- No CMake changes (Task 09 already stages the full `sq/` subtree).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green, including the new DmpLoader PSG tests.
2. **DMP PSG import** — drop a real DefleMask PSG DMP file (community
   source; do not use any file extracted from a game ROM) onto the plugin
   window; the browser shows it with an `SQ` badge; single-click loads
   it into SQ mode; the SQ panel populates; playback produces sound.
   The toast confirms "Imported as SQ preset (DMP PSG approximation)."
3. **FM DMP not broken** — drop a DMP FM instrument; it still loads as FM
   with no regressions.
4. **Unknown mode byte** — a hand-crafted DMP byte buffer with byte 2 = 3
   (in a unit test) returns an error result; the toast message is
   descriptive.
5. **Pending tag resolves** — drop a folder containing a mix of FM and
   PSG DMP files onto the browser; expand the folder; each file shows the
   correct `FM` or `SQ` badge within the batch-resolve cap.
6. **All 12 factory presets load** — open the browser; filter to `SQ`;
   single-click each factory preset in turn; each loads without errors
   and the SQ panel updates with non-default values.
7. **Sound check** — cycle through all 12 presets while playing MIDI and
   confirm each one has a distinctly different character matching its
   description.

## Done when

- [ ] `loadDmpPsg` exists and handles the volume macro → ADSR
      approximation; arpeggio / pitch macros generate the toast warning.
- [ ] `tagFromFile` resolves `.dmp` mode correctly; non-`.dmp` extensions
      are unchanged; `Tag::Pending` defers scan-time file I/O.
- [ ] All 12 factory `.psg` presets are tuned, committed, and load
      without errors.
- [ ] `ctest` is green including the new DmpLoader PSG tests.
- [ ] `pluginval --strictness-level 8` still passes (no regression from
      the `tagFromFile` change).
