# Task 09 — Tagged unified preset browser & .psg format

> **Milestone:** Tagged preset browser — one browser modal shows FM and
> SQ presets across all roots; loading a preset auto-switches the
> instance mode if the preset's tag differs from the current mode.
> D mode does not appear in the browser (no preset format).
> **Depends on:** Task 08.
> **Design references:** `docs/design/08-ui-views.md` view 5 (primary —
> the browser modal layout) + view 1 (header patch chrome, including
> the D-mode greying behaviour), `docs/design/04-patch-system.md` (Tag
> table, `.psg` schema, browser design, drag-and-drop semantics),
> ADR-0025, ADR-0006.

## Objective

Replace the v1 patch browser modal with the v2 **tagged unified preset
browser**: one modal that shows every FM and SQ preset from every root
(Factory / Saved / Imported / custom), filtered by a top-of-modal chip
row. Loading a preset switches the instance to the preset's mode if it
differs from the current mode. Add the one new v2 preset format —
`.psg` (SQ) — alongside the FM patch loaders.

After this task, the user can: open the browser from the header 📂
button (when in FM or SQ); filter by mode; click a `.psg` from the SQ
factory pool and have the plugin flip to SQ mode and apply the preset;
drag a `.psg` file onto the plugin window and have it import into the
user-imported root; drop a folder onto the window and have it register
as a custom root.

D mode has no preset format ([ADR-0025](../../design/adr/0025-tagged-preset-browser.md)
*Alternatives considered*): its 3 apvts params (`prescaler`, `mono`,
`dry_wet`) persist via the host's project state, exactly like any
built-in DAW audio FX.

## Context & key constraints

- **Tag derivation by extension** (ADR-0025, `04-patch-system.md`
  *Tagging*):

  | Extension | Tag |
  |---|---|
  | `.tfi`, `.vgi`, `.dmp`, `.y12`, `.opm`, `.vgm`, `.vgz` | FM |
  | `.psg` | SQ |

  D mode has no extension that maps to a tag. `.wav` is **not** a
  recognised v2 tag. `tagFromExtension(ext)` and
  `kSupportedPatchExtensions` live in `src/PatchSystem.{h,cpp}`; both
  the file picker and the drag-and-drop handler consume them.
- **`.psg` JSON schema** (`04-patch-system.md` *.psg Format*):
  ```json
  {
    "version": 1,
    "name": "Soft Lead",
    "channels": {
      "tone1": { "atk":8, "dr1":4, "sus":12, "dr2":0, "rr":6,
                 "vol":1.0, "pan":-0.3, "detune":0 },
      "tone2": { ... }, "tone3": { ... },
      "noise": { "atk":..., "rr":..., "vol":..., "pan":...,
                 "type":"white"|"periodic",
                 "rate":"low"|"mid"|"high"|"ch2" }
    }
  }
  ```
- **Loaders/writers** (`04-patch-system.md`):
  - `PsgPreset.{h,cpp}` — `loadPsgPreset(path)` returns a struct
    populated from the JSON; `savePsgPreset(path, preset)` writes the
    JSON. Out-of-range / missing fields clamp to defaults; unparseable
    files raise a notification toast and do not load.
  - **No** `DacPreset.{h,cpp}` — D has no preset format.
- **PatchSystem** extension: add `enum class Tag { FM, SQ };` and
  `tagFromExtension(juce::String ext)`. Folder-scan code starts
  classifying every found file by extension and bucketing it by tag.
  D is a `mode_select` value but never a `Tag` value.
- **Browser layout** (view 5):
  - Mode filter chip row at top — `All / FM / SQ`. Default = the
    instance's current mode (or `All` when the instance is in D mode,
    since D has no presets to filter to).
  - Search box filtering by patch name across roots, honouring the
    chip.
  - Left pane: folder tree by root. Factory carries a lock glyph,
    Saved / Imported / custom are writable. Each folder shows its
    patch count when scanned. Lazy scan on first expand.
  - Right pane: patch list. Each row shows a small `FM` / `SQ` badge
    + the name. Single-click loads.
  - `+ Add Folder…`, `Import file`, `Export▾`, `Delete`, `Close`
    buttons. `Export▾` is greyed when `mode_select == D` (no format).
- **D-mode header chrome** (view 1): when `mode_select == D` the
  patch-name LCD + ◀ / ▶ / 📂 buttons in the header are greyed
  (`.is-disabled`) and non-interactive. This task implements the
  greying — Task 08 left the cluster wired up for FM/SQ behaviour;
  this task adds the D-mode `.is-disabled` toggle keyed off
  `mode_select`. The LCD shows the static placeholder `AUDIO FX`
  (or `—`) when in D mode.
