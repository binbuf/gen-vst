# Task 31 — Multi-sample DAC (note-mapped sample kit)

> **Depends on:** Task 07 (single-sample DAC engine), Task 26 (DAC panel
> restyled to the multi-sample grid layout — the visual scaffolding is
> already present and stubbed with a "coming soon" toast).
> **Design references:** [ADR-0014](../design/adr/0014-special-channel-features.md)
> (DAC on a dedicated `ymfm` instance), [`08-ui-views.md`](../design/08-ui-views.md)
> view 3 (single-sample form factor), `docs/genny-ui.md` Bottom Row / D
> section. The Genny screenshot `094629` is the visual reference for the
> note grid that this task makes interactive.
> **Note:** Stamped by Task 26 when the DAC panel was visually restyled to
> Genny's multi-sample layout while the engine still drives a single sample.
> Slots 29 and 30 were occupied by Tasks 29 (VGM logging) and 30 (Scala
> tuning), so this task takes slot 31.

## Objective

Promote the DAC panel's read-only note grid (Task 26) to a functional
multi-sample kit. Each of the 20 grid cells (C-3..G-4 chromatic, 4 rows x
5 cols) binds to its own loaded WAV. Triggering a MIDI note in that range
plays the matched cell's sample at the cell's stored rate / pitch; the
existing single LOAD WAV / CLEAR pair is replaced by per-cell loading
via clicks on the grid.

## Context & key constraints

- **Engine reuse, not replace.** The dedicated DAC `ymfm` instance from
  ADR-0014 keeps its single 8-bit PCM register-write path. Multi-sample
  support is implemented above it: the player selects which sample byte
  stream to feed into `0x2A` per incoming note. Switching samples mid-
  playback restarts the bytestream (no crossfade — this is hardware-
  authentic).
- **20 sample slots maximum (one per grid cell).** A cell with no sample
  is silent on note-on. The grid range is fixed (C-3 .. G-4); notes
  outside it are dropped. Wider keymaps are post-MVP.
- **Per-cell rate.** Each cell stores its own `dac_rate` value (one of
  8000 / 11025 / 22050). Loading a WAV resamples to the cell's rate; the
  existing global `dac_rate` apvts is repurposed as the "default rate for
  the next load" and the per-cell value is persisted alongside the PCM
  in plugin state.
- **Plugin state size.** 20 cells x ~88 kB (1 s at 22050) easily fits in
  the binary state blob; the existing `PluginState` PCM-embed path
  (`07-feature-spec.md`) extends naturally to a `std::vector<DacCell>`.
- **No DAW automation of "which cell".** Cell selection is driven by
  MIDI note, not an apvts param — automation surfaces stay the global
  `dac_enable` / `dac_mode` / `dac_level` from Task 07.

## Scope

- `src/DACKit.{h,cpp}` (new) — owns the 20-cell `std::vector<DacCell>`,
  with `loadCellWav(int cell, juce::File)`, `clearCell(int cell)`,
  `cellForNote(int midiNote)`, and an audio-thread `prepareForNote` that
  arms which PCM stream the next note-on will feed into `0x2A`.
- `src/DACPlayer.cpp` — consume `DACKit` instead of the single embedded
  WAV; the existing `processBlock` write-cadence stays the same.
- `src/PluginState.cpp` — serialise / deserialise `DacKit` cells. Saved
  format: cell count + per-cell { midiNote, rate, sampleByteCount, bytes }.
- `src/PluginEditor.cpp` — new native functions `loadDacCellWav(cell)`,
  `clearDacCell(cell)`, `getDacKit()` returning `{ cells: [{ name,
  lengthSec, rate, midiNote }] }`. Wire up the JS click handler from
  Task 26 to call `loadDacCellWav(cellIndex)`.
- `ui/src/views/d-view.js` — repaint cell labels to show the loaded
  sample name (short form, e.g., `KICK01`) when present; bind cell click
  to the new `loadDacCellWav` native function and remove the deferral
  toast. Empty cells stay labelled with their note name.
- `tests/DACKitTests.cpp` — load / clear cells, MIDI-note dispatch,
  serialisation round-trip via `PluginState`.

## Out of scope

- Velocity-layered samples per cell (one sample per cell, period).
- Pitch shifting beyond the per-cell native rate (no "C-3 sample plays
  at D-3" mapping).
- Drag-drop of WAVs onto cells (LOAD button per cell via the existing
  native file chooser is sufficient for MVP).
- Sample editor (trim, loop points) — the cell stores the raw WAV.

## Implementation steps

1. **DACKit data model.** `DacCell { juce::String name; int midiNote;
   int rate; std::vector<int8_t> pcm; }` plus a `DACKit` owner that
   indexes cells by note.
2. **Loader path.** `loadCellWav(cell, file)` reuses the Task 07 WAV ->
   8-bit PCM resampler; the cell's rate field defaults to the global
   `dac_rate` value at load time.
3. **Audio-thread dispatch.** `DACPlayer::noteOn(int note)` calls
   `kit.cellForNote(note)`; if a cell exists, swaps the active byte
   stream and rate, then triggers the existing register-write cadence.
4. **State persistence.** Round-trip the kit through `PluginState::save`
   / `restore`. Cells with empty PCM serialise as a single zero-length
   marker so the state stays compact.
5. **JS wire-up.** Replace Task 26's `pushNotify(DEFERRED_MSG)` handler
   with `loadDacCellWav(cellIndex)`; refresh the grid render to show
   loaded cell names.
6. **Test.** Round-trip + dispatch test in `DACKitTests`.

## Deliverables

- `src/DACKit.{h,cpp}` — new.
- Updates to `src/DACPlayer.cpp`, `src/PluginState.cpp`,
  `src/PluginEditor.cpp`, `src/CMakeLists.txt`.
- `ui/src/views/d-view.js` — per-cell load wired up.
- `tests/DACKitTests.cpp`, `tests/CMakeLists.txt` updated.

## Verification

1. `cmake --build build/windows-debug --config Debug` succeeds.
2. `ctest --test-dir build/windows-debug -C Debug --output-on-failure`
   — all green; `DACKitTests` passes.
3. Standalone:
   - Click a cell, pick a WAV, the cell label flips to the sample name.
   - Play the matching MIDI note -> the loaded sample plays.
   - Play an empty cell's note -> silence (no clicks, no leftover sample).
   - Save the project, reload -> every cell's sample re-loads.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] 20 cells, each loadable with its own WAV at one of three rates.
- [ ] MIDI notes C-3..G-4 dispatch to their cell; outside range = silent.
- [ ] Kit persists across project save/restore.
- [ ] `DACKitTests` covers load / clear / dispatch / round-trip.
- [ ] `pluginval` SUCCESS.
