# Task 06 — MIDI pipeline

> **Milestone:** Playable 6-part multitimbral FM instrument.
> **Depends on:** Task 05.
> **Design references:** `docs/design/01-architecture.md` (primary — *MIDI
> Pipeline*, *Render Pipeline*), `docs/design/07-feature-spec.md` (*MIDI CC Map*,
> *Pitch Bend*, *Program Change*), ADR-0013.

## Objective

Make the instrument fully **playable**: replace block-granular note handling
with **sample-accurate** MIDI processing and implement the complete dispatch —
the routing table, note on/off, velocity→TL, pitch bend, the full CC map,
sustain, the panic CCs, program change, and aftertouch routing.

## Context & key constraints

- **Sample-accurate rendering** (`01-architecture.md` *MIDI Pipeline* /
  *Render Pipeline*): iterate the `juce::MidiBuffer` in timestamp order; for each
  gap between consecutive MIDI events, render audio for exactly that sub-block,
  then dispatch the event. Do not quantize events to block boundaries.
- **Routing table:** each MIDI channel maps to exactly one destination. This
  task wires the **6 FM parts** (default MIDI channels 1–6). Build the table so
  PSG-slot and DAC destinations slot in cleanly — **Task 07 adds those entries**.
- A **CC affects the part bound to the channel the CC arrived on**, not the
  UI-selected part.
- **CC map:** implement the full table in `07-feature-spec.md` *MIDI CC Map*.
  Scaling: `hardware_val = round(cc_val × max_val / 127.0f)`. Each CC writes the
  corresponding `apvts` parameter for that part (the parameters already exist
  from Task 05), so CC and DAW automation share one path.
- **Velocity → TL** is a toggle (default on). When on, scale carrier-operator TL
  by note velocity.
- **Pitch bend:** `semitone_offset = (bend/8192) × range`; range is configurable
  ±1/±2/±7/±12 semitones, **default ±2**. On a bend, recompute F-number + BLK for
  **every active voice of the part on the bent channel**.
- **Sustain (CC 64):** holds a part's voices through note-off until the pedal
  releases. **CC 120** all-sound-off and **CC 123** all-notes-off must
  immediately free voices (123 with release, 120 immediate). **CC 121** reset
  all controllers.
- **Program Change:** loads the **Nth factory patch in sorted order** into the
  part bound to that channel. Enumerate the staged factory patch directory
  sorted by filename for a stable index. (This enumeration is superseded by the
  patch-browser roots in Task 09 — keep it small and replaceable.)
- **Aftertouch (channel pressure):** routed to a configurable target. Implement
  the routing mechanism with the **default target = LFO depth** (PMS); the
  Settings control to change it (Off / LFO depth / Carrier TL) is Task 13. This
  is `07-feature-spec.md` open question 5 — record LFO depth as the chosen MVP
  default.
- All MIDI handling stays on the audio thread; **no allocation/locks** in
  `processBlock`. Patch loads triggered by Program Change use the message-thread
  load + lock-free queue pattern (`01-architecture.md` *Threading Model*) — or,
  if the factory patch set is preloaded into memory at `prepareToPlay`, a
  Program Change can swap an in-memory `Patch` directly. Pick one and keep the
  audio thread allocation-free.

## Scope

- Sample-accurate `MidiBuffer` iteration with per-sub-block rendering.
- The MIDI-channel → destination routing table (6 FM parts; extensible).
- Note on/off with velocity; velocity→TL toggle.
- Pitch bend with configurable range; F-number recalculation for active voices.
- The full CC map from `07-feature-spec.md`.
- Sustain (CC 64); CC 120 / 121 / 123.
- Program Change → Nth sorted factory patch.
- Aftertouch routing (default: LFO depth).
- Parameters for the bend range, velocity→TL toggle, and aftertouch target
  added to `createParameterLayout()` if not already present.
- A unit test for routing + CC scaling where it can be tested without a host.

## Out of scope

- PSG / DAC routing destinations and engines → Task 07.
- The MIDI routing **editor UI** and Settings UI → Task 13.
- Poly/Mono/Unison modes → Task 15.
- Patch-browser roots (Program Change uses a minimal sorted enumeration here) →
  Task 09.

## Implementation steps

1. Refactor `processBlock` to iterate MIDI events in timestamp order and render
   audio per sub-block between events.
2. Build the routing table; route note-on/off to `VoiceAllocator` via the bound
   part.
3. Implement velocity handling + the velocity→TL toggle.
4. Implement pitch bend (range parameter + active-voice F-number recalc).
5. Implement the full CC map with the scaling formula; CC writes the part's
   `apvts` parameters.
6. Implement sustain (CC 64) hold logic and CC 120 / 121 / 123.
7. Implement Program Change → sorted factory patch.
8. Implement aftertouch routing with the LFO-depth default.
9. Write the routing/CC unit test.

## Deliverables

`src/MidiRouter.{h,cpp}` (or routing inside `PluginProcessor`),
updates to `src/PluginProcessor.{h,cpp}`, `src/VoiceAllocator.{h,cpp}`,
`tests/MidiRoutingTests.cpp` (new; add to `tests/CMakeLists.txt`).

## Verification

1. `ctest` — the routing/CC-scaling test passes (channel→part mapping; CC value
   scaling for several CCs incl. boundary values 0 and 127).
2. DAW, multitimbral: load distinct patches on parts 1–3, send notes on MIDI
   channels 1/2/3 — three different timbres play independently and
   polyphonically at once.
3. Velocity: soft vs hard notes differ in level/brightness; toggling velocity→TL
   off removes the velocity response.
4. Pitch bend: a bend wheel bends the held notes of that channel's part
   smoothly and in tune; changing the range parameter changes the bend depth;
   other parts are unaffected.
5. CC automation: automate several mapped CCs from the DAW — the corresponding
   parameter and the sound both respond; the same CC on a different channel
   affects only that channel's part.
6. Sustain: hold CC 64, release keys — notes sustain; release CC 64 — they
   release. CC 123 / CC 120 immediately silence everything (panic works).
7. Program Change: send PC values — the target channel's part swaps to the Nth
   factory patch; the timbre changes audibly; no audio-thread glitch.
8. `pluginval --strictness-level 8` passes, including with MIDI + automation.

## Done when

- [ ] MIDI is processed sample-accurately with per-sub-block rendering.
- [ ] 6 FM parts play independently on their routed MIDI channels.
- [ ] Velocity, pitch bend (configurable range), and the full CC map work.
- [ ] Sustain and the panic CCs (120/121/123) work.
- [ ] Program Change loads the Nth factory patch without an audio glitch.
- [ ] Aftertouch routes to LFO depth by default.
- [ ] Routing unit test and `pluginval` pass.
