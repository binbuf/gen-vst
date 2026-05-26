# Task 02 — Subfolder taxonomy + CMake staging + browser verification

> **Milestone:** Patch tree is navigable — factory patches are organised
> by category in the browser, the bundle layout reflects the same
> structure, and the existing scan/load paths handle the deeper tree
> without regression.
> **Depends on:** `mvp2/09` (unified browser is live and scans the
> factory root).
> **Design references:** `docs/design/04-patch-system.md`
> (*Patch Browser Design*, *Factory bank*, *CMake sketch*),
> `src/CMakeLists.txt` (current factory-patch staging block),
> `src/PatchBrowser.cpp` (`initialize()`, `scanImmediateChildren`,
> `factoryPatchByIndex`).

## Objective

Move the factory patch tree from a flat layout into a category-based
subfolder layout so users can find sounds by purpose, not by scanning
~50 files of generic timbre names. The browser already supports
nested folders (`PatchFolder::subfolders` in
`src/PatchBrowser.{h,cpp}`); what's missing is (a) the actual
directory structure on disk, (b) the CMake globs to stage the new
layout into the plugin bundle, and (c) verification that the existing
`factoryPatchByIndex` / Program Change pool keeps working when the
files no longer sit at the top level.

After this task:

- `extern/patches/` has FM patches in `fm/{bass,lead,keys,brass,pad,drums,fx}/`
  and SQ patches in `sq/{lead,bass,perc,fx}/`.
- The CMake factory-patch glob walks the new tree recursively (still
  excluding the gitignored `extra/`).
- The plugin bundle's `Contents/Resources/patches/` mirrors the new
  structure on macOS, Windows, and Linux.
- The patch browser shows the category folders as collapsible tree
  nodes under the Factory root.
- `factoryPatchByIndex` enumerates the full category-recursed factory
  pool, not just top-level files — keeping the Program Change source-
  of-truth complete after the move.

## Context & key constraints

### What the browser already supports

`src/PatchBrowser.cpp` `initialize()` already eagerly scans the
factory root's immediate children **and** every subfolder under it
(see the loop near `scanImmediateChildren` at the factory root and
its `subfolders`). The browser's folder-tree UI in `09-preset-browser`
also already handles nested `PatchFolder` nodes — `mvp2/09` shipped
the tree code generically. What's new in this task is just that there
will be actual subfolders to navigate.

### What does *not* yet handle recursion

Two enumeration paths in `PatchBrowser::initialize()` use
**non-recursive** `fs::directory_iterator`:

1. **Factory subfolder eager scan**: scans each immediate child folder
   of the factory root. If we add a deeper level (e.g.,
   `fm/bass/distortion/`), the scan won't see it. We *won't* add a
   third level in this task — the taxonomy is exactly two levels
   (`fm/<category>/`, `sq/<category>/`) — so the existing two-level
   eager scan covers it. Document this in the task constraints so a
   later contributor doesn't accidentally add a third level and break
   the eager-scan contract.

