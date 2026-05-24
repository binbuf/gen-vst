# Task 05 — Voice allocator & parameter system

> **Depends on:** Task 04.
> **Design references:** `docs/design/01-architecture.md` (primary — *Parts and
> Voices*, *Parameter System*, *Render Pipeline*, *Threading Model*),
> `docs/design/07-feature-spec.md` (*Multitimbral Architecture*),
> `docs/design/06-build-system.md` (*Tests*), ADR-0010, ADR-0013.

## Objective

Build the **16-voice shared pool** with LRU stealing, the **6-part
multitimbral** model (`PartManager`), and the **full `apvts` FM parameter
layout** with per-block dirty-diff register writes. After this task the plugin
is a 16-voice polyphonic, 6-part multitimbral FM instrument whose every FM
parameter is DAW-automatable.

## Context & key constraints

- Gen VST is **six-part multitimbral with a shared 16-voice pool** (ADR-0013):
  6 FM parts (each = one `Patch` + one assigned MIDI channel), and 16
  `ymfm::ym2612` voice instances that are **not** statically owned by parts.
- **16 instances, channel 0 each** (ADR-0010) — one voice per instance. Each
  voice records which part and which note it currently serves.
- **Allocation:** note-on → identify the part bound to the MIDI channel → take a
  free voice (LRU steal if none free) → load that part's patch register values
  → key on. **Voice stealing is global LRU**; voices in release phase are
  preferred for stealing over voices in sustain/decay.
- **Polyphony is 16 notes total, shared across all parts** — the pool size, not
  a per-part cap. (Configurable voice count and per-part modes are Task 15.)
- **Parameter system** (`01-architecture.md` *Parameter System*): the `apvts`
  holds a **full FM parameter set per part** — IDs `<name>_op<1-4>_part<1-6>`
  for per-operator params and `<name>_part<1-6>` for per-part channel params,
  plus globals. ≈50 FM params/part ≈ 300 FM parameters. Declare them once in
  `createParameterLayout()` (generate with loops, not 300 hand-written lines).
- **Audio-thread parameter access:** cache a raw `std::atomic<float>*` per
  parameter per part in `prepareToPlay`; read with `*ptr` in `processBlock` — no
  map lookups, no locks, no allocation.
- **Dirty tracking** (`01-architecture.md` *Parameter System*): each `Voice`
  keeps a shadow copy of its last-written register values; `processBlock` diffs
  the voice's part's current params against the shadow and writes **only changed
  registers** to ymfm, so live parameter edits update sounding voices without
  rewriting every register every block.
- The render loop now **sums all active voices** at the native rate into the FM
  mix bus, then resamples once (extends Task 02's single-voice path; ADR-0011).
- Keep `master_gain` from Task 02. **PSG and DAC parameters are added in
  Task 07** — do not declare them here.

## Scope

- `Voice` — owns a `ymfm::ym2612` instance, its serving part index + note + a
  monotonically increasing "last note-on" timestamp for LRU, and the register
  shadow for dirty tracking.
- `VoiceAllocator` — the 16-voice pool: `noteOn(part, note, velocity)`,
  `noteOff(part, note)`, `allNotesOff()`, `allSoundOff()`; free-voice search;
  global LRU steal preferring release-phase voices; renders/sums active voices
  into the native-rate mix buffer.
- `PartManager` — 6 `Part`s, each holding a `Patch` and an assigned MIDI channel
  (defaults: parts 0–5 → MIDI channels 1–6); a `loadPatch(part, Patch)` path.
- `createParameterLayout()` — the full 6-part FM parameter set + globals.
- Raw-pointer parameter cache, built per part in `prepareToPlay`.
- `processBlock` dirty-diff: per active voice, write only the registers whose
  values changed vs the shadow.
- A **basic** MIDI-channel → part note routing sufficient to test polyphony
  (block-granular is acceptable here; Task 06 makes it sample-accurate and adds
  CC/bend/etc.).
- `tests/VoiceAllocatorTests.cpp`.

## Out of scope

- Sample-accurate sub-block MIDI, the full CC map, pitch bend, sustain, program
  change → Task 06.
- PSG/DAC parameters and engines → Task 07.
- Poly/Mono/Unison modes, configurable voice count → Task 15.
- The UI binding to these parameters → Tasks 10–11.

## Implementation steps

1. Implement `Voice` (instance + bookkeeping + register shadow).
2. Implement `VoiceAllocator`: pool, free search, global LRU steal
   (release-phase preferred), note-on/off, all-notes/sound-off, native-rate
   summing render.
3. Implement `PartManager` with the 6 parts and default channel bindings.
4. Write `createParameterLayout()` generating the per-part FM parameter set +
   globals; build the raw-pointer cache in `prepareToPlay`.
5. Implement the dirty-diff register write in `processBlock`.
6. Add basic MIDI-channel→part routing so polyphony is testable now.
7. Write `VoiceAllocatorTests` and register it in `tests/CMakeLists.txt`.

## Deliverables

`src/Voice.{h,cpp}`, `src/VoiceAllocator.{h,cpp}`, `src/PartManager.{h,cpp}`,
updates to `src/PluginProcessor.{h,cpp}` (the `apvts` layout, the cache, the
render loop), `tests/VoiceAllocatorTests.cpp`, `tests/CMakeLists.txt`.

## Verification

1. `ctest` — `VoiceAllocatorTests` passes: free-voice allocation; the 17th
   simultaneous note steals a voice; LRU picks the oldest and prefers a
   release-phase voice; `noteOff` frees a voice; `allNotesOff`/`allSoundOff`
   free everything; a note-on on a given MIDI channel is served by the correct
   part.
2. Build + Standalone: play a 10+ note chord — all notes sound simultaneously
   (true polyphony, not monophonic). Hold 16 notes, play a 17th — the oldest is
   stolen cleanly with no click/stuck note.
3. In a DAW: put different patches on parts via the dev load path, send notes on
   MIDI channels 1 and 2 — the two channels play their two different timbres
   simultaneously (multitimbral).
4. In a DAW: the FM parameters appear in the plugin's automation list (≈300 FM
   params + globals). Automate one operator's TL on part 1 while a note sustains
   — the sounding voice's timbre changes (dirty-diff write works).
5. `processBlock` does no allocation/locking — `pluginval --strictness-level 8`
   passes, including under automation.

## Done when

- [ ] 16-voice shared pool with global LRU stealing (release-phase preferred).
- [ ] 6 parts, each with its own patch + MIDI channel; multitimbral playback works.
- [ ] Full per-part FM parameter set in `apvts`, DAW-automatable.
- [ ] Live parameter edits update sounding voices via dirty-diff writes.
- [ ] `VoiceAllocatorTests` passes; `pluginval` passes under automation.
