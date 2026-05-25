# Task 11 — State persistence & v2 parity audit

> **Milestone:** Release-ready — every apvts parameter round-trips
> through the DAW project; the active patch path and registered custom
> roots survive save / load; the v2 parity checklist from
> `07-feature-spec.md` is clean; pluginval and the CI matrix pass on
> Windows, macOS, and Linux.
> **Depends on:** Task 10.
> **Design references:** `docs/design/04-patch-system.md`
> (*Plugin state file `.gnvst`*), `docs/design/01-architecture.md`
> (*State Persistence*), `docs/design/07-feature-spec.md` (*State*,
> *Genny VST FM Parity Checklist*, *Extensions Beyond Genny*),
> `docs/design/06-build-system.md` (*GitHub Actions CI*, *Platform
> Targets*), ADR-0025, ADR-0016, ADR-0017.

## Objective

Land DAW state persistence — the apvts tree + the active patch path +
the registered custom roots — so saving a DAW project and re-opening
it brings the plugin back to where the user left it, including the
active preset and the user-added custom patch roots.

Then run the v2 parity audit: walk every line of
`07-feature-spec.md`'s checklists; mark each *shipped* or carry it to
the post-MVP backlog with a one-line note. Run `pluginval` strictness
8 on each platform's build. Confirm CPU profiling is within the
target band.

After this task, the mvp2 chain is complete and the plugin is ready
for a v2 release.

## Context & key constraints

- **`.gnvst` plugin state file** (`04-patch-system.md` *Plugin state
  file*):
  - Contents: the full `apvts` XML (mode + FM + SQ + D + globals),
    the active patch path (one path; the patch's extension implies
    its tag and therefore the mode), the registered custom-root paths.
  - **No embedded base64 PCM** in v2 (D mode does not load WAV files
    per ADR-0021).
  - Custom-root paths and the active-patch path are stored as
    **absolute filesystem paths**. Unresolvable paths on load are
    reported via a notification toast and do not block — the
    restored apvts values stay in place and the instance keeps its
    last known sound (`01-architecture.md` *State Persistence*).
- **State envelope shape** (per `07-feature-spec.md` *State Persistence*):
  ```xml
  <GenVstState>
    <patch path="…/factory/bass.tfi"/>      <!-- absent if no patch -->
    <customRoots>
      <root path="…"/>
    </customRoots>
    <!-- apvts parameter tree follows here -->
  </GenVstState>
  ```
- **Mode auto-switch on restore** (ADR-0025): the restore path applies
  the patch via the same `loadPatch` flow as the browser — if the
  patch's tag differs from the restored `mode_select`, the path's
  tag wins (so a project where the user manually flipped to D *after*
  loading an FM patch will restore the FM patch and switch back to FM;
  this is consistent with "preset tag = mode"). Alternatively the
  restored apvts `mode_select` wins if no patch path is present.
- **Pending-state-restore pattern** (already in v1 PluginProcessor —
  `pendingStateRestore`): the patch browser needs the JUCE wrapper
  type set by the plugin client wrapper after the constructor returns,
  so the patch reload + custom-root re-register are deferred to the
  first `prepareToPlay`. Keep this pattern; the per-mode envelope is
  slightly different (one patch path, not six).
- **`v2 parity checklist`** — `07-feature-spec.md` lists every feature
  Gen VST aims to ship. Walk each one and either:
  - Tick `[x]` if shipped (the task that landed it can be cited),
  - Or move the line to the post-MVP backlog in
    `docs/tasks/mvp2/README.md` *Post-MVP backlog* with a one-line
    note explaining why it's deferred.
- **Cross-platform CI** (`06-build-system.md` *GitHub Actions CI*):
  the three jobs already exist from v1. Verify they still pass for v2:
  - **Windows** — VST3 + Standalone build; pluginval level 8.
  - **macOS** — VST3 + AU + Standalone; `auval -v aumu Genv GnVs`.
  - **Linux** — VST3 + Standalone; `apt` deps include
    `libwebkit2gtk-4.1-dev`.
