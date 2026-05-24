# Task 20 — Y12 + OPM patch loaders

> **Depends on:** Task 08.
> **Design references:** `docs/design/04-patch-system.md` (primary — *Y12
> Format*, *OPM Format*, *Loading Code Sketch*, *Folders, Import & Export*),
> ADR-0019.

## Objective

Add the **Y12** and **OPM** loaders so the patch system imports five formats
(TFI, VGI, DMP, Y12, OPM). Extend the supported-extension list (a new
`kSupportedPatchExtensions` constant) that the import file chooser and
drag-and-drop handler from Task 14 consume. No UI changes are in scope — the
existing IMPORT-tab plumbing picks the new extensions up automatically.

## Context & key constraints

- **Y12 — 128 bytes, single-channel YM2612 register dump.** Byte layout in
  `04-patch-system.md` *Y12 Format*: four 16-byte operator blocks (OP1–OP4),
  then ALG/FB/AMS/PMS/AMON-packed/LFO. **The exact register byte ordering
  inside each operator block must be verified against the TFM Music Maker
  spec** at implementation time — the spec table in 04-patch-system.md gives
  the intent; the implementer cross-references the TFM Music Maker source
  (or Furnace's Y12 loader as a fallback) and records the verified offsets in
  a code comment, the same pattern used for DMP v11 in ADR-0012.
- **Y12 carries no L/R.** Default `Patch::lr = 3` (both enabled) so loaded
  patches are audible, matching the existing TFI/VGI loaders.
- **OPM — line-based ASCII, YM2151 origin.** Format in `04-patch-system.md`
  *OPM Format*: one or more `@:<num> <name>` header blocks, each followed by
  `LFO:`, `CH:`, `M1:`, `C1:`, `M2:`, `C2:` lines. Operator mapping:
  `M1→OP1, C1→OP2, M2→OP3, C2→OP4`.
- **OPM DT2 is dropped silently.** YM2612 has no DT2 register; warning on every
  OPM file would be noise. Document the decision in a source comment.
- **OPM SSG-EG defaults to 0** (off). OPM has no SSG-EG field.
- **OPM TL is 0–127, same dB-per-step as YM2612** — no rescaling. Verify by
  loading a known-good VOPM reference patch and listening; recorded in a code
  comment per the existing VGI TL-range comment pattern.
- **OPM LFO enable:** the format has no explicit enable. Set `lfo_enable = 1`
  if any of `LFRQ`, `AMD`, `PMD` is non-zero, else `0`.
- **Multi-instrument OPM files** load the first `@:` block only; subsequent
  blocks are ignored. (Multi-instrument OPM bank import is post-MVP — added
  to the README backlog.)
- All loaders run on the **message thread**, return `PatchLoadResult`
  (`std::optional<Patch>` + error string), and clamp values to hardware
  ranges using the existing `clampTo` / `clampSsg` helpers in
  `src/PatchSystem.cpp`.

## Scope

- `loadY12(path)` — 128-byte parse; verified byte ordering; `lr` defaults to 3.
- `loadOPM(path)` — line-tokenized text parse; DT2 dropped; SSG-EG defaults to
  0; first-block-only on multi-instrument files.
- New `kSupportedPatchExtensions` constant in `src/PatchSystem.h` —
  `{".tfi", ".vgi", ".dmp", ".y12", ".opm"}`. Task 14's import chooser and
  drag-and-drop handler consume it; both call sites are updated to use the
  constant instead of the hard-coded `*.tfi;*.vgi;*.dmp` literal.
- Extend `tests/PatchLoaderTests.cpp` (or a new `Y12OpmLoaderTests.cpp` if the
  existing file is already large) to cover Y12 and OPM loads, hardware-range
  clamping, malformed-input rejection, and the DT2-drop / SSG-EG-default
  behavior for OPM.
- One Y12 fixture and one OPM fixture under `tests/fixtures/patches/`.

## Out of scope

- VGM bank import → Task 21.
- Multi-instrument OPM banks (post-MVP, recorded in README backlog).
- UI changes — the IMPORT tab and drag-and-drop already work; they pick up the
  new extensions via `kSupportedPatchExtensions`.

## Implementation steps

1. Add `kY12FileSize = 128`, `kSupportedPatchExtensions`, `loadY12`, `loadOPM`
   declarations to `src/PatchSystem.h`.
2. Implement `loadY12` in `src/PatchSystem.cpp` reusing the existing
   `readFileBytes` / `clampTo` / `clampSsg` helpers. Verify the operator-block
   byte ordering against the TFM Music Maker reference; record the verified
   offsets in a code comment.
3. Implement `loadOPM` in `src/PatchSystem.cpp`. Read whole file as text;
   tokenize lines; parse `LFO`, `CH`, and the four operator lines. A missing
   line or too-few-integers is a load error. DT2 dropped; SSG-EG defaults to 0;
   AMS-EN bit → `amon[op]`; LFO enable derived from non-zero LFRQ/AMD/PMD.
4. Update the two Task-14 call sites (`Import file` `juce::FileChooser` filter
   and `FileDragAndDropTarget::isInterestedInFileDrag`) to read from
   `kSupportedPatchExtensions` instead of the hard-coded TFI/VGI/DMP string.
5. Add `Y12LoaderTests.cpp` and `OpmLoaderTests.cpp` (or extend
   `PatchLoaderTests.cpp` — pick whichever keeps each file under ~400 lines).
   Construct fixtures both as hand-built byte buffers (Y12) and as inline
   strings (OPM) so tests are self-contained, plus one on-disk fixture each
   under `tests/fixtures/patches/` exercising the full file-read path.
6. Register new test files in `tests/CMakeLists.txt`.

## Deliverables

- `src/PatchSystem.h` — new constants and declarations.
- `src/PatchSystem.cpp` — new loader implementations.
- Updates to the Task-14 file-chooser and drag-and-drop call sites to consume
  `kSupportedPatchExtensions`.
- `tests/Y12LoaderTests.cpp`, `tests/OpmLoaderTests.cpp` (or extensions to
  `tests/PatchLoaderTests.cpp`).
- `tests/fixtures/patches/<name>.y12`, `tests/fixtures/patches/<name>.opm`.
- `tests/CMakeLists.txt` registration.

## Verification

1. `ctest --test-dir build/windows-debug --output-on-failure` — new tests pass,
   covering:
   - A hand-built 128-byte Y12 buffer loads with the expected field values;
     out-of-range bytes are clamped; a wrong-size buffer is rejected with a
     descriptive error.
   - The Y12 fixture file loads when read from disk.
   - An inline OPM string loads; M1/C1/M2/C2 map to OP1/OP2/OP3/OP4; DT2 is
     silently ignored (no error); SSG-EG values are 0; AMS-EN unpacks to
     `amon[op]`; `lfo_enable` reflects non-zero LFRQ/AMD/PMD.
   - A multi-instrument OPM string loads the first block only; the second
     block's fields are not present.
   - A missing-line OPM input returns an error.
   - The OPM fixture file loads when read from disk.
2. Build the plugin — it still builds. The IMPORT tab's file chooser shows
   `.y12` and `.opm` in its filter (no code change needed in the chooser
   itself; only the filter constant changed).
3. Drop a `.y12` and an `.opm` file onto the plugin window in Standalone —
   both import into `<userAppData>/GenVst/patches/imported/` and appear in
   the IMPORT tab. Loading either plays the expected timbre.
4. `pluginval --strictness-level 8` passes (loaders are message-thread only;
   no audio-path change).

## Done when

- [ ] `loadY12` works; Y12 byte ordering is verified and the source comment
      cites the reference.
- [ ] `loadOPM` works; DT2 is silently dropped; SSG-EG defaults to 0;
      multi-instrument files load the first block only.
- [ ] `kSupportedPatchExtensions` is the single source of truth for the
      supported-extension list; both Task-14 call sites consume it.
- [ ] New tests cover Y12, OPM, the clamping behavior, the DT2 drop, the
      multi-instrument-first-block behavior, and on-disk fixtures.
- [ ] `pluginval` passes.
