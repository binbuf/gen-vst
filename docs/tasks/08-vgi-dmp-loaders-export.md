# Task 08 — VGI & DMP loaders, patch export

> **Depends on:** Task 04.
> **Design references:** `docs/design/04-patch-system.md` (primary — *VGI
> Format*, *DMP Format*, *Loading Code Sketch*, *Folders, Import & Export*),
> ADR-0012.

## Objective

Complete the patch-format support: add the **VGI** and **DMP** loaders and the
**TFI / VGI exporters**, so the plugin can import all three documented formats
and write patches back out.

## Context & key constraints

- **VGI — 43 bytes, no header.** Extends TFI with AMS/FMS. Byte layout in
  `04-patch-system.md` *VGI Format*: byte `0x02` is AMD/FMD (`0b00AA0FFF` —
  bits 5:4 = AMS 0–3, bits 2:0 = FMS/PMS 0–7); operators OP1–OP4 follow, shifted
  one byte vs TFI; **AMON is packed into the DR byte, bit 7**.
- **VGI TL-range caution.** Some sources give OP2–OP4 TL as 0–63 rather than
  0–127. **Verify against the plutiedev reference**
  (`https://www.plutiedev.com/format-tfi`) during implementation and load
  accordingly. This is `02`/`07` open-question item *VGI TL range* — record
  what you find in a code comment.
- **DMP — version 11 (`0x0B`) only** (ADR-0012). Variable-length, version-
  dependent. Files of **any other version are rejected** with a clear error
  message (no best-effort parsing). v11 FM layout in `04-patch-system.md`
  *DMP Format*: byte 0 version, byte 1 system (`0x02` Genesis / `0x42`
  extended — reject anything else), byte 2 instrument type (`0` = FM — reject
  PSG-type DMP), then LFO AMS/FMS, ALG, FB, then per-operator data.
- **DMP v11 byte offsets must be verified** against the Furnace source
  `src/format/dmp.cpp`. Furnace is consulted as a **local, gitignored reference
  checkout only** — never committed, never added as a build dependency.
- All loaders run on the **message thread**, return `PatchLoadResult`
  (`std::optional<Patch>` + error string), and clamp values to hardware ranges.
- A rejected DMP returns a `PatchLoadResult` with a populated `error`. Surfacing
  that error as a UI notification toast is **Task 13** — this task only produces
  the error string.
- **Export:** TFI = a 42-byte buffer; VGI = a 43-byte buffer with AMS/FMS packed
  into byte `0x02` and AMON into the DR byte. Export is the inverse of the
  corresponding loader.

## Scope

- `loadVGI(path)` — 43-byte parse incl. AMS/FMS and AMON unpacking; verified TL
  range.
- `loadDMP(path)` — v11-only parse; reject all other versions / wrong system /
  PSG-type with a clear error.
- `exportTFI(patch, path)` and `exportVGI(patch, path)`.
- Extend `tests/PatchLoaderTests.cpp` to cover VGI, DMP, and round-trips.

## Out of scope

- The patch browser, roots, scanning → Task 09.
- The Import/Export **UI** (file choosers, buttons) and the notification toast →
  Task 14 / Task 13.
- Legacy DMP v8 — explicitly post-MVP (ADR-0012).

## Implementation steps

1. Implement `loadVGI` per the design; verify the OP2–OP4 TL range against the
   plutiedev reference and comment the finding.
2. Implement `loadDMP` for v11; verify byte offsets against a local gitignored
   Furnace checkout (`src/format/dmp.cpp`); reject other versions / systems /
   instrument types with explicit error strings.
3. Implement `exportTFI` and `exportVGI` as the inverse packers.
4. Extend `PatchLoaderTests` with VGI/DMP/export coverage. Since the repo ships
   no `.vgi`/`.dmp` files, construct byte-level fixtures in the test (you control
   the format) and use export→load round-trips.

## Deliverables

Updates to `src/PatchSystem.{h,cpp}` (or `PatchLoader.{h,cpp}`),
`tests/PatchLoaderTests.cpp`.

## Verification

1. `ctest --test-dir build/windows-debug --output-on-failure` — `PatchLoaderTests`
   passes, covering:
   - A hand-built valid VGI fixture loads; AMS/FMS and AMON unpack correctly;
     the chosen TL range is applied.
   - A hand-built v11 DMP fixture loads as an FM patch.
   - A DMP with version ≠ 11 is rejected with a non-empty, descriptive error
     and no patch; likewise a wrong-system byte and a PSG instrument-type DMP.
   - `exportTFI` then `loadTFI` round-trips a `Patch` with no field drift;
     `exportVGI` then `loadVGI` round-trips including AMS/FMS and AMON.
2. Build the plugin — it still builds, and `pluginval --strictness-level 8`
   passes (loaders are message-thread only; no audio-path change).

## Done when

- [ ] `loadVGI` works; the VGI TL-range question is resolved and commented.
- [ ] `loadDMP` parses v11 and rejects every other version with a clear error.
- [ ] DMP v11 offsets verified against the Furnace source.
- [ ] `exportTFI` / `exportVGI` round-trip cleanly.
- [ ] `PatchLoaderTests` covers VGI/DMP/export and passes.
