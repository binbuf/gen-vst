# Task 27 — Drag-drop slot reorder (instrument rack)

> **Depends on:** Task 22 (instrument-rack widget + per-part APVTS layout).
> **Design references:** `ui/src/widgets/instrument-rack.js` (the `:::` handle glyph is already drawn, render-only), `docs/design/08-ui-views.md` view 1 center column. Reference behavior from Genny's `D:\repos\github\genny\Genny\src\Interface\UIInstrumentsPanel.cpp:103-146` (drag-drop slot reorder via index swap).
> **Note:** This slot was originally specified as an LFO waveform selector. The YM2612 LFO is hardwired triangle (verified against ymfm's OPN2 implementation and the Genny source — `D:\repos\github\genny\Genny\src\YM2612\YM2612Enum.h` has no waveform parameter); the original task was abandoned and the slot repurposed.

## Objective

Wire up drag-drop reorder of rows in the instrument rack widget. The `:::` handle glyph in `ui/src/widgets/instrument-rack.js:207-209` is already rendered but inert ("render-only this pass (post-MVP reorder per task)"). This task makes it functional: grabbing the handle and dragging vertically moves the row to its new index in the rack, persists the new order through the part-slot apvts mapping, and updates every dependent view (routing strip, per-instrument panels, MIDI dispatch) to follow.

## Context & key constraints

- **The widget already owns the row data.** `InstrumentRack.rows` is the source of truth and is rendered each call to `render()`. The drag-handle column is at `dragX = removeX - 14` (see `_renderDataRow` around line 200). Hit-testing that column on `pointerdown` starts a drag; vertical motion moves the row in `rows`; release commits the new order.
- **Selection follows the dragged row.** If the user drags the currently-selected row, `this.selected` must track its new index so the rest of the UI (FM view's per-part binding) keeps focusing the same instrument.
- **Persist to apvts.** The rack order is not a JS-only concept — it maps to the apvts part-slot ordering that drives audio dispatch. After commit, the editor pushes the new ordering through a native function (likely `setRackOrder(indices)` or by re-emitting the per-row state used to build the rack). Choose the existing call path that minimises new C++ surface; if there isn't one, add a `reorderRackRow(fromIndex, toIndex)` native function to `PluginEditor.cpp` paralleling the existing add/remove handlers.
- **Add-row stays sticky.** The trailing "+ ADD INSTRUMENT" row (`_renderAddRow`) must not be reorderable — it is always the last entry. Hit-testing during drag must reject that row.
- **Visual feedback.** While dragging, render the dragged row as a translucent "ghost" at the cursor's Y, and draw a 1-px insertion-line cue at the candidate drop position between rows. Keep the existing canvas-driven approach (no DOM elements introduced).
- **Scroll-while-drag.** If the user drags past the top or bottom edge, auto-scroll the rack at ~ROW_H / 200 ms so long racks stay reachable. Reuse the existing `scrollY` / `_maxScroll()` logic.

## Scope

- `ui/src/widgets/instrument-rack.js` — drag state machine (`_dragState = { fromIndex, currentY, ghost }`), `pointerdown` on the handle column, `pointermove` updates a virtual insertion index, `pointerup` commits via a new `onReorder(fromIndex, toIndex)` callback. Auto-scroll near edges. Render the ghost row and insertion indicator.
- `ui/src/views/fm-view.js` (or wherever `InstrumentRack` is instantiated) — pass an `onReorder` handler that calls the native `reorderRackRow` function and refreshes the rack from the resulting state push.
- `src/PluginEditor.cpp` — new `reorderRackRow` native function if no equivalent exists. Delegates to a method on the editor/processor that re-orders the apvts part-slot mapping.
- `src/PartManager.cpp` (or whichever class owns the slot ordering) — `reorderSlot(fromIndex, toIndex)` that moves an entry within the slot list and triggers a state push to the UI.

## Out of scope

- Horizontal reorder (rack is one-column; there is no horizontal axis to swap on).
- Drag-drop *between* different rack widgets (only one rack exists in the UI).
- Reordering via keyboard shortcuts (post-MVP).
- Animating the rest of the rows as the dragged row moves — a static insertion-line cue is sufficient.
- Drop-target indicator on the "+ ADD INSTRUMENT" row (drops there are rejected silently).

## Implementation steps

1. **Drag-handle hit zone.** In `instrument-rack.js`, factor out the drag-handle X range (currently inline at `dragX = removeX - 14`) into a method so `_onPointerDown` can check whether the click landed on the handle vs. the row body. A click on the handle starts a drag; a click on the row body keeps the existing select-row behaviour.
2. **Drag state machine.** Add `_dragState` and the `pointerdown` / `pointermove` / `pointerup` handlers. During drag: update `_dragState.currentY`, compute the candidate insertion index, call `render()` to redraw with the ghost row + insertion cue. Reject the trailing "+" row as a drop target.
3. **Auto-scroll near edges.** When `currentY` is within ~8 px of the top or bottom of the canvas, advance `scrollY` by `ROW_H` per 200 ms via a `requestAnimationFrame` loop. Stop on pointerup.
4. **Commit + callback.** On `pointerup` inside the rack body, if `fromIndex !== toIndex`, call `onReorder(fromIndex, toIndex)`. Move `this.selected` along with the row so focus tracks. Then `render()` once more without the ghost.
5. **Native bridge.** Add `reorderRackRow` to `PluginEditor.cpp` if missing. Wire it to a slot-ordering method on the processor; the processor re-emits the rack state via the existing notification path so the UI rerenders from the new ordering.
6. **State persistence.** The slot ordering must survive a project save/restore. If `PluginState` already serialises the part-slot mapping, no change needed; otherwise extend `PluginState::save` / `restore`.

## Deliverables

- `ui/src/widgets/instrument-rack.js` — drag-drop wiring + ghost render + auto-scroll.
- `ui/src/views/fm-view.js` (or the rack's host view) — `onReorder` handler.
- `src/PluginEditor.cpp` — `reorderRackRow` native function.
- `src/PartManager.cpp` + `.h` (or equivalent slot owner) — `reorderSlot` method.
- `src/PluginState.cpp` — slot-ordering persistence if not already covered.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green. If `PartManagerTests` exists, add a `reorderSlot` round-trip assertion; otherwise the verification is UI-driven.
3. Standalone:
   - Open the FM view with at least three instrument rows.
   - Grab the `:::` handle on row 2 and drag it above row 1 → on release, the row sits in position 1 and the rest re-flow.
   - Drag row 1 past the bottom of the rack with the rack not fully scrolled → the rack auto-scrolls and the row settles at the new bottom.
   - The currently-selected row, when dragged, stays selected at its new index — routing strip and per-instrument panels follow it.
   - Drag a row onto the "+ ADD INSTRUMENT" row → the drag is rejected; the row returns to its original position.
   - Save the project, close, reopen → the rack appears in the new order.
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] Grabbing the `:::` handle and dragging vertically commits a new row order on release.
- [ ] Insertion-line cue and ghost row render during drag.
- [ ] Auto-scroll triggers when the cursor nears the rack edges.
- [ ] Selection follows the dragged row.
- [ ] Slot ordering persists across project save/restore.
- [ ] `pluginval` SUCCESS.
