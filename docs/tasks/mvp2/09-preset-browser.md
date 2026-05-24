# Task 09 — Tagged unified preset browser & .psg / .gdac formats

> **Milestone:** Tagged preset browser — one browser modal shows FM, SQ,
> and D presets across all roots; loading a preset auto-switches the
> instance mode if the preset's tag differs from the current mode.
> **Depends on:** Task 08.
> **Design references:** `docs/design/08-ui-views.md` view 6 (primary —
> the browser modal layout), `docs/design/04-patch-system.md` (Tag table,
> `.psg` schema, `.gdac` schema, browser design, drag-and-drop semantics),
> ADR-0025, ADR-0006.

## Objective

Replace the v1 patch browser modal with the v2 **tagged unified preset
browser**: one modal that shows every preset from every root (Factory /
Saved / Imported / custom) and every mode (FM / SQ / D), filtered by a
top-of-modal chip row. Loading a preset switches the instance to the
preset's mode if it differs from the current mode. Add the two new v2
preset formats — `.psg` (SQ) and `.gdac` (D) — alongside the FM patch
loaders.

After this task, the user can: open the browser from the header 📂
button; filter by mode; click a `.gdac` from the D-mode factory pool
and have the plugin flip to D mode and apply the preset; drag a `.psg`
file onto the plugin window and have it import into the user-imported
root; drop a folder onto the window and have it register as a custom
root.

## Context & key constraints

- **Tag derivation by extension** (ADR-0025, `04-patch-system.md`
  *Tagging*):

  | Extension | Tag |
  |---|---|
  | `.tfi`, `.vgi`, `.dmp`, `.y12`, `.opm`, `.vgm`, `.vgz` | FM |
  | `.psg` | SQ |
  | `.gdac` | D |

  `.wav` is **not** a recognised v2 tag. `tagFromExtension(ext)` and
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
- **`.gdac` JSON schema** (`04-patch-system.md` *.gdac Format*):
  ```json
  { "version":1, "name":"Crunchy Drums",
    "prescaler":0.65, "mono":false, "dry_wet":1.0 }
  ```
- **Loaders/writers** (`04-patch-system.md`):
  - `PsgPreset.{h,cpp}` — `loadPsgPreset(path)` returns a struct
    populated from the JSON; `savePsgPreset(path, preset)` writes the
    JSON. Out-of-range / missing fields clamp to defaults; unparseable
    files raise a notification toast and do not load.
  - `DacPreset.{h,cpp}` — same pattern for `.gdac`.
- **PatchSystem** extension: add `enum class Tag { FM, SQ, D };` and
  `tagFromExtension(juce::String ext)`. Folder-scan code starts
  classifying every found file by extension and bucketing it by tag.
- **Browser layout** (view 6):
  - Mode filter chip row at top — `All / FM / SQ / D`. Default = the
    instance's current mode.
  - Search box filtering by patch name across roots, honouring the
    chip.
  - Left pane: folder tree by root. Factory carries a lock glyph,
    Saved / Imported / custom are writable. Each folder shows its
    patch count when scanned. Lazy scan on first expand.
  - Right pane: patch list. Each row shows a small `FM` / `SQ` / `D`
    badge + the name. Single-click loads.
  - `+ Add Folder…`, `Import file`, `Export▾`, `Delete`, `Close`
    buttons.
- **Load behaviour** (ADR-0025 *Preview behaviour*):
  - Single-click on a patch loads it immediately.
  - **If the patch's tag differs from the current mode, the instance's
    mode auto-switches** — no confirmation modal; the previous patch
    is left untouched on disk.
  - The browser stays open for auditioning multiple patches in turn;
    Close dismisses.
- **Drag-and-drop** (`08-ui-views.md` view 11): a native
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
- **Default-preset on manual mode switch**: when the user flips the
  header mode selector to a different mode, the instance silently
  loads a sensible default preset for that mode (ADR-0021). Default
  table:
  - FM → `extern/patches/bass.tfi` (or the first sorted factory FM).
  - SQ → `extern/patches/sq/default.psg` (seeded in this task).
  - D → `extern/patches/d/default.gdac` (seeded in this task).
- **Header ◀ / ▶ buttons** become live in this task — they navigate
  prev / next within the active mode's sorted preset list across all
  roots.
- **Factory preset folders** — seed `extern/patches/sq/` with a small
  starter set (e.g. `default.psg`, `pulse-arp.psg`, `soft-lead.psg`)
  and `extern/patches/d/` with a starter set (`default.gdac`,
  `crunchy-drums.gdac`, `voice-sample.gdac`, `subtle-crush.gdac`).
  CMake's factory-patch staging block (Task 02 retained it) recursively
  enumerates the FM top-level `.tfi` files *plus* the entire
  `extern/patches/sq/` and `extern/patches/d/` subtrees. **Do not**
  pull in the gitignored `extern/patches/extra/` dev-only set
  (`04-patch-system.md` *Build requirement*).