- **Load behaviour** (ADR-0025 *Preview behaviour*):
  - Single-click on a patch loads it immediately.
  - **If the patch's tag differs from the current mode, the instance's
    mode auto-switches** — no confirmation modal; the previous patch
    is left untouched on disk. (Auto-switch can only ever land on FM
    or SQ — there's no D preset to switch *to*. A load that lands the
    instance in FM/SQ from a D state silently leaves the D apvts
    values untouched on the way out; coming back to D later via the
    mode pill resumes those values.)
  - The browser stays open for auditioning multiple patches in turn;
    Close dismisses.
- **Drag-and-drop** (`08-ui-views.md` view 10): a native
  `juce::FileDragAndDropTarget` on the editor handles drops because an
  HTML5 drop inside the WebView yields `File` objects only, not real
  paths. The editor forwards the resolved paths to the patch system.
  - Dropping a supported file → import to the user-imported root.
  - Dropping a `.vgm` / `.vgz` → run Import Bank on that file.
  - Dropping a folder → import every supported file recursively into
    the user-imported root. (Earlier v1 behaviour was "register the
    folder as a custom root"; ADR-0025 + the v2 spec say "import every
    supported file"; the user manually registers a custom root via
    *Add Folder…* in the browser.)
- **Manual mode switch behaviour** (ADR-0021):
  - **FM / SQ** — the instance silently loads the default preset for
    that mode. Default table:
    - FM → `extern/patches/bass.tfi` (or the first sorted factory FM).
    - SQ → `extern/patches/sq/default.psg` (seeded in this task).
  - **D** — the instance does **nothing** to its D apvts params. The
    host owns them. First entry into D in a fresh instance starts from
    the `juce::AudioParameter` defaults naturally; subsequent entries
    preserve whatever the host last set.
- **Header ◀ / ▶ buttons** become live in this task — they navigate
  prev / next within the active mode's sorted preset list across all
  roots. Greyed and non-interactive in D mode (see header-chrome rule
  above).
- **Factory preset folder** — seed `extern/patches/sq/` with a small
  starter set (e.g. `default.psg`, `pulse-arp.psg`, `soft-lead.psg`).
  No D-mode factory folder exists (no `.gdac` format). CMake's
  factory-patch staging block (Task 02 retained it) recursively
  enumerates the FM top-level `.tfi` files *plus* the entire
  `extern/patches/sq/` subtree. **Do not** pull in the gitignored
  `extern/patches/extra/` dev-only set (`04-patch-system.md` *Build
  requirement*).

## Scope

- C++:
  - `src/PatchSystem.{h,cpp}` — `Tag` enum (`{ FM, SQ }`),
    `tagFromExtension`, `kSupportedPatchExtensions`, extended
    folder-scan that buckets by tag.
  - New `src/PsgPreset.{h,cpp}` + `tests/PsgPresetTests.cpp`.
  - `PluginProcessor`:
    - Loading a `.psg` writes the preset's fields into the SQ apvts
      params.
    - The patch-delivery queue path gains a tagged variant: the
      message thread parses the file into the matching struct, pushes
      a tagged item into the queue, processBlock drains and applies.
    - On load, if the patch's tag differs from the current mode, the
      message thread flips `mode_select` via the apvts *before*
      enqueueing the apply. (Tags are only `FM` or `SQ`, so the
      auto-switch destination is never D.)
    - On manual mode switch, the destination-specific behaviour above
      (FM/SQ load default preset; D leaves apvts alone).
  - `PluginEditor` native functions:
    - `getPatchList()` — returns the full list, each entry with
      `{ path, name, tag, root }`. Tags are `FM` or `SQ` only.
    - `loadPatch(path)` — message-thread parse + dispatch per above.
    - `savePatch()` — writes the current mode's preset to the
      user-saved root with the right extension (`.tfi` for FM, `.psg`
      for SQ). Returns an error result if called in D mode (no format
      to write); the UI button is greyed in D so the call should not
      arise.
    - `importPatch()` — file picker filtered to
      `kSupportedPatchExtensions`; copies into user-imported root.
    - `exportPatch(format)` — file picker for export. `format` =
      `"tfi"`/`"vgi"` in FM mode, `"psg"` in SQ mode. Disabled in D
      mode (UI button greyed).
    - `addPatchRoot()` — native directory chooser; the chosen folder
      becomes a custom root.
    - `deletePatch(path)` — removes from a writable root; refused
      (toast) for the factory root.
    - `patchNav(direction)` — implements the real navigation now
      (replaces the Task 08 stub). No-op in D mode (no presets).
  - `juce::FileDragAndDropTarget` on the editor: on drop, dispatch
    per the rules in *Context*. A drop while in D mode that resolves
    to an FM or SQ tag still imports + auto-switches.