- **CPU profiling pass** (Open Question #1 from `07-feature-spec.md`):
  16 ymfm instances at 44,100 Hz should stay well under 30 % of one
  core on a modern desktop. Use a real DAW (Reaper) under a
  stress-test project: 16-voice chord held + LFO active + Filter +
  Ladder both on + AUTO_RETRIG mode + the level-meter telemetry
  pumping. Record the figure; if it exceeds the target the issue is
  filed under post-MVP (CPU profiling itself is the deliverable for
  this task, not the optimisation).

## Scope

- **`src/PluginState.{h,cpp}`** rewrite:
  - `getStateInformation`:
    1. Snapshot `apvts.copyState()`.
    2. Build a `<GenVstState>` XML envelope. Append a `<patch
       path="…"/>` element when `patchBrowser` has an active patch
       (call it `getActivePatchPath()` — Task 09 should already
       provide that accessor). Append a `<customRoots>` block listing
       every registered custom-root path.
    3. Place the apvts XML *inside* the `<GenVstState>` envelope (or
       alongside it — pick one shape and keep it stable; the design
       sketches "the apvts parameter tree follows here" inside the
       envelope, so prefer that).
    4. Serialise to the `juce::MemoryBlock`.
  - `setStateInformation`:
    1. Parse the bytes into XML.
    2. Pull the `<patch>` and `<customRoots>` data into
       `pendingStateRestore`; pull the apvts XML and apply it via
       `apvts.replaceState(...)`.
    3. Set `stateRestored = true`.
    4. The first `prepareToPlay` after restore drains
       `pendingStateRestore`: re-register every custom root with the
       PatchBrowser (raise a toast per unresolved path); load the
       active patch by path (raise a toast if the path no longer
       resolves; on success, the patch-load flow's mode-auto-switch
       handles `mode_select`).
- **Editor → Processor wiring**: a new `getActivePatchPath()` /
  `getCustomRoots()` accessor pair on the processor surfaces what
  `getStateInformation` needs. (Likely already exist from v1 — verify
  they survived Task 02's strip.)
- **Stateful UI selection** — `mode_select` is in apvts so it
  round-trips. The header's selected-mode pill, the FM panel's
  selected op-badge, the browser's last-selected chip — only
  `mode_select` round-trips; the op-badge selection and chip choice
  are JS-side UI state that defaults on every editor mount (and
  that's correct, per the v2 spec; no need to persist them).
- **CI matrix verification** — push to a feature branch and confirm
  all three jobs pass. Fix any v2-introduced regressions (typically:
  Linux WebKitGTK deps; macOS code-signing for AU validation).
- **`pluginval` matrix** — Windows + macOS + Linux runs of
  `pluginval --strictness-level 8 --validate`. Record any failures
  and fix or carry forward (only fix; pluginval failures must not
  ship).
- **CPU profile run** — record peak CPU % on the Reaper stress
  project. Add a one-line note to `07-feature-spec.md` *Open
  Questions* #1 with the measured number and the verdict.
- **Parity audit** — walk every checkbox in `07-feature-spec.md`:
  - **FM Parity Checklist** — every line ticked or moved to backlog.
  - **Extensions Beyond Genny** — same.
  - Update `docs/tasks/mvp2/README.md` *Post-MVP backlog* with any
    moved items.

## Out of scope

- New features. This task only persists, audits, and validates.
- Optimising the CPU profile (the *measure* is the deliverable here;
  follow-up optimisation, if needed, is a post-MVP exercise).
- A signed/notarized installer pipeline (ADR-0016 — bundle-only
  ships for v2; installer work is post-MVP).

## Implementation steps

1. **`PluginState` rewrite** as above. Lean on the existing
   pending-state-restore pattern in `PluginProcessor`; only the
   contents shift to v2's single-patch model.
2. Verify the editor reads the right defaults when no project state
   is present: a fresh instance starts in FM mode with no active
   patch; the patch-name LCD shows "—"; the panel renders empty
   defaults.
3. Run a manual DAW save / load test on Windows: load `bass.tfi`,
   tweak a couple of knobs, save the project, close Reaper, re-open
   the project. The plugin returns with the same patch loaded, the
   tweaked knobs, the same mode.
4. Repeat for SQ (`.psg`). Confirm the SQ panel is mounted on restore
   and the per-channel envelope / vol / pan / detune values match.
5. **D-mode state persistence** — set `mode_select = D`; route audio
   into the plugin; tweak `prescaler` to ~0.7, `mono` to on, `dry_wet`
   to ~0.5. Save the project; close Reaper; re-open the project. The
   plugin restores in D mode with all three apvts values intact (no
   `.gdac` file involved — the state rides on the host's normal
   project state envelope via `setStateInformation`).
6. **Mode-switch + restore** — start in FM with a patch loaded; flip
   to D and tweak the decimator; save the project; close; re-open —
   plugin restores to D mode with the tweaked values, **and** the FM
   patch path is still remembered so flipping back to FM restores it.
7. **Custom-root persistence** — add an external folder via the
   browser's *Add Folder…*; close & re-open the project; the folder
   is still in the tree, still scannable.
8. **Missing-patch toast** — delete the active patch file on disk,
   re-open the project; a notification toast surfaces "Patch could
   not be loaded: <path>"; the apvts values remain so the sound is
   approximately what was saved.
9. **Missing custom-root toast** — same, with a removed external
   folder.
10. **Parity audit** — open `07-feature-spec.md`, walk every checkbox.
    Tick those that ship. Move any deferred line to the README's
    Post-MVP backlog. Common candidates: `.kbm` keyboard mapping
    (already deferred), MTS Sysex, multi-instrument OPM bank import,
    Channel 3 special as a top-level editor (post-MVP per ADR-0014).
11. **CI pass** — push to a v2-final branch; confirm
    Windows / macOS / Linux jobs are green.
11. **CPU profile** — open the Reaper stress project, hold a 16-voice
    chord with FM AUTO_RETRIG + LFO + filter + ladder + telemetry
    active for ~60 s; record peak CPU. Add a one-line note to
    `07-feature-spec.md` *Open Questions* #1.
12. **`pluginval` strictness 8** on each platform's bundle. Fix
    anything that fails.

## Deliverables

- `src/PluginState.{h,cpp}` rewritten for v2.
- `tests/PluginStateTests.cpp` (new or rewritten) covering:
  - Round-trip of apvts.
  - Round-trip with one `<patch path="…"/>` element.
  - Round-trip with two `<customRoots>` entries.
  - Unresolvable patch path queues a pending notification.
  - State without the `<GenVstState>` envelope (legacy v1 byte
    stream) is recognised as legacy and rejected gracefully with a
    one-line message (do NOT attempt to migrate v1 state — projects
    saved on v1 stay on v1; the v2 release notes warn users).
- Updated `docs/tasks/mvp2/README.md` (post-MVP backlog entries
  moved from `07-feature-spec.md`).
- Updated `docs/design/07-feature-spec.md` (parity-checklist ticks +
  open-questions #1 CPU measurement note).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green, including the PluginState tests.
2. **Cross-platform** — the GitHub Actions matrix is green for Windows,
   macOS, Linux. Each job's `pluginval --strictness-level 8` passes.
3. **DAW state round-trip on Windows** — save/load Reaper project
   restores the patch, the knobs, the mode, the custom roots, the
   notification toast for any unresolved path.
4. **DAW state round-trip on macOS** — same, in Logic Pro (AU). Verify
   `auval -v aumu Genv GnVs` passes.
5. **DAW state round-trip on Linux** — same, in Bitwig / Reaper.
6. **CPU profile** — 16-voice chord stress project records < 30 %
   peak CPU on a modern desktop (the *measure* is the deliverable;
   if exceeded, file as post-MVP).
7. **Parity audit** — every line in `07-feature-spec.md`'s checklists
   is either ticked (shipped, with the task number) or moved to the
   README's Post-MVP backlog with a one-line note.

## Done when

- [ ] State round-trips through DAW save / load including patch path
      + custom roots.
- [ ] Unresolved paths raise toasts but don't block load.
- [ ] PluginStateTests pin the v2 envelope shape.
- [ ] Three-platform CI matrix is green; `pluginval` strictness 8
      passes on each platform's bundle.
- [ ] CPU profile recorded; figure noted in `07-feature-spec.md`
      Open Questions #1.
- [ ] Parity-checklist audit complete; every line ticked or moved to
      the README's Post-MVP backlog.
