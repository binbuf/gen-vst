# Task 22 — Genny-style Instrument Rack & per-instrument routing

> **Depends on:** Task 16 (state persistence) — every preceding task is done.
> **Design references:** `docs/genny-ui.md` (center column), `docs/design/08-ui-views.md` (view 1, view 10), ADR-0013 (multitimbral voice model).
> **Memory:** [[reference-genny-vst-features]] — Genny's instrument-rack UX is the parity bar.

## Objective

Replace the center column's fixed `Instruments` LCD + global FM/SQ/D section pills + placeholder routing strip with a **user-curated instrument rack**: an ordered list of N loaded instruments, each one a typed slot (FM / SQ / D) with its own MIDI channel, transpose, range, detune, and L/R balance. `+` adds, `−` removes, row-click selects. The underlying 6-FM + 3-PSG-tone + 1-noise + 1-DAC engine is **unchanged** (per ADR-0013) — the rack is a UI repackaging that maps each row onto one of the fixed parts.

## Context & key constraints

- **No engine rewrite.** The audio path, voice allocator, MidiRouter routing semantics, and apvts shape stay; we add per-part parameters and reshape the UI on top.
- **Fixed slot pool.** FM rows occupy parts 1–5 (channel 6 reserved for DAC per ADR-0014), SQ rows occupy PSG tone 1–3 + noise, D rows occupy the DAC. `+` finds the next free slot of the requested type; full pool surfaces a toast.
- **Selection drives the active part.** Clicking a rack row swaps the bottom panel via the existing `body[data-section]` mechanism *and* rebinds the right-side controls via the existing channel-paging path (Task 11's `selectChannel` native fn).
- **Type tab becomes read-only.** The current global `FM/SQ/D` section pills become a non-interactive type indicator for the selected row; section selection is implied by row click.
- **Channel slot buttons remain visible but read-only this pass.** FM rows show `1 2 3 4 5`, SQ rows show `M1 M2 M3 M4`, D rows show just `6`. User cannot reassign slot — that's a post-MVP nicety.
- **Existing MIDI ROUTING modal (view 5) stays.** It's now the conflict-overview surface; the inline `MIDI` step-field on the rack row is the primary edit point.
- **Persistence.** All new per-part params persist via the existing `juce::AudioProcessorValueTreeState` state path (Task 16); no new state schema needed beyond the new param IDs.

## Scope

- New per-part apvts params (loop-generated alongside existing per-part FM params):
  - `midi_ch_part<n>` (1–16), `transpose_st_part<n>` (−24..+24), `transpose_oct_part<n>` (−2..+2),
    `note_lo_part<n>` (0–127), `note_hi_part<n>` (0–127), `detune_cents_part<n>` (−100..+100),
    `balance_part<n>` (−1..+1) for n in the union of FM parts, PSG tone+noise, and DAC.
  - Skip any param that already exists; consult `src/PluginProcessor.cpp::createParameterLayout()`.
- `MidiRouter` changes (`src/MidiRouter.cpp`):
  - On note-on/off, filter by `note_lo/hi_part<n>` before forwarding.
  - On forwarded note, apply `transpose_st + 12*transpose_oct` and `detune_cents` (cents become an additional pitch offset; if MidiRouter doesn't already hold a fractional pitch, route the cents via a per-part fine-tune that feeds `Voice::setPitch`).
  - Per-part `midi_ch_part<n>` overrides the hard-coded default channel map; verify the editor UI reads/writes this rather than the routing table directly.
- `PartManager` (`src/PartManager.{h,cpp}`):
  - `bool isPartActive(PartId) const` — true iff the part has a loaded patch path (FM/SQ) or loaded WAV (DAC).
  - `std::optional<PartId> getFreeSlot(InstrumentType)` — first inactive part of the given type.
  - Emit a change-broadcaster signal so the UI can refresh the rack when active/inactive state flips.
- UI rework (`ui/index.html`, `ui/src/views/fm-view.js`, `ui/src/styles/chassis.css`, optional new widget `ui/src/widgets/instrument-rack.js`):
  - Rack list with rows: type icon (sine glyph FM, square SQ, drum-kit pixel D — extend `ui/src/widgets/folder-icon.js`/`gear-icon.js` style), patch name (or `— empty —`), `:::` drag-handle glyph (render only).
  - `+` button → 3-row inline popover (`FM / SQ (PSG) / D (DAC)`) → opens the existing patch browser modal (`ui/src/modals/patch-browser.js`) scoped to that type's roots → selection loads into `getFreeSlot(type)`. If `getFreeSlot` returns nullopt, toast `"All <type> slots are in use."`.
  - `−` button → calls `resetCurrentPart` (or new `clearPart(partId)`) for the selected row; row is removed.
  - Row click → `selectPart(partId)` (extend existing `selectChannel`); UI rebinds.
  - Per-instrument routing strip (replaces the placeholder block at `ui/index.html:81-87`): bind the new params via the existing `bindSlider`/`bindStepField`/`bindCombo` helpers in `ui/src/binding.js`. Two-thumb range slider for `RNG` is a new widget — add `ui/src/widgets/range-slider.js`.
- `docs/design/08-ui-views.md` view 1 — update the center-column description to reflect the rack model and the per-row routing controls; preserve view 10 (polyphony) as a sub-group within the per-instrument strip.

## Out of scope

- User-reassignable channel slots (no slot drag-drop, no slot picker).
- Drag-reorder of rack rows (render only; reorder is post-MVP).
- Removing or hiding the existing MIDI ROUTING modal — it stays as conflict overview.
- Engine-side polyphony rebalancing across active parts.
- Per-instrument color theming.

## Implementation steps

1. **Audit** `src/PluginProcessor.cpp::createParameterLayout()` for existing per-part params; list which of the seven new params already exist (likely some of `midi_ch`, `balance`).
2. **Extend apvts** — add the missing per-part params via the existing loop pattern.
3. **PartManager** — add `isPartActive`, `getFreeSlot`, change-broadcaster signal; expose via a new native function `getRackState()` returning `[{ partId, type, patchName, midiCh, transposeSt, transposeOct, noteLo, noteHi, detuneCents, balance }]`.
4. **MidiRouter** — wire transpose, range filter, detune-cents fine offset. Add unit tests in `tests/MidiRoutingTests.cpp` (range filter clips note-on/off correctly; transpose stacks; detune sums).
5. **Native fns** — add `selectPart(partId)`, `clearPart(partId)`, `addInstrument(type)` (calls `getFreeSlot` and signals JS to open the patch browser scoped to that type's roots).
6. **UI** — build the rack widget, the `+`/`−` controls, the inline type popover, the routing strip with the new range-slider widget. Reuse existing `lcd-list.js` as the rack base.
7. **Patch-browser scoping** — extend `ui/src/modals/patch-browser.js` `open()` signature with an optional `{ scope: 'fm' | 'psg' | 'dac' }` that filters the root list (FM patches → factory + saved + imported FM; PSG → PSG-tagged folders; DAC → `*.wav` only).
8. **Docs** — update `docs/design/08-ui-views.md` view 1; keep view 10 referenced from inside the per-instrument strip.

## Deliverables

- `src/PluginProcessor.cpp` — new per-part params in `createParameterLayout()`.
- `src/MidiRouter.{h,cpp}` — transpose/range/detune filtering + tests.
- `src/PartManager.{h,cpp}` — `isPartActive`, `getFreeSlot`, change broadcaster.
- `src/PluginEditor.{h,cpp}` — `getRackState`, `selectPart`, `clearPart`, `addInstrument` native fns.
- `ui/index.html` — center-column markup change.
- `ui/src/views/fm-view.js`, `ui/src/widgets/instrument-rack.js` (new), `ui/src/widgets/range-slider.js` (new).
- `ui/src/styles/chassis.css` — rack row + routing strip styling.
- `ui/src/modals/patch-browser.js` — scoped open.
- `tests/MidiRoutingTests.cpp` — new cases (range filter, transpose stacking, detune cents).
- `docs/design/08-ui-views.md` — view 1 rewrite.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green; new MidiRouting cases pass.
3. Standalone:
   - Open plugin → rack shows one default FM row on channel 1; no other rows.
   - `+` → pick FM → patch browser → load a patch → second FM row appears on channel 2.
   - `+` → pick SQ → load → SQ row appears using `M1`; bottom panel switches to the SQ view; right-column routing strip rebinds.
   - `+` → pick D → loads a WAV (the existing single-WAV path) → DAC row appears; bottom panel is the D view.
   - Change MIDI channel on row 1 via the step-field → MIDI events on the new channel reach part 1; old channel goes silent.
   - Set transpose +12 on row 1 → notes play one octave higher.
   - Set range to 60–72 on row 1 → notes outside that window are silently dropped.
   - `−` on row 2 → row removed; the underlying part 2 patch path is cleared.
4. Save the preset; quit; relaunch; reload → rack rebuilds with same rows + per-row routing values.
5. `pluginval --strictness-level 8 "build\windows-debug\src\GenVst_artefacts\Debug\VST3\Gen VST.vst3"` — SUCCESS.

## Done when

- [ ] Rack list renders one row per active part; type icons distinguish FM/SQ/D.
- [ ] `+` opens type popover → patch browser scoped to that type → loads into a free slot.
- [ ] `−` clears the selected row's slot and removes the row.
- [ ] Per-instrument MIDI channel, transpose (semitone + octave), range, detune, balance all bind to apvts and audibly affect playback.
- [ ] MidiRouter unit tests for range/transpose/detune pass.
- [ ] State saves and reloads correctly.
- [ ] `pluginval --strictness-level 8` SUCCESS.
- [ ] `docs/design/08-ui-views.md` view 1 reflects the rack model.
