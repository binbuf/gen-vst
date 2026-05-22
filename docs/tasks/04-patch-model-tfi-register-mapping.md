# Task 04 — Patch model, TFI loader & FM register mapping

> **Depends on:** Task 02.
> **Design references:** `docs/design/04-patch-system.md` (primary — *Patch Data
> Model*, *TFI Format*, *Loading Code Sketch*), `docs/design/02-fm-synthesis.md`
> (*Per-Operator Registers*, *Register Write Sequence for Note-On*,
> *0xA0–0xBF Frequency*), `docs/design/06-build-system.md` (*Tests*),
> ADR-0010, ADR-0012.

## Objective

Introduce the real `Patch` data model, the **TFI loader** (the primary patch
format), and the **FM register-mapping** code that turns a `Patch` into the
correct ymfm register sequence. Replace Task 02's hard-coded patch so that a
loaded factory `.tfi` file plays. Add the first real unit tests.

## Context & key constraints

- `Patch` is a plain-integer struct mirroring hardware register ranges — exact
  fields in `04-patch-system.md` *Patch Data Model*. There are 6 parts later, so
  `Patch` is one part's worth of data.
- **TFI is 42 bytes, no header/magic.** Byte layout in `04-patch-system.md`
  *TFI Format*. Operators are stored **sequentially OP1, OP2, OP3, OP4**.
- **Operator register order is NOT the storage order.** ymfm hardware operator
  offsets are **S1 +0x00, S3 +0x04, S2 +0x08, S4 +0x0C** — S2 and S3 are swapped
  relative to their numbers (`02-fm-synthesis.md` *Per-Operator Registers*). The
  mapping from `Patch` operator index (0=OP1…3=OP4) to register offset must use
  this order. This is the single most common bug in this code — get it right.
- **Every FM voice is channel 0 of its own `ymfm` instance** (ADR-0010), so
  register writes always use bank-0 ports (`0`=address, `1`=data) and channel
  offset `0`. The bank-1 / ch4–6 addressing in `02-fm-synthesis.md` is hardware
  reference and is **not used** for FM voices (only the DAC instance touches
  channel 6 — Task 07).
- **Note-on register sequence** is exact — follow `02-fm-synthesis.md`
  *Register Write Sequence for Note-On*: key-off → per-operator params (in S1,
  S3, S2, S4 order) → channel params (`0xB0` ALG/FB, `0xB4` L/R/AMS/PMS) →
  frequency **HIGH byte `0xA4` before LOW byte `0xA0`** → key-on.
- **F-number:** `FREQ = round(note_hz × 2^20 / 53267.0)`, choose `BLK` so `FREQ`
  ∈ `0x000–0x7FF` (`02-fm-synthesis.md` *Frequency*). A4 → BLK=4, FREQ≈0x43B.
- **DT encoding caution:** TFI stores DT as 0–6; the YM2612 register field
  encodes detune differently. Handle the conversion in the register-mapping
  layer per the `04-patch-system.md` *DT encoding note* — load TFI's value into
  `Patch.dt`, convert to the hardware field when writing register `0x30`.
- TFI carries no FMS/PMS, AMS, AMON, or LFO — default those to 0 on load.
- Loaders run on the **message thread only** and return a `PatchLoadResult`
  (`std::optional<Patch>` + error string — no `std::expected`). Clamp every
  loaded value to its valid hardware range so corrupt files fail gracefully.
- Unit tests use GoogleTest (`tests/` wired in Task 01). The design names the
  test files: `PatchLoaderTests.cpp`, `FrequencyCalcTests.cpp`,
  `RegisterWriteTests.cpp`.

## Scope

- `Patch` struct and `PatchLoadResult`.
- `loadTFI(path)` — 42-byte parse, range clamping, name from filename.
- The **register-mapping** module: `Patch` + MIDI note → the ordered list of
  `(register, value)` writes for a note-on, and the key-off write. Frequency
  calculation (MIDI note → F-number + BLK). The DT encoding conversion.
- Make the register-mapping module **testable without a live chip** — route its
  writes through a thin sink (interface, callback, or returned vector) so a test
  can capture and assert the exact sequence, while the real path writes to the
  `ymfm::ym2612` instance.
- Wire it in: replace Task 02's hard-coded patch by loading a factory `.tfi`
  (a hard-coded dev path into the staged factory dir is fine for this task) and
  playing it through the single Task 02 voice.
- Three unit-test files: `PatchLoaderTests` (TFI), `FrequencyCalcTests`,
  `RegisterWriteTests`. Add them to `tests/CMakeLists.txt`.

## Out of scope

- VGI / DMP loaders and patch export → Task 08.
- The 6-part `Patch` array, voice pool, parameter system → Task 05.
- Per-block dirty-diff register writes → Task 05 (this task does a full note-on
  write each time).
- The patch browser / roots → Task 09.

## Implementation steps

1. Define `Patch` and `PatchLoadResult` (a `PatchSystem.h`/`.cpp` or
   `Patch.h` + `PatchLoader.{h,cpp}` — your call; the design's file list has
   `PatchSystem.h/cpp`).
2. Implement `loadTFI` per the `04-patch-system.md` sketch, with clamping.
3. Implement the register-mapping module: operator-offset table (S1/S3/S2/S4),
   the note-on sequence builder, frequency calc, DT conversion. Make it emit
   through a capturable sink.
4. Replace the Task 02 hard-coded patch: load a factory `.tfi` and drive the
   voice from the resulting `Patch`.
5. Write the three test files; register them in `tests/CMakeLists.txt`.

## Deliverables

`src/Patch.h` (or in `PatchSystem.h`), `src/PatchSystem.{h,cpp}` (or
`PatchLoader.{h,cpp}`), the register-mapping module (e.g. `src/FmRegisterMap.{h,cpp}`),
updates to `src/PluginProcessor.cpp` and the Task 02 render engine,
`tests/PatchLoaderTests.cpp`, `tests/FrequencyCalcTests.cpp`,
`tests/RegisterWriteTests.cpp`, `tests/CMakeLists.txt`.

## Verification

1. `ctest --test-dir build/windows-debug --output-on-failure` — all three new
   suites pass:
   - `PatchLoaderTests`: every one of the 39 factory `.tfi` files loads
     successfully; values are within hardware ranges; a wrong-size buffer and a
     missing file each return a `PatchLoadResult` with a non-empty `error` and
     no patch.
   - `FrequencyCalcTests`: A4 (MIDI 69) → BLK=4, FREQ within ±1 of 0x43B;
     across MIDI 0–127 every result keeps FREQ in `0x000–0x7FF`; one octave up
     doubles the effective frequency.
   - `RegisterWriteTests`: for a known `Patch`, the captured note-on write
     sequence matches an expected log — correct order (key-off, S1/S3/S2/S4
     operator blocks, channel regs, freq HIGH then LOW, key-on) and correct
     values, including the DT conversion.
2. Build + launch the Standalone. With the dev path pointed at `organ.tfi` (or
   `bass.tfi`), playing notes produces that timbre; swap the path to a
   different factory patch and the timbre audibly changes.
3. `pluginval --strictness-level 8` still passes.

## Done when

- [ ] `Patch` + `loadTFI` implemented; all 39 factory files load and clamp.
- [ ] Register mapping uses the S1/S3/S2/S4 offset order and the exact note-on
      sequence; DT conversion is correct.
- [ ] All three unit-test suites pass under `ctest`.
- [ ] A loaded factory `.tfi` audibly plays its timbre in the Standalone.
- [ ] `pluginval` still passes.
