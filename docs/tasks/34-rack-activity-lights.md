# Task 34 — Per-slot activity lights (instrument rack)

> **Depends on:** Task 22 (instrument-rack widget + per-part state push), Task 27 (drag-drop reorder establishes the per-row interaction surface), Task 33 (copy/paste extends the per-row glyph layout — this task adds another visual band but no new interaction).
> **Design references:** `ui/src/widgets/instrument-rack.js`, `ui/src/widgets/voice-leds.js` (existing LED-grid render style that this task should mimic), `src/VoiceAllocator.h` (per-channel key-on / active state). Reference behavior from Genny's `D:\repos\github\genny\Genny\src\Interface\UIInstrumentElement.cpp:150-171` (per-row `_lights` vector showing which hardware channels are active for that slot).

## Objective

Add a small per-channel activity-LED strip to each rack row so the user can see, at a glance, which hardware channels (FM 1-6, PSG 1-4) are currently keyed-on for that slot. Genny draws ten tiny dots per row that light when MIDI for that slot is dispatched to the corresponding hardware channel; the result is a live "which row is talking to which channel" overview that pairs naturally with the existing per-row routing controls.

## Context & key constraints

- **Read-only.** The lights are pure indicators — no click handlers, no drag, no hover popups in MVP. They report the state the audio engine already computes; the rack widget does not own the data.
- **Data source.** The C++ side already tracks per-voice key-on state (see `VoiceAllocator`). Aggregate that per slot: a 10-bit bitmask per row (FM 6 + PSG 4). Publish at ~30 Hz via the existing notification path so the rack widget can repaint without polling.
- **Cheap render.** Each LED is a 2×2-pixel block in the existing canvas. Drawn from the same palette as `voice-leds.js`. Total visual cost: 10 dots × ROW_H, in a horizontal strip just to the right of the type icon (or beneath the patch name — pick whichever fits the existing layout without pushing other glyphs).
- **Bitmask semantics.** Bit N = "this slot has at least one voice keyed-on on channel N". Released-and-decaying voices count as on until envelope reaches silence — match the existing key-on tracking semantics, do not roll your own.
- **Layout-friendly.** Do not let the LED strip steal space from the drag handle, copy/paste glyphs, or remove cell added by Tasks 27 and 33. If horizontal space is tight, the strip can drop to a single row of dots beneath the patch name rather than alongside it.

## Scope

- `src/VoiceAllocator.cpp` + `.h` (or wherever channel-active state lives) — expose a `std::array<uint16_t, kMaxRackRows> rowActiveBitmask()` (or equivalent) that aggregates the current key-on mask per slot.
- `src/PluginEditor.cpp` — emit the per-row bitmask alongside the existing rack state push at ~30 Hz. Tag it as a separate notify field so the UI can update lights without rerendering everything.
- `ui/src/widgets/instrument-rack.js` — extend `rows[i]` with an `activeMask` field, render a 10-LED strip in `_renderDataRow`, and add a `setActiveMasks(masks: number[])` method that updates the per-row mask without rebuilding the row list.
- `ui/src/views/fm-view.js` (or the rack host) — receive the notify field and call `rack.setActiveMasks(...)`.

## Out of scope

- Per-LED click handlers (e.g., "click to solo this channel").
- DAC channel activity (DAC plays back samples, not keyed voices — the indicator semantics don't cleanly apply; defer until a clear UX is decided).
- Velocity-coded LED brightness (binary on/off only for MVP).
- Hover tooltips identifying which channel each dot represents.
- Activity history / persistence — lights reflect *current* state only.

## Implementation steps

1. **Aggregate per-slot bitmask.** In `VoiceAllocator`, when a voice's key-on state changes, OR its channel bit into the slot's row bitmask; on release-to-silence, clear it. A flat `std::atomic<uint16_t>` per row keeps reads lock-free.
2. **Notify cadence.** Hook the existing rack-state push (or add a sibling notify field) to emit the row-bitmask array at ~30 Hz. If the existing path already emits per-frame, piggyback rather than starting a new timer.
3. **Receive on the UI.** In the rack host view, route the new notify field to `rack.setActiveMasks(masks)`.
4. **Render.** In `_renderDataRow`, after the patch name (or beneath it if space is tight), render 10 dots: bit 0 = leftmost, bit 9 = rightmost. Use `lcd-pixel` for lit, `lcd-base-hi` (the dim variant) for unlit. Snap to integer pixels.
5. **Layout audit.** After integration, manually verify the rack at 1× and 2× UI scale that no glyph is clipped. If the LED strip pushes the remove cell off the right edge, fall back to a beneath-the-name single-row layout (the inline rendering needs no other change — just relocate the dot draw call).

## Deliverables

- `src/VoiceAllocator.cpp` + `.h` — per-row activity-mask aggregation + accessor.
- `src/PluginEditor.cpp` — notify field with the per-row masks.
- `ui/src/widgets/instrument-rack.js` — `activeMask` row field + `setActiveMasks` + LED render.
- `ui/src/views/fm-view.js` (or the rack host) — wire the notify to the rack.

## Verification

1. `cmake.exe --build build/windows-debug --config Debug` succeeds.
2. `ctest.exe --test-dir build/windows-debug -C Debug --output-on-failure` — all green.
3. Standalone:
   - Set up two FM rows on different MIDI channels. Play a chord on the first row's channel → first row's LED strip shows the corresponding FM channel(s) lit; second row stays dark.
   - Release notes → LEDs go out after envelope decay.
   - Add a PSG row, play a note → the rightmost PSG LED in that row lights.
   - The lit channels match the actual voice allocation (check against the existing main voice-LED widget).
4. `pluginval --strictness-level 8` — SUCCESS.

## Done when

- [ ] Each rack row shows 10 per-channel activity LEDs.
- [ ] LEDs light when the audio engine reports a keyed-on voice on that channel for that slot.
- [ ] LEDs go dark after envelope release.
- [ ] Render does not clip drag-handle / copy-paste / remove glyphs at either UI scale.
- [ ] `pluginval` SUCCESS.