- UI (`ui/src/modals/preset-browser.js`):
  - Build the view-5 layout. Mode filter chips (`All / FM / SQ`),
    search input, folder tree (left), patch list (right), the action
    button row at the bottom.
  - On open, default the chip row to the instance's current mode (or
    `All` if in D mode).
  - Single-click on a patch row calls `loadPatch(path)`; the browser
    stays open.
  - Wire each action button to its native function. The `Export▾`
    button greys when `mode_select == D`.
- Header changes:
  - 📂 button now opens the preset browser modal. Greyed in D mode
    (see *D-mode header chrome* constraint).
  - ◀ / ▶ buttons call `patchNav('prev'|'next')`. Greyed in D mode.
  - The patch-name LCD updates whenever a patch loads (subscribes to
    a `patchLoaded` event the C++ pushes on every successful load).
    In D mode the LCD displays the static placeholder `AUDIO FX` and
    does not subscribe.
  - The entire patch cluster (`.hdr-patch` container) toggles
    `.is-disabled` based on `mode_select == D`.

## Out of scope

- State persistence of custom-root paths and the active patch path —
  Task 10 (the in-memory list is fine for this task; reloading the
  plugin loses the registrations until Task 10 lands).
- Cross-platform smoke testing of native file choosers and DnD —
  Task 10.
- Glob optimisation for very large custom-root trees beyond the lazy
  scan + background-index pattern v1 already shipped.
- Any D-mode preset machinery — `.gdac` format, `DacPreset` class,
  `extern/patches/d/` folder were all dropped in the ADR-0025 revision.
  D mode's state lives in apvts only and rides on the project save.

## Implementation steps

1. **`PatchSystem` extension** — add `Tag` enum (`{ FM, SQ }`),
   `tagFromExtension`, `kSupportedPatchExtensions`. Update the
   folder-scan to bucket by tag.
2. **`PsgPreset.{h,cpp}`** — load + save JSON parsers using
   `juce::JSON`. Define a `PsgPreset` struct mirroring the schema.
   Apply-to-apvts helper: given a `PsgPreset` and the apvts, write
   each field into the matching `psg_*[ch]` param via
   `setValueNotifyingHost`.
3. **`PluginProcessor` patch-delivery**:
   - The patch-load queue gains tag awareness. One way: a `struct
     PatchEnvelope { Tag tag; Patch fm; PsgPreset sq; }` in the
     queue; the audio thread reads `tag` and applies the matching
     variant. Another way: two separate queues, one per tag. Pick one
     (single typed queue with `Tag` discriminator is simpler at this
     scope).
   - On `loadPatch(path)`: parse on the message thread, pick the
     correct loader by `tagFromExtension`, flip `mode_select` to
     match the tag if different, push the envelope.
4. **Native functions** (`PluginEditor`): implement
   `getPatchList / loadPatch / savePatch / importPatch /
   exportPatch / addPatchRoot / deletePatch / patchNav`. Each runs on
   the message thread; results return synchronously where the JS side
   needs them. The save/export/nav functions guard against D mode
   (return early with an error result, even though the UI button is
   greyed).
5. **`juce::FileDragAndDropTarget`** on the editor: implement
   `isInterestedInFileDrag(files)` (accept anything with a supported
   extension or a folder), `filesDropped(files, x, y)` — dispatch
   per the *Context* rules.
6. **Preset browser modal** (`ui/src/modals/preset-browser.js`).
   Build the layout. The folder tree fetches contents lazily via a
   new `expandFolder(path)` native function that returns the
   children + per-child patch count. Wire everything to the native
   functions.
7. **Header wiring** — 📂 opens the browser; ◀ / ▶ call `patchNav`;
   the patch-name LCD subscribes to `patchLoaded`. Add the
   `.hdr-patch.is-disabled` class toggle keyed off `mode_select`; in
   D mode the LCD displays `AUDIO FX` instead of a patch name and
   none of the buttons respond.
8. **Default-preset on manual mode switch** — `PluginProcessor`
   subscribes to `mode_select` apvts changes:
   - If switching to **FM** or **SQ** and the new mode has no active
     patch, call the default-preset loader for that mode (FM →
     `bass.tfi` (or first sorted factory FM); SQ →
     `extern/patches/sq/default.psg`).
   - If switching to **D**, do nothing — the host owns the D apvts
     values per ADR-0021.
9. **Seed factory presets** — write the small starter set under
   `extern/patches/sq/` (`default.psg`, `pulse-arp.psg`,
   `soft-lead.psg`). Update CMake's factory-patch staging to include
   the `sq/` subtree recursively, excluding the gitignored
   `extern/patches/extra/` set.