## Scope

- C++:
  - `src/PatchSystem.{h,cpp}` — `Tag` enum, `tagFromExtension`,
    `kSupportedPatchExtensions`, extended folder-scan that buckets by
    tag.
  - New `src/PsgPreset.{h,cpp}` + `tests/PsgPresetTests.cpp`.
  - New `src/DacPreset.{h,cpp}` + `tests/DacPresetTests.cpp`.
  - `PluginProcessor`:
    - Loading a `.psg` writes the preset's fields into the SQ apvts
      params.
    - Loading a `.gdac` writes the preset's fields into the D apvts
      params.
    - The patch-delivery queue path gains a tagged variant: the
      message thread parses the file into the matching struct, pushes
      a tagged item into the queue, processBlock drains and applies.
    - On load, if the patch's tag differs from the current mode, the
      message thread flips `mode_select` via the apvts *before*
      enqueueing the apply.
  - `PluginEditor` native functions:
    - `getPatchList()` — returns the full list, each entry with
      `{ path, name, tag, root }`.
    - `loadPatch(path)` — message-thread parse + dispatch per above.
    - `savePatch()` — writes the current mode's preset to the
      user-saved root with the right extension (`.tfi` for FM,
      `.psg` for SQ, `.gdac` for D). FM default save format is TFI;
      `Export▾` lets the user pick VGI instead.
    - `importPatch()` — file picker filtered to
      `kSupportedPatchExtensions`; copies into user-imported root.
    - `exportPatch(format)` — file picker for export. `format` =
      `"tfi"`/`"vgi"` in FM mode, `"psg"` in SQ mode, `"gdac"` in D mode.
    - `addPatchRoot()` — native directory chooser; the chosen folder
      becomes a custom root.
    - `deletePatch(path)` — removes from a writable root; refused
      (toast) for the factory root.
    - `patchNav(direction)` — implements the real navigation now
      (replaces the Task 08 stub).
  - `juce::FileDragAndDropTarget` on the editor: on drop, dispatch
    per the rules in *Context*.
- UI (`ui/src/modals/preset-browser.js`):
  - Build the view-6 layout. Mode filter chips, search input, folder
    tree (left), patch list (right), the action button row at the
    bottom.
  - On open, default the chip row to the instance's current mode.
  - Single-click on a patch row calls `loadPatch(path)`; the browser
    stays open.
  - Wire each action button to its native function.
- Header changes:
  - 📂 button now opens the preset browser modal.
  - ◀ / ▶ buttons call `patchNav('prev'|'next')`.
  - The patch-name LCD updates whenever a patch loads (subscribes to
    a `patchLoaded` event the C++ pushes on every successful load).

## Out of scope

- State persistence of custom-root paths and the active patch path —
  Task 10 (the in-memory list is fine for this task; reloading the
  plugin loses the registrations until Task 10 lands).
- Cross-platform smoke testing of native file choosers and DnD —
  Task 10.
- Glob optimisation for very large custom-root trees beyond the lazy
  scan + background-index pattern v1 already shipped.

## Implementation steps

1. **`PatchSystem` extension** — add `Tag` enum, `tagFromExtension`,
   `kSupportedPatchExtensions`. Update the folder-scan to bucket by
   tag.
2. **`PsgPreset.{h,cpp}`** — load + save JSON parsers using
   `juce::JSON`. Define a `PsgPreset` struct mirroring the schema.
   Apply-to-apvts helper: given a `PsgPreset` and the apvts, write
   each field into the matching `psg_*[ch]` param via
   `setValueNotifyingHost`.
3. **`DacPreset.{h,cpp}`** — same pattern, simpler struct.
4. **`PluginProcessor` patch-delivery**:
   - The patch-load queue gains tag awareness. One way: a `struct
     PatchEnvelope { Tag tag; Patch fm; PsgPreset sq; DacPreset d; }`
     in the queue; the audio thread reads `tag` and applies the
     matching variant. Another way: three separate queues, one per
     tag. Pick one (single typed queue with `Tag` discriminator is
     simpler at this scope).
   - On `loadPatch(path)`: parse on the message thread, pick the
     correct loader by `tagFromExtension`, flip `mode_select` to
     match the tag if different, push the envelope.
5. **Native functions** (`PluginEditor`): implement
   `getPatchList / loadPatch / savePatch / importPatch /
   exportPatch / addPatchRoot / deletePatch / patchNav`. Each runs on
   the message thread; results return synchronously where the JS side
   needs them.
6. **`juce::FileDragAndDropTarget`** on the editor: implement
   `isInterestedInFileDrag(files)` (accept anything with a supported
   extension or a folder), `filesDropped(files, x, y)` — dispatch
   per the *Context* rules.
