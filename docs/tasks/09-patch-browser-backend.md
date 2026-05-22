# Task 09 — Patch browser backend

> **Depends on:** Task 05, Task 08.
> **Design references:** `docs/design/04-patch-system.md` (primary — *Patch
> Browser Design*, *Patch Library & Delivery*, *Runtime patch roots*, *Audio
> Thread Delivery*, *Scanning strategy*), `docs/design/05-ui-ux.md` (*Native
> functions*), `docs/design/01-architecture.md` (*Threading Model*),
> ADR-0004, ADR-0005, ADR-0006.

## Objective

Build the **C++ backend** for the folder-tree patch browser: patch roots,
runtime factory-root resolution, the lazy-scanned folder-tree model, the
background search index, the lock-free patch-delivery queue to the audio
thread, and the native functions the UI will call. No browser UI yet — this is
the engine the Task 14 UI sits on.

## Context & key constraints

- The browser is **folder-tree based, organised around patch roots** (ADR-0006):
  - **Factory root** — read-only, always present, auto-loaded every launch.
  - **User root** — `<userAppData>/GenVst/patches/`, writable.
  - **Custom roots** — any number of user-registered folders; paths persisted in
    plugin state, re-scanned next launch.
- **Runtime factory-root resolution** (ADR-0005): for VST3/AU the factory
  patches are in the bundle's `Contents/Resources/patches/`; for Standalone they
  are in the platform data directory (`GENVST_STANDALONE_PATCH_DIR`, set in
  CMake in Task 01). Resolve the correct location at runtime per format.
- **Scanning is lazy** (`04-patch-system.md` *Scanning strategy*): startup scans
  only each root's immediate children; a folder's contents are read only when
  its tree node is first expanded; a folder's patch count is filled in once
  scanned. A custom root can be a tree of **tens of thousands** of files — an
  eager scan would stall the UI.
- The **name-search index** (patch name → path) is built on a **background
  thread** after startup; search never triggers an upfront full scan.
- **File enumeration and parsing run on the message thread only**, never the
  audio thread.
- **Audio-thread delivery** (`04-patch-system.md` *Audio Thread Delivery*): the
  message thread loads the file into a `Patch`, tags it with the target part
  index, and pushes `(part, Patch)` into a `juce::AbstractFifo`-based lock-free
  SPSC queue (capacity 4). `processBlock` drains the queue at block start and
  applies each patch to that part. A load failure never reaches the audio
  thread.
- **Native functions** (`05-ui-ux.md` *Native functions*) are JUCE WebView
  native functions returning Promises. This task implements the **C++ side** and
  registers them on the `WebBrowserComponent`: `getPatchList`,
  `loadInstrument` / `loadPreset`, `savePatch` / `importPatch` / `exportPatch`,
  plus tree-navigation queries the UI needs (expand-folder, search). The UI that
  calls them is Task 14; the file-chooser bodies of import/export are Task 14.
- Loading an Instrument/Preset targets the **currently selected part**
  (ADR-0013).
- Program Change (Task 06) currently uses a minimal sorted factory enumeration —
  re-point it at the factory root provided here so there is one source of truth.

## Scope

- `PatchRoot` model + the three root kinds; runtime factory-root resolution per
  plugin format; user-root creation.
- The folder-tree data model with lazy scan (immediate children at startup;
  on-demand expansion; per-folder counts).
- The background search-index thread (name → path), started after startup.
- The lock-free `(part, Patch)` delivery queue; `processBlock` drains it.
- The native-function C++ implementations + registration on the WebView.
- Custom-root registration/unregistration (unregister removes from the list
  only — never deletes files).
- Re-point Task 06's Program Change at the factory root.

## Out of scope

- The patch browser **modal UI**, the file choosers, drag-and-drop → Task 14.
- The notification toast that surfaces load failures → Task 13.
- Persisting custom-root paths and the active patch path in plugin state →
  Task 16 (expose the data; Task 16 serializes it).

## Implementation steps

1. Implement `PatchRoot` and runtime factory-root resolution (bundle vs
   standalone data dir); create the user root if absent.
2. Implement the lazy folder-tree model: startup scan of immediate children;
   on-demand folder expansion; per-folder patch counts.
3. Implement the background search-index thread.
4. Implement the lock-free patch-delivery queue and drain it in `processBlock`;
   route delivered patches through `PartManager::loadPatch`.
5. Implement and register the native functions on the `WebBrowserComponent`.
6. Re-point Program Change at the factory root.

## Deliverables

`src/PatchBrowser.{h,cpp}` (or extend `PatchSystem.{h,cpp}`), updates to
`src/PluginProcessor.{h,cpp}`, `src/PluginEditor.{h,cpp}` (native-function
registration), `src/PartManager.{h,cpp}`.

## Verification

> The browser UI does not exist yet. Verify the backend via the native
> functions (call them from the JS console of the dev-server build, or a
> temporary debug button) and the dev load path.

1. Standalone and VST3 both **resolve the factory root** at runtime — calling
   `getPatchList` for the factory root returns the 39 factory patches.
2. Point a custom root at `extra/` (~30k files). Startup is
   **not** stalled — the immediate children appear quickly; a deep folder is
   only scanned (and its count filled) when expanded.
3. The background search index returns correct name→path hits for factory
   patch names without any full eager scan.
4. Invoking `loadInstrument` / `loadPreset` for a patch applies it to the
   currently selected part: the patch reaches the audio thread via the lock-free
   queue and the part's sound changes — with **no audio glitch or dropout**.
5. A failed load (point at a corrupt/wrong-size file) returns an error to the
   caller and **never** reaches the audio thread.
6. Program Change still works and now indexes the shared factory root.
7. `pluginval --strictness-level 8` passes — `processBlock` only drains the
   lock-free queue; no allocation/locks added to the audio thread.

## Done when

- [ ] Factory / user / custom roots modelled; factory root resolves at runtime
      for both Standalone and VST3.
- [ ] Lazy folder-tree scan handles a 30k-file custom root without UI stall.
- [ ] Background search index works without an eager full scan.
- [ ] Patches reach the audio thread only via the lock-free queue; no glitch.
- [ ] Native functions implemented and registered; Program Change uses the
      factory root.
- [ ] `pluginval` passes.