## Deliverables

- C++ new: `src/PsgPreset.{h,cpp}`.
- C++ updated: `src/PatchSystem.{h,cpp}`, `src/PluginProcessor.{h,cpp}`,
  `src/PluginEditor.{h,cpp}`.
- UI new: `ui/src/modals/preset-browser.js`.
- UI updated: `ui/src/header.js` (📂 button + ◀ / ▶ wiring +
  D-mode `.is-disabled` toggle on `.hdr-patch`),
  `ui/src/widgets/patch-name-lcd.js` (subscribes to `patchLoaded` for
  FM/SQ; shows `AUDIO FX` placeholder in D mode).
- Tests: `tests/PsgPresetTests.cpp`.
- Factory preset seed: `extern/patches/sq/default.psg`,
  `extern/patches/sq/pulse-arp.psg`, `extern/patches/sq/soft-lead.psg`.
- `src/CMakeLists.txt` factory-patch staging extended to recursively
  include `sq/` (still excluding `extra/`).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green, including the new test file (round-trip a `.psg` through
   load / save; verify clamping on out-of-range values; verify
   unparseable files return the error result).
2. **Browser open / filter / load** (from FM mode):
   - Header 📂 opens the browser.
   - Filter chip defaults to the instance's current mode (FM).
   - Switch to `All`; all factory presets show with FM / SQ badges
     (no D entries).
   - Click an FM TFI → loads; the FM panel updates; the patch-name
     LCD shows the patch name.
   - Click a SQ `.psg` → mode flips to SQ; the SQ panel mounts; the
     panel populates with the preset's per-channel envelope /
     volume / pan values; the patch-name LCD shows the preset name.
3. **D-mode header chrome** — switch to D mode via the mode pill.
   The patch-name LCD displays `AUDIO FX` and is dimmed. The ◀ / ▶
   and 📂 buttons are visibly greyed and do not respond to clicks.
   Hovering shows no tooltip. The browser is **not** reachable from
   D mode header; switching back to FM/SQ via the mode pill restores
   the buttons.
4. **Search** — type "bass" in the search box (browser opened from
   FM/SQ); only patches matching "bass" appear in the right pane.
5. **Import** — `Import file` → pick a TFI / VGI / DMP / Y12 / OPM /
   PSG; it appears under `Imported`. The file picker filter does
   **not** include `.gdac`.
6. **Export** — load an FM patch, click `Export▾` → TFI; the saved
   file round-trips back through `Import file` to the same patch.
   Switch to D mode — `Export▾` is greyed (no format).
7. **Add Folder** — pick an external folder containing some `.tfi`;
   it appears in the tree as a custom root.
8. **Delete** — delete an Imported patch; it disappears. Try to
   delete a Factory patch — refused via a toast.
9. **DnD** — drag a `.tfi` onto the plugin window → imported. Drag a
   `.psg` onto the plugin window → imported. Drag a folder → every
   supported file inside is imported. Drag a `.vgm` → Import Bank
   runs. (Drag a `.gdac` — file type not recognised, no import; no
   error toast either, the drop is just ignored because the
   extension isn't in `kSupportedPatchExtensions`.)
10. **Manual mode switch behaviour** — start in FM; click `SQ` on the
    mode pill; the SQ panel mounts with the `default.psg` factory
    preset loaded. Flip to D; the D panel mounts with whatever
    apvts values were last in place (or the `juce::AudioParameter`
    defaults on first entry). Tweak `prescaler`; flip to FM; flip
    back to D — `prescaler` retains the tweaked value (host owns it).
11. **Header ◀ / ▶** — load any FM patch; press ▶ — the next FM
    patch in sorted order across all roots loads (no skip across
    modes when the filter is FM; with `All` the prev/next still
    stays within the active mode per the design).
12. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"`
    — passes.

## Done when

- [ ] `Tag` enum (`{ FM, SQ }`) + `tagFromExtension` live in
      PatchSystem.
- [ ] `.psg` load / save round-trip without loss; the test pins the
      schema.
- [ ] Browser modal opens / closes; filter chips work (`All / FM / SQ`);
      single-click loads with auto-mode-switch.
- [ ] Header ◀ / 📂 / ▶ all wired and functional in FM/SQ; greyed
      and non-interactive in D.
- [ ] Drag-and-drop handles files, folders, and `.vgm`/`.vgz`.
      `.gdac` is not recognised (the extension was never added to
      `kSupportedPatchExtensions`).
- [ ] Factory `sq/` seed set ships in the bundle; loading any of
      them works end-to-end.
- [ ] Manual mode switch loads the default preset for FM / SQ; D
      mode switch leaves apvts alone (host owns the values).
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
