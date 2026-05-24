# Task 16 — State persistence

> **Milestone:** Feature-complete MVP.
> **Depends on:** Task 07, Task 09.
> **Design references:** `docs/design/01-architecture.md` (primary — *State
> Persistence*), `docs/design/07-feature-spec.md` (*State Persistence*),
> `docs/design/04-patch-system.md` (*Cross-OS portability*), ADR-0006, ADR-0013,
> ADR-0014.

## Objective

Implement full DAW **state save/restore** — `getStateInformation` /
`setStateInformation` — so a project round-trips every part, patch, routing,
sample, custom root, and setting. After this the MVP is feature-complete.

## Context & key constraints

- All `apvts` parameters round-trip **automatically** via `apvts.copyState()` /
  the parameter tree — this task handles the **non-`apvts` custom fields**.
- Custom fields appended to the state XML (`07-feature-spec.md` *State
  Persistence* shows the schema; `01-architecture.md` *State Persistence* shows
  the read/write outline):
  - **Per part:** the assigned MIDI channel and the active patch identified
    **by absolute filesystem path** (not a flat bank index — ADR-0006). Stored
    paths may point into the factory root, either user subroot
    (`…/patches/saved/` or `…/patches/imported/`, see Task 14), or any custom
    root. The unresolved-path handling below covers all of these uniformly.
  - **Custom root paths** (the registered patch-browser custom roots).
  - **DAC:** the loaded 8-bit PCM, base64-encoded, so a saved project is
    self-contained (ADR-0014).
  - The **MIDI routing table** (channel → destination map).
  - `voiceCount`, `bendRange`, the UI scale: if these are implemented as `apvts`
    parameters they persist automatically; if they are plain settings,
    serialize them as custom attributes per the `07-feature-spec.md` schema.
    Match whatever Tasks 06/13/15/17 chose — be consistent.
- **`setStateInformation`** restores the `apvts`, re-binds each part's MIDI
  channel, reloads each part's patch by path, restores the DAC PCM, and
  re-registers + re-scans the custom roots.
- **Unresolved paths are not fatal** (`04-patch-system.md` *Cross-OS
  portability*): a patch path that no longer resolves leaves the part's restored
  `apvts` parameter values in place and raises a **notification toast** (Task 13
  `notify` channel); a custom root that no longer resolves is **reported, not
  silently dropped**. Absolute paths will not resolve across OSes / different
  folder layouts — this is an accepted MVP limitation.
- Patch reloads on state restore run on the **message thread** and reach the
  audio thread through the Task 09 lock-free queue — never block the audio
  thread.

## Scope

- `getStateInformation`: `apvts` state + the custom XML fields above.
- `setStateInformation`: parse, restore `apvts`, re-bind MIDI channels, reload
  per-part patches by path, restore DAC PCM, re-register/re-scan custom roots.
- Missing-path handling: retain restored parameters + raise a toast for an
  unresolved patch path; report an unresolved custom root.
- DAC PCM base64 encode/decode.

## Out of scope

- A multi-part "performance" file format — out of MVP scope (`05-ui-ux.md`).
- Anything that already auto-persists via `apvts`.

## Implementation steps

1. Implement `getStateInformation`: serialize the `apvts` plus the custom XML
   fields (parts, custom roots, DAC PCM base64, routing table, any non-`apvts`
   settings).
2. Implement `setStateInformation`: parse and restore everything in the right
   order (parameters → routing → per-part patch reloads → DAC PCM → custom
   roots).
3. Implement the unresolved-path handling with the `notify` toast.
4. Implement DAC PCM base64 encode/decode.

## Deliverables

Updates to `src/PluginProcessor.{h,cpp}` (`get`/`setStateInformation`),
and any state helpers (`src/PluginState.{h,cpp}` optional).

## Verification

1. In a DAW, build a non-trivial project: distinct patches on parts 1–4 from
   different roots, non-default MIDI channels, a registered custom root, a
   loaded DAC WAV, and non-default settings (voice count, bend range, Mono on a
   part, a Unison spread). Save the project, close it, reopen it — **every one
   of those** is restored exactly.
2. Save the project; restart the DAW entirely; reopen — state still restores
   (not just an in-session recall).
3. Two plugin instances in one project each keep their own independent state.
4. Move/rename a patch file referenced by a saved project, then reopen it — the
   affected part keeps its restored parameter values and a notification toast
   reports the missing patch; the rest of the project loads fine. A removed
   custom root is reported, not silently dropped.
5. The DAC sample is restored from the embedded base64 PCM with **no external
   WAV file present** (self-contained project).
6. `pluginval --strictness-level 8` passes (it exercises state save/restore).

## Done when

- [ ] `apvts` + all custom fields (parts, roots, DAC PCM, routing, settings)
      save and restore.
- [ ] A project round-trips fully across a DAW restart.
- [ ] Unresolved patch paths retain parameters + raise a toast; missing roots
      are reported.
- [ ] The DAC sample is self-contained in the project state.
- [ ] `pluginval` passes.
