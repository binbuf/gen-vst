# Task 33 — Per-slot copy/paste (instrument rack)

> **Depends on:** Task 22 (instrument-rack widget), Task 27 (drag-drop reorder establishes the per-row interaction surface this task extends).
> **Design references:** `ui/src/widgets/instrument-rack.js`, `src/PartManager.h` (per-part state owner), `src/PatchSystem.h` (the `Patch` struct that is the clipboard payload for FM rows). Reference behavior from Genny's `D:\repos\github\genny\Genny\src\Interface\UIPresetElement.cpp:13-15` (`kCopyButton` / `kPasteButton` on each slot, clones state without file I/O).

## Objective

Add **copy** and **paste** to each rack row so the user can duplicate an instrument into another slot without going through file export/import. Genny ships this as a pair of small buttons per row; gen-vst lacks any in-rack clone affordance today. The clipboard holds one entry in process memory, scoped to the editor session.

## Context & key constraints

- **Clipboard scope: editor-session only.** The clipboard does not persist across project save/restore or DAW restarts. It lives on the editor (JS side) or processor (C++ side) — pick whichever owns the rack-row state more naturally. The Genny behavior is the same; persistence across sessions would be surprising.
- **Per-row affordance.** Each rack row gets a small "copy" glyph next to the existing "-" remove cell, and a "paste" glyph that appears only when the clipboard is non-empty. Keep both glyphs in the same canvas-rendered style as the existing remove cell — no DOM elements.
- **Cross-type rules.** The clipboard is typed: FM-row clipboard cannot paste into a PSG row, and vice versa. DAC rows are their own type. Attempting a cross-type paste shows a one-line toast ("Cannot paste FM patch into PSG slot") and is otherwise a no-op.
- **Atomic paste.** Pasting overwrites all of the target slot's parameters in one operation; the rack rerenders from the new state. Undo is out of scope for MVP (gen-vst doesn't have a generic undo path yet).
- **Empty slot paste.** If the target slot is empty (a row that was just added via "+ ADD INSTRUMENT"), paste populates it and the rack-row label updates to the source patch's name.

## Scope

- `ui/src/widgets/instrument-rack.js` — render copy/paste glyphs in `_renderDataRow`, add hit zones to `_onClick`, expose `onCopy(row)` and `onPaste(row)` callbacks alongside the existing `onSelect` / `onAdd` / `onRemove`.
- `ui/src/views/fm-view.js` (or wherever `InstrumentRack` is constructed) — track `clipboardRow` in editor-side state; wire `onCopy` to capture the current row's state via a native function, `onPaste` to apply the clipboard via a native function. Refresh the rack render so the paste glyph appears once a copy has occurred.
- `src/PluginEditor.cpp` — `copySlot(rowIndex)` returns the row's full state as a serialised blob (or `nullptr` if empty); `pasteSlot(rowIndex, blob)` applies it. Choose a blob format consistent with the existing per-patch serialisation (whatever `loadInstrument` / `PluginState` already uses for one slot).
- `src/PartManager.cpp` (and PSG / DAC equivalents) — `captureSlotState(int slotIndex)` and `restoreSlotState(int slotIndex, ...)`. For FM, this is the existing `Patch` struct plus the routing/range/transpose/balance/etc. apvts values for that part. For PSG / DAC, the analogous per-slot state.

## Out of scope

- Multi-slot clipboard / clipboard history.
- Cross-DAW-session clipboard (system clipboard integration).
- Drag-drop copy (hold modifier while dragging) — keep the explicit button per Genny's UX, easier to discover.
- Undo of paste.
- Copy/paste of only a subset of slot state (e.g., only the routing strip, only the FM patch). One slot = one atomic unit.

## Implementation steps

1. **Glyph rendering.** In `_renderDataRow`, add a `copyX` cell to the left of `removeX`. Draw a small "C" or two-rectangle glyph. If `clipboardRow` is set and the row is paste-compatible with it, draw a "P" / overlay glyph in the same column otherwise occupied by the remove cell's left edge — or pop the paste glyph in a separate cell. Keep horizontal layout compact; nudge `dragX` left if needed.
2. **Hit testing.** In `_onClick`, route clicks on the copy / paste cells to the new `onCopy` / `onPaste` callbacks. Preserve the existing select-row / remove-row / drag-handle behaviours.
3. **Clipboard state.** Editor-side `clipboardRow = { type, payload }` where `payload` is the opaque blob returned by `copySlot`. After a copy, call `rack.setClipboardType(type)` so the rack render knows which paste glyphs to show.
4. **Native bridge.** `copySlot` / `pasteSlot` on the C++ side. For the FM case, reuse the existing patch serialisation (the same shape `loadInstrument` consumes). For PSG / DAC, mirror that pattern.
5. **Cross-type guard.** Both the rack widget (visual: hide paste glyph on incompatible rows) and the native `pasteSlot` (defensive: returns an error if types mismatch) enforce the rule. UI shows the toast.
6. **State push.** After a successful paste, the processor emits the same state push that other rack-mutating operations use so the rack rerenders with the new row state.

## Deliverables

- `ui/src/widgets/instrument-rack.js` — copy/paste glyphs + callbacks.
- `ui/src/views/fm-view.js` (or the rack host) — editor-side clipboard + native-function wiring.
- `src/PluginEditor.cpp` — `copySlot` / `pasteSlot` native functions.
- `src/PartManager.cpp` + `.h` (and PSG/DAC equivalents) — `captureSlotState` / `restoreSlotState`.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green. Add a `PartManagerTests` assertion for `captureSlotState` / `restoreSlotState` round-trip if the test suite exists.
3. Standalone:
   - Load an FM patch into row 1. Click copy on row 1 → paste glyph appears on every empty / FM row.
   - Click paste on an empty FM row → row populates with the copied patch; routing strip / per-part panels reflect the new state.
   - Modify row 1's algorithm. Row 2 (the paste target) is unaffected — paste is by value.
   - Try pasting into a PSG row → toast appears, PSG row unchanged.
   - Click paste on a populated FM row → that row's existing state is overwritten.
   - Close the DAW project and reopen → the clipboard is gone (paste glyphs absent) but every row's state persists.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] Copy glyph visible on every rack row; click captures slot state to the editor clipboard.
- [ ] Paste glyph visible only when clipboard is non-empty and the row's type matches.
- [ ] Paste atomically overwrites the target slot's state and refreshes dependent views.
- [ ] Cross-type paste is rejected with a toast.
- [ ] Clipboard does not persist across DAW sessions (this is the intended behaviour).
- [ ] `pluginval` SUCCESS.