7. **Preset browser modal** (`ui/src/modals/preset-browser.js`).
   Build the layout. The folder tree fetches contents lazily via a
   new `expandFolder(path)` native function that returns the
   children + per-child patch count. Wire everything to the native
   functions.
8. **Header wiring** — 📂 opens the browser; ◀ / ▶ call `patchNav`;
   the patch-name LCD subscribes to `patchLoaded`.
9. **Default-preset on manual mode switch** — `PluginProcessor`
   subscribes to `mode_select` apvts changes. If the change is a
   manual flip *and* the new mode has no active patch, call the
   default-preset loader (parameterised by the default table above).
10. **Seed factory presets** — write the small starter sets under
    `extern/patches/sq/` and `extern/patches/d/`. Update CMake's
    factory-patch staging to include both subtrees (recursively),
    excluding the gitignored `extern/patches/extra/` set.

## Deliverables

- C++ new: `src/PsgPreset.{h,cpp}`, `src/DacPreset.{h,cpp}`.
- C++ updated: `src/PatchSystem.{h,cpp}`, `src/PluginProcessor.{h,cpp}`,
  `src/PluginEditor.{h,cpp}`.
- UI new: `ui/src/modals/preset-browser.js`.
- UI updated: `ui/src/header.js` (📂 button + ◀ / ▶ wiring),
  `ui/src/widgets/patch-name-lcd.js` (subscribes to `patchLoaded`).
- Tests: `tests/PsgPresetTests.cpp`, `tests/DacPresetTests.cpp`.
- Factory preset seed: `extern/patches/sq/default.psg`,
  `extern/patches/sq/pulse-arp.psg`, `extern/patches/sq/soft-lead.psg`;
  `extern/patches/d/default.gdac`, `extern/patches/d/crunchy-drums.gdac`,
  `extern/patches/d/voice-sample.gdac`, `extern/patches/d/subtle-crush.gdac`.
- `src/CMakeLists.txt` factory-patch staging extended to recursively
  include `sq/` + `d/` (still excluding `extra/`).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green, including the two new test files (round-trip a `.psg` and
   a `.gdac` through load / save; verify clamping on out-of-range
   values; verify unparseable files return the error result).
2. **Browser open / filter / load**:
   - Header 📂 opens the browser.
   - Filter chip defaults to the instance's current mode.
   - Switch to `All`; all factory presets show with FM / SQ / D
     badges.
   - Click an FM TFI → loads; the FM panel updates; the patch-name
     LCD shows the patch name.
   - Click a SQ `.psg` → mode flips to SQ; the SQ panel mounts; the
     panel populates with the preset's per-channel envelope /
     volume / pan values; the patch-name LCD shows the preset name.
   - Click a D `.gdac` → mode flips to D; the D panel mounts; the
     PRESCALER / MONO / DRY/WET reflect the preset.
3. **Search** — type "bass" in the search box; only patches matching
   "bass" appear in the right pane.
4. **Import** — `Import file` → pick a TFI / VGI / DMP / Y12 / OPM /
   PSG / GDAC; it appears under `Imported`.
5. **Export** — load an FM patch, click `Export▾` → TFI; the saved
   file round-trips back through `Import file` to the same patch.
6. **Add Folder** — pick an external folder containing some `.tfi`;
   it appears in the tree as a custom root.
7. **Delete** — delete an Imported patch; it disappears. Try to
   delete a Factory patch — refused via a toast.
8. **DnD** — drag a `.tfi` onto the plugin window → imported. Drag a
   `.psg` onto the plugin window → imported. Drag a folder → every
   supported file inside is imported. Drag a `.vgm` → Import Bank
   runs.
9. **Default-preset on manual mode switch** — start in FM; click
   `SQ` on the mode pill; the SQ panel mounts with the `default.psg`
   factory preset loaded; flip to D; D panel mounts with
   `default.gdac`. The previous mode's patch on disk is untouched.
10. **Header ◀ / ▶** — load any FM patch; press ▶ — the next FM
    patch in sorted order across all roots loads (no skip across
    modes when the filter is FM; with `All` the prev/next still
    stays within the active mode per the design).
11. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"`
    — passes.

## Done when

- [ ] `Tag` enum + `tagFromExtension` live in PatchSystem.
- [ ] `.psg` and `.gdac` load / save round-trip without loss; the
      tests pin the schema.
- [ ] Browser modal opens / closes; filter chips work; single-click
      loads with auto-mode-switch.
- [ ] Header ◀ / 📂 / ▶ all wired and functional.
- [ ] Drag-and-drop handles files, folders, and `.vgm`/`.vgz`.
- [ ] Factory `sq/` + `d/` seed sets ship in the bundle; loading any
      of them works end-to-end.
- [ ] Manual mode switch loads the default preset for the new mode.
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
