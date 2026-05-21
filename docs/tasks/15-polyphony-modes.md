# Task 15 — Polyphony modes & voice count

> **Depends on:** Task 05, Task 11.
> **Design references:** `docs/design/07-feature-spec.md` (primary —
> *Polyphony*, *Polyphony Modes*, *Pitch Bend*), `docs/design/08-ui-views.md`
> (view 10), `docs/design/02-fm-synthesis.md` (*Frequency*), ADR-0013.

## Objective

Add the per-part **Poly / Mono / Unison** polyphony modes and the global
**configurable voice count**, with the inline UI controls from view 10.

## Context & key constraints

- Polyphony mode is a **per-part** setting — each of the 6 FM parts is
  independently Poly, Mono or Unison. All modes draw voices from the shared
  16-voice pool (ADR-0013).
- **Poly** (default): standard polyphony with global LRU stealing — already
  built in Task 05. This task adds Mono and Unison alongside it.
- **Mono:** a single voice. New note-on either:
  - **Retrigger** — key-off the current voice, wait one block, key-on the new
    note; or
  - **Legato** — skip key-off, update only the frequency registers, envelope
    continues from its current level.
  Both are implemented; the shipped default is **Retrigger** (this resolves the
  `07-feature-spec.md` open question — record Retrigger as the MVP default).
- **Unison:** all N voices play the same pitch, each detuned by a per-voice
  **F-number offset** — **not** the YM2612 DT register (DT is a coarse 3-bit
  detune and cannot express cents). Offsets fan out symmetrically (voice 0 = 0,
  ±1, ±2, … per `07-feature-spec.md` *Unison*). Spread is a parameter in cents
  (0–50); compute each voice's F-number for its detuned pitch. The default
  spread is **12 ¢** (the value shown in the view 10 spec).
- **Voice count** is global: **8 / 12 / 16**, default 16 (`07-feature-spec.md`
  *Poly*). The Settings control exists from Task 13; this task makes the
  `VoiceAllocator` honor it (cap the usable pool size).
- **View 10 controls** (`08-ui-views.md` view 10) live inline in the FM
  section's center-column control stack (Task 11 left a placeholder): a
  `POLY/MONO/UNISON` selector; when `MONO`, a `GLIDE` retrig/legato toggle
  appears; when `UNISON`, a `SPREAD` slider (cents) appears. These are per-part
  `apvts` parameters and **re-bind on part selection** like the rest of the FM
  controls.

## Scope

- Per-part mode parameters (poly/mono/unison, mono glide, unison spread).
- `VoiceAllocator` / `Voice` changes for Mono (retrigger + legato) and Unison
  (multi-voice F-number spread).
- Honor the global voice-count setting in the allocator.
- The view 10 inline UI controls with conditional GLIDE/SPREAD sub-controls,
  bound per-part and re-binding on part selection.

## Out of scope

- Chord mode (note-range zones) — explicitly post-MVP (`07-feature-spec.md`).
- Per-part voice reservations/caps — post-MVP (ADR-0013).

## Implementation steps

1. Add the per-part mode parameters to `createParameterLayout()`.
2. Implement Mono in the allocator: retrigger and legato note handling.
3. Implement Unison: allocate N voices per note with the symmetric F-number
   spread; recompute on pitch bend too.
4. Make the allocator respect the global voice-count setting.
5. Build the view 10 controls; show GLIDE only in Mono, SPREAD only in Unison;
   bind per-part with paging re-bind.

## Deliverables

Updates to `src/VoiceAllocator.{h,cpp}`, `src/Voice.{h,cpp}`,
`src/PluginProcessor.cpp` (parameter layout), `ui/src/views/fm-view.*` (the
view 10 controls).

## Verification

1. **Poly** still behaves as before (Task 05 regression check).
2. **Mono:** set a part to Mono — overlapping notes play monophonically.
   `GLIDE = Retrigger` re-attacks the envelope on each new note; `GLIDE = Legato`
   slides pitch with the envelope continuing — the two are audibly different.
3. **Unison:** set a part to Unison — one key plays a thick, detuned stack;
   increasing `SPREAD` widens the chorus; `SPREAD = 0` collapses to a single
   pitch. Unison pitch bend stays coherent across the stack.
4. Mode is **per-part**: part 1 Mono and part 2 Poly behave independently at
   the same time.
5. **Voice count:** set it to 8 — only 8 simultaneous voices sound (the 9th
   steals); set to 16 — 16 sound. The view 10 controls re-page correctly with
   the channel selector.
6. `pluginval --strictness-level 8` passes.

## Done when

- [ ] Poly / Mono / Unison all work, selectable per part.
- [ ] Mono supports retrigger and legato; default is retrigger.
- [ ] Unison detunes via F-number offset with a cents spread (default 12 ¢).
- [ ] The global voice count (8/12/16) is honored by the allocator.
- [ ] View 10 controls work and re-page with part selection.
- [ ] `pluginval` passes.
