# Task 30 — Scala tuning import (Import Tuning button)

> **Depends on:** Task 24 (IMPORT-tab button exists as a stub), Task 06
> (`MidiRouter::midiNoteToFreq` is the central note → frequency function).
> **Design references:** [Scala `.scl` format](https://www.huygens-fokker.org/scala/scl_format.html),
> `docs/design/07-feature-spec.md` (microtuning is currently 12-TET only).
> **Note:** Stamped by Task 24 when the Import Tuning button was stubbed
> instead of implemented. Renumber if a slot conflict arises.

## Objective

Promote the IMPORT-tab **Import Tuning** button from a "coming soon" toast
(Task 24) to a working `.scl` parser + per-note frequency table that the FM
voice allocator and the PSG tone channels consult. The current implementation
is hard-wired 12-TET via `midiNoteToFreq (note) = 440 * 2^((note-69)/12)`.
This task swaps that single formula for a lookup against the loaded tuning,
falling back to 12-TET when no tuning is loaded.

## Context & key constraints

- **Scala `.scl` format.** ASCII; first non-comment line is a human-readable
  description, second is the scale degree count, then one line per degree
  giving the ratio (as `N/M`) or cents (`123.456`). The 1/1 root is implicit.
  Parse all degrees into a `std::vector<double> cents` of length N
  (excluding the implicit unison).
- **Mapping degrees → MIDI notes.** Scala bundles a `.kbm` (keyboard mapping)
  file alongside `.scl` for octave size + reference frequency. For MVP we
  assume the standard 12-degree mapping rooted at MIDI 69 = 440 Hz. The
  Import Tuning button accepts `.scl` files only; a `.kbm` companion is
  picked up automatically if it sits next to the `.scl` with the same stem.
- **Audio-thread access.** The lookup table is a flat `std::array<double, 128>`
  of pre-computed frequencies, swapped in atomically via
  `std::shared_ptr<TuningTable>` — the audio thread loads the shared pointer
  per voice activation and walks the table with no locking.
- **Reset path.** Loading the same `.scl` twice replaces the table; loading
  an empty / malformed file emits an error toast and leaves the previous
  table intact. The Settings modal grows a "Reset to 12-TET" item that
  swaps in the default table.
- **PSG pitch.** The SN76489 frequency divider is computed from the
  desired Hz; routing PSG tone channels through the same `noteToFreq`
  call as FM keeps both engines in microtune lockstep.

## Scope

- `src/Tuning/Tuning.{h,cpp}` (or top-level `src/Tuning.{h,cpp}` matching
  the rest of the codebase) — `TuningTable` struct with a 128-entry
  frequency array, `parseScl(path, error) → std::shared_ptr<TuningTable>`,
  and a `Tuning` singleton holding the active table.
- `src/MidiRouter.cpp` — replace the inlined 12-TET formula in
  `midiNoteToFreq` with a `Tuning::lookup(note)` call.
- `src/SN76489Engine.cpp` — same swap on the tone-channel pitch path.
- `src/PluginEditor.cpp` — rewrite `importTuningDialog` (currently a stub)
  to open a `*.scl` file picker, parse, swap the shared pointer, and emit
  a notify with the scale description.
- `src/PluginState.cpp` — persist the loaded `.scl` file path so a
  project reload re-parses the same tuning.
- `tests/TuningTests.cpp` — `.scl` parser round-trip (12-TET fixture,
  Pythagorean fixture, malformed fixtures); lookup against MIDI 60–72.

## Out of scope

- `.kbm` keyboard mapping beyond the standard 12-degree assumption.
- Per-channel tuning (one tuning is shared by every FM + PSG voice).
- Live tuning editor UI inside the plugin window.
- MTS (MIDI Tuning Standard) Sysex messages.

## Implementation steps

1. **Scaffold** `Tuning.{h,cpp}` + the `parseScl` parser, plus the atomic
   shared-pointer swap. Add to `src/CMakeLists.txt`.
2. **Hook the lookup.** Replace the 12-TET formula in `midiNoteToFreq` and
   in `SN76489Engine` with `Tuning::activeTable().lookup(note)`.
3. **Wire the button.** Replace `importTuningDialog` stub in
   `PluginEditor.cpp` with the file chooser + parse + swap path.
4. **Persist.** Save the active `.scl` path in `PluginState::save` and
   re-parse on restore. An unresolved path falls back to 12-TET and emits
   a toast.
5. **Test.** `TuningTests` covers 12-TET fixture (== default), Pythagorean
   fixture, malformed lines, missing files.

## Deliverables

- `src/Tuning.{h,cpp}` — new.
- Updates to `src/MidiRouter.cpp`, `src/SN76489Engine.cpp`,
  `src/PluginEditor.cpp`, `src/PluginState.cpp`, `src/CMakeLists.txt`.
- `tests/TuningTests.cpp`, `tests/CMakeLists.txt` updated.
- `tests/fixtures/tunings/` — `12tet.scl`, `pythagorean.scl`, fixtures.
- `docs/design/07-feature-spec.md` — note the microtuning support.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` —
   all green; `TuningTests` passes.
3. Standalone:
   - Click **IMPORT TUNING** → pick `pythagorean.scl` → toast shows the
     scale description.
   - Play a chord across an octave → the pitch relations sound different
     from 12-TET (Pythagorean fifth is wider).
   - Restart the DAW project → tuning re-applies from the persisted path.
   - Pick a malformed `.scl` → error toast; previous tuning remains active.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] `.scl` parser handles ratio + cents lines; rejects malformed input.
- [ ] FM + PSG share the same tuning table.
- [ ] Active `.scl` path persists across project save/restore.
- [ ] `TuningTests` covers the parser + lookup.
- [ ] `pluginval` SUCCESS.