2. **`factoryPatches` enumeration for Program Change** (lines ~117–137
   of `PatchBrowser.cpp`): enumerates the *immediate* top-level files
   in the factory root only. After this task, the factory FM patches
   are no longer at the top level — they're under
   `fm/<category>/`. **This enumeration must switch to a recursive
   walk** scoped to the factory root (excluding any nested folders
   that don't carry FM patches — i.e., skip `sq/` for the FM Program
   Change pool, or, depending on the mvp2/11 backlog wiring, build
   per-mode pools).

   The exact PC semantics ("Program Change selects the Nth patch of
   the active mode") are deferred under the `mvp2/11` post-MVP entry,
   so the minimum this task owes is: **`factoryPatchByIndex` returns
   every factory FM patch, not just top-level ones, with a stable
   sort order** (recommended: depth-first sort by relative path —
   `fm/bass/01.tfi` < `fm/bass/02.tfi` < `fm/lead/01.tfi` …).

### CMake staging — the recursive-with-exclusion problem

`src/CMakeLists.txt` currently uses non-recursive `file(GLOB)` for the
FM extensions (lines ~104–109) *and* `file(GLOB_RECURSE)` for the SQ
`.psg` tree. The comment explicitly warns that a recursive walk
across `extern/patches/*` would pull in the gitignored `extra/` set
(ADR-0004).

After this task:

- FM patches live under `extern/patches/fm/` recursively.
- SQ patches live under `extern/patches/sq/` recursively.
- `extra/` continues to live at `extern/patches/extra/` (gitignored,
  not committed) — its location is **unchanged**, so a recursive
  walk *scoped to `extern/patches/fm/` and `extern/patches/sq/`*
  never sees it. The exclusion is structural; no `LIST_DIRECTORIES`
  or path filter is needed.
- `extern/patches/` no longer holds any `.tfi` / `.vgi` / `.y12` files
  at the top level. The top-level holds only `fm/`, `sq/`, and the
  gitignored `extra/`.

The bundle-staging POST_BUILD copy (the Windows/Linux fallback for
`juce_add_bundle_resources_directory` not working off macOS) uses
`copy_directory FACTORY_STAGE → Resources/patches/`. Since
`FACTORY_STAGE` is built up from the globs, the bundle will mirror
the new layout automatically once the globs are updated; no separate
change is needed.

### Tests that touch the factory layout

Verify before claiming done:

- `tests/PatchLoaderTests.cpp` — loads files via
  `GENVST_FACTORY_PATCHES_DIR` (defined in `tests/CMakeLists.txt` as
  `${CMAKE_SOURCE_DIR}/extern/patches`). Any test that hard-codes a
  filename (e.g., loads `bass.tfi` by name) will now need to load
  `fm/bass/bass.tfi` (or whatever the new path is) instead. Audit
  with a grep for `.tfi"` / `.vgi"` / `.y12"` string literals in
  `tests/`.
- `tests/PatchBrowserTests.cpp` — uses
  `GENVST_FACTORY_PATCHES_DIR` for scanning. Tests that count
  top-level factory files (e.g., `numFactoryPatches()` returning a
  specific number) need the recursive-pool change to match the new
  count semantics.
- `tests/PsgPresetTests.cpp` — loads from
  `${GENVST_FACTORY_PATCHES_DIR}/sq`; if SQ files move into
  `sq/<category>/`, any hard-coded `sq/<name>.psg` path needs the
  category injected.

## Scope

### Disk layout

Create these folders under `extern/patches/` and move the existing
files into them. Categorisation is by *primary musical purpose*; a
patch that could fit two categories goes in the one the name
suggests most strongly.

**FM tree** (`extern/patches/fm/`):

| Folder | Contents (existing files to move) |
|--------|----------------------------------|
| `bass/` | `bass.tfi`, `distbass.tfi`, `distslap.tfi`, `elecbass.tfi`, `slapbass.tfi`, `wobble_bass.vgi` |
| `lead/` | `flute.tfi`, `lfo_lead.y12`, `ocarina.tfi`, `sawtooth.tfi`, `sax.tfi`, `softsaw.tfi`, `softsqr.tfi`, `square.tfi`, `triangle.tfi`, `trumpet.tfi`, `vibrato_lead.vgi`, `sine.tfi`, `neslike.tfi` |
| `keys/` | `harp.tfi`, `harpsich.tfi`, `lyre.tfi`, `marimba.tfi`, `piano.tfi`, `sofpiano.tfi`, `tackpian.tfi`, `toypiano.tfi`, `banjo.tfi`, `guitar.tfi`, `distguit.tfi` |
| `brass/` | `fifths.tfi`, `shimmer_brass.vgi` |
| `pad/` | `chorus_bell.vgi`, `gentle_pluck.vgi`, `lfo_pad.y12`, `organ.tfi`, `tremolo_pad.vgi` |
| `drums/` | `kick.tfi`, `snare.tfi`, `cymbal.tfi`, `hithat.tfi`, `wooddrum.tfi`, `timpani1.tfi`, `timpani2.tfi`, `steldrum.tfi` |
| `fx/` | `bell.tfi`, `synbell.tfi` |

> **Note on category fit.** Some of the moves are debatable — `banjo.tfi`
> and `guitar.tfi` are arguably their own category, but a 7-folder tree
> is enough granularity for ~50 files; deeper splits add navigation
> cost without saving search effort. Stick to the seven FM categories
> listed unless a Task 03 / Task 04 audit changes the count.

**SQ tree** (`extern/patches/sq/`):

| Folder | Contents (existing files to move) |
|--------|----------------------------------|
| `lead/` | `default.psg`, `soft-lead.psg`, `bright-pluck.psg`, `retro-beep.psg`, `chip-melody.psg`, `title-screen.psg`, `pulse-arp.psg` |
| `bass/` | `square-bass.psg`, `periodic-bass.psg` |
| `perc/` | `noise-snare.psg`, `noise-hats.psg` |
| `fx/` | `detuned-chord.psg` |

> **If `01-sq-preset-retune.md` has not yet run**, do the move first
> and `01` re-tunes the files in place at their new locations.

### Code changes

- **`src/CMakeLists.txt`** — `FACTORY_FM` glob switches from
  non-recursive `extern/patches/*.{tfi,vgi,y12,dmp,opm}` to recursive
  under `extern/patches/fm/`. The SQ glob stays as
  `file(GLOB_RECURSE … extern/patches/sq/*.psg)`. The
  `FACTORY_STAGE` build copies preserve relative paths under
  `fm/<category>/` and `sq/<category>/`.

  Worked example:

  ```cmake
  file(GLOB_RECURSE FACTORY_FM
      "${CMAKE_SOURCE_DIR}/extern/patches/fm/*.tfi"
      "${CMAKE_SOURCE_DIR}/extern/patches/fm/*.vgi"
      "${CMAKE_SOURCE_DIR}/extern/patches/fm/*.y12"
      "${CMAKE_SOURCE_DIR}/extern/patches/fm/*.dmp"
      "${CMAKE_SOURCE_DIR}/extern/patches/fm/*.opm")
  file(GLOB_RECURSE FACTORY_SQ_PSG
      "${CMAKE_SOURCE_DIR}/extern/patches/sq/*.psg")

  # Preserve relative paths under fm/ and sq/ in the stage dir.
  # file(COPY ... DESTINATION ...) is non-preserving; switch to a
  # per-file loop that computes the relative path and copies it into
  # FACTORY_STAGE/<relative>.
  ```

  `file(COPY)` does **not** preserve directory structure when the
  source is a list of files; it flattens into the destination. Use a
  per-file loop that mirrors the relative path:

  ```cmake
  foreach(src IN LISTS FACTORY_FM)
      file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}/extern/patches" "${src}")
      get_filename_component(rel_dir "${rel}" DIRECTORY)
      file(MAKE_DIRECTORY "${FACTORY_STAGE}/${rel_dir}")
      file(COPY "${src}" DESTINATION "${FACTORY_STAGE}/${rel_dir}")
  endforeach()
  # Same loop for FACTORY_SQ_PSG.
  ```

  Adjust the `install(FILES …)` block for Standalone the same way —
  honour the `fm/<cat>/` and `sq/<cat>/` substructure.

- **`src/PatchBrowser.cpp`** — `factoryPatches` enumeration (the
  block around lines 117–137) switches from
  `fs::directory_iterator` to `fs::recursive_directory_iterator` on
  the factory root. Skip files under `sq/` (those are not FM patches
  and don't belong in the FM Program Change pool — but the per-mode
  PC behaviour itself is deferred under `mvp2/11`; for now the
  enumeration just needs to surface every FM `.tfi/.vgi/.y12/.dmp/.opm`
  under the factory root in a stable sorted order). Stable sort key:
  relative path from the factory root.

  Worked example:

  ```cpp
  std::vector<fs::path> tfiFiles;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator (factoryFs, ec);
       it != fs::recursive_directory_iterator{};
       it.increment (ec))
  {
      if (! it->is_regular_file (ec)) continue;
      if (! isPatchExtension (it->path())) continue;
      // Skip the sq/ subtree — SQ presets don't belong in the FM
      // factory pool (.psg loaders return PsgPreset, not Patch).
      auto rel = fs::relative (it->path(), factoryFs);
      if (! rel.empty() && rel.begin()->string() == "sq") continue;
      tfiFiles.push_back (it->path());
  }
  std::sort (tfiFiles.begin(), tfiFiles.end(),
             [&] (const fs::path& a, const fs::path& b)
             {
                 return fs::relative (a, factoryFs)
                      < fs::relative (b, factoryFs);
             });
  ```

  The existing factory-root subfolder eager scan (the loop over
  `factory->folder->subfolders`) continues to work — it picks up the
  new `fm/` and `sq/` subfolders as immediate children, then scans
  each of *their* immediate children (the 7 FM categories and 4 SQ
  categories). The eager-scan recursion depth is exactly two, which
  matches the taxonomy.

### Test updates

- **`tests/PatchLoaderTests.cpp`** — audit for any string literals
  like `"bass.tfi"`, `"piano.tfi"`, etc. Update to the new path
  (`"fm/bass/bass.tfi"`, `"fm/keys/piano.tfi"`).
- **`tests/PatchBrowserTests.cpp`** — `numFactoryPatches()` test
  (line ~334 area) — the expected count is unchanged (we're moving
  files, not adding / removing), but the test's expectation that all
  patches come from the top-level needs the recursive-pool change to
  succeed. Also: any test that compares a patch's source path needs
  the new prefix.
- **`tests/PsgPresetTests.cpp`** — update `sqDir` (line ~184) and
  any hard-coded preset filenames to include their new `<category>/`
  prefix.

## Out of scope

- Adding any new patches. This task only **moves** existing files and
  updates the build / scan paths. Task 03 authors new FM patches into
  the new structure; Task 04 bundles additional community packs.
- Deeper subfolders (`fm/bass/distortion/`, etc.). The taxonomy is
  exactly two levels deep; deeper splits are a follow-up if the
  patch count grows enough to warrant them.
- Changes to the SQ preset character descriptions in
  `04-patch-system.md`. The 12-preset table there continues to refer
  to file basenames; the new subfolder prefix is implicit from the
  taxonomy table in this task.
- Renaming any patches. File basenames stay exactly as they are;
  only the parent folder changes.

## Implementation steps

1. **Create the directory tree.**
   - `mkdir extern/patches/fm/{bass,lead,keys,brass,pad,drums,fx}`
   - `mkdir extern/patches/sq/{lead,bass,perc,fx}`
2. **Move FM files** into the categories in the table above. Use
   `git mv` for each file so history follows the move:
   ```
   git mv extern/patches/bass.tfi extern/patches/fm/bass/bass.tfi
   git mv extern/patches/distbass.tfi extern/patches/fm/bass/distbass.tfi
   …
   ```
3. **Move SQ files** into their categories:
   ```
   git mv extern/patches/sq/default.psg extern/patches/sq/lead/default.psg
   git mv extern/patches/sq/square-bass.psg extern/patches/sq/bass/square-bass.psg
   …
   ```
4. **Update `src/CMakeLists.txt`** — replace the `FACTORY_FM` glob,
   replace the staging `file(COPY)` calls with the relative-path
   preserving loop (see *Code changes* worked example), update the
   `install(FILES …)` blocks for the Standalone target.
5. **Update `src/PatchBrowser.cpp`** — switch the `factoryPatches`
   enumeration to recursive with the `sq/` skip and the relative-path
   sort (see *Code changes* worked example).
6. **Audit tests** — grep `tests/` for the old basenames and any
   `extern/patches/` string literals. Update each to the new path.
   Cite specific files where any test would fail without the update.
7. **Build + test** — full `cmake --build` and `ctest`. Expect every
   test that loads or counts factory patches to pass with the new
   paths.
8. **Smoke-test in a DAW** — load the plugin, open the patch browser,
   expand the Factory root. Confirm the `fm/` and `sq/` subfolder
   nodes appear; confirm each category subfolder expands and shows
   its patches; confirm single-click load still works from any
   subfolder.
9. **Bundle inspection.** On the platform you can build, inspect the
   shipped bundle directory:
   - macOS: `<plugin>.vst3/Contents/Resources/patches/` mirrors the
     `fm/<cat>/` and `sq/<cat>/` tree.
   - Windows: `<bundle path>/Resources/patches/` mirrors the same.
   - Linux: same as Windows.
10. **Commit as one structural change.** Separating the
    `git mv`s from the CMake / browser / test changes makes review
    easier but doubles the risk of an intermediate state that doesn't
    build. Prefer a single commit unless your review process requires
    splitting them.

## Deliverables

- `extern/patches/fm/<7 categories>/<files>` — every existing FM
  patch moved into its category folder; top-level `extern/patches/`
  no longer holds any `.tfi/.vgi/.y12` files.
- `extern/patches/sq/<4 categories>/<files>` — every existing SQ
  preset moved into its category folder; top-level
  `extern/patches/sq/` no longer holds any `.psg` files.
- `src/CMakeLists.txt` — recursive `FACTORY_FM` glob, relative-path-
  preserving stage copy loop, updated `install(FILES …)` for
  Standalone.
- `src/PatchBrowser.cpp` — recursive `factoryPatches` enumeration
  with stable sort and `sq/` skip.
- Updated test files (any of `tests/PatchLoaderTests.cpp`,
  `tests/PatchBrowserTests.cpp`, `tests/PsgPresetTests.cpp` that hold
  hard-coded basenames or paths).

## Verification

1. **Repo state.** `git status` shows the moves as renames
   (`git mv` preserves history). Top-level `extern/patches/` lists
   only `fm/`, `sq/`, `extra/` (gitignored), and any project files
   like `.gitignore`.
2. **Build.** `cmake --build build/windows-debug` succeeds with no
   missing-file warnings.
3. **Tests.** `ctest --output-on-failure` is green. The
   `PatchBrowser` tests that exercise `numFactoryPatches()` and
   `factoryPatchByIndex` pass with the new counts/paths.
4. **Bundle inspection.** The plugin bundle's
   `Contents/Resources/patches/` mirrors the new directory tree
   (verify on at least one platform).
5. **DAW smoke test.** Load the plugin; expand the Factory root in
   the patch browser; confirm the 7 FM category folders and 4 SQ
   category folders appear; click into each and verify the expected
   files are present; load one patch from each category and confirm
   it plays.
6. **`extra/` not bundled.** `find <bundle>/Contents/Resources/patches
   -name "*.tfi" | wc -l` matches the count of committed FM patches;
   confirms the gitignored `extra/` set didn't sneak in via a too-loose
   glob.
7. **No top-level FM/SQ patches.** A `ls extern/patches/*.tfi 2>&1`
   returns "no such file" — top-level enumeration captures nothing,
   forcing the recursion to do the work.
8. **`pluginval --strictness-level 8` still passes** on the artefact.

## Done when

- [ ] All FM patches live under `extern/patches/fm/<category>/`.
- [ ] All SQ presets live under `extern/patches/sq/<category>/`.
- [ ] CMake stages the recursive tree into the bundle with the
      `fm/<cat>/` / `sq/<cat>/` structure preserved.
- [ ] `PatchBrowser::factoryPatches` is populated via recursive walk
      with `sq/` skipped and stable sort by relative path.
- [ ] Tests are green after the audit.
- [ ] The factory root expands in the patch browser into the
      documented category tree; loads work from every subfolder.
- [ ] `pluginval --strictness-level 8` still passes.
