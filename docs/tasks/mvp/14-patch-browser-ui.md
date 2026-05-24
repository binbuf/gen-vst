# Task 14 — Patch browser UI, import/export & drag-and-drop

> **Milestone:** Full UI — every view, section, and modal is built and wired.
> **Depends on:** Task 09, Task 13.
> **Design references:** `docs/design/08-ui-views.md` (primary — view 4 *Patch
> browser*, view 11 *Native file choosers*), `docs/design/04-patch-system.md`
> (*Patch Browser Design*, *Folders, Import & Export*), `docs/design/05-ui-ux.md`
> (*File drag-and-drop*), ADR-0006.

## Objective

Build the **patch browser modal** on top of the Task 09 backend, **split the
writable user root into `saved/` and `imported/` subroots**, wire the
main-window quick-access patch lists to the right subroots, and implement
**import / export / delete / add-folder** and native **drag-and-drop**. This
completes the UI.

## Context & key constraints

- The patch browser is a **full-window modal overlay** (`08-ui-views.md`
  view 4) — it covers the 960×560 window with the main UI dimmed behind it,
  using the shared modal framework from Task 13.
- Layout (view 4): a search box; a left **folder-tree** pane (every root and its
  subfolders as a collapsible tree, `Factory` carrying a lock glyph, each
  scanned folder showing its patch count, **lazy scan on first expand**); a
  right **patch list** pane (`.tfi`/`.vgi`/`.dmp` files in the selected folder);
  and a button row: `Import file`, `Export▾`, `Delete`, `+ Add Folder…`,
  `Preview`, `Close`.
- All tree/list/search data comes from the **Task 09 native functions** — the
  UI calls them; it does not scan the filesystem itself.
- **Behaviour:** single-click or `Enter` on a patch loads it into the
  **currently selected FM part** (ADR-0013); the modal stays open so several
  patches can be auditioned. `Preview` sends a middle-C note-on at fixed
  velocity for ~1 s into the active part. `Delete` is **disabled for the
  read-only Factory root**. A load failure raises a notification toast (Task 13)
  and never blocks.

### Subfolder split (writable user root)

Task 09 shipped a single writable **User** root at
`<userAppData>/GenVst/patches/`. This task splits it into two distinct roots,
per `04-patch-system.md` *Patch roots*:

- `<userAppData>/GenVst/patches/saved/` — the **user-saved** root, populated
  only by `savePatchAsTfi()`.
- `<userAppData>/GenVst/patches/imported/` — the **user-imported** root,
  populated only by `importPatchFile()` and drag-and-drop imports.

Concrete changes:

- Add `PatchRootKind::UserSaved` and `PatchRootKind::UserImported`; retire
  the old `User` value. The `kind` field in `rootsAsJson()` serialises as
  `"user-saved"` and `"user-imported"`.
- Both subfolders are auto-created on startup via idempotent
  `fs::create_directories` — no installer step required.
- `savePatchAsTfi()` writes into the `saved/` root; `importPatchFile()` and
  the drag-and-drop file path write into the `imported/` root.
- Pre-existing flat user-root patches (from older builds) are silently
  ignored by the main-window lists. They remain accessible by `+ Add Folder…`
  pointing at the legacy directory as a custom root — no migration code.

### Quick-access list bindings

The main window's three patch-list surfaces each pin to one of the roots:

- `INSTRUMENTS` (center column) → `kind: "factory"`.
- `PRESETS` tab (right column) → `kind: "user-saved"`. Empty on a fresh install.
- `IMPORT` tab (right column) → `kind: "user-imported"`. Empty on a fresh install.

The folder icon in the Presets/Import tab header opens the browser modal.
Choosing a folder in the modal does **not** repaint `INSTRUMENTS` — the
quick-access surfaces are pinned, not navigable. The modal is the place to
browse anything outside those three roots (including custom roots).

> **Bridge note.** Task 11 wired the main-window PRESETS list to the factory
> root as a placeholder. That binding is replaced by step (d) below; until
> this task ships, the runtime UI shows the placeholder behaviour — a known
> deviation from the now-updated `08-ui-views.md` view 4.

- **Native file choosers** (`08-ui-views.md` view 11) — `juce::FileChooser`,
  not WebView content: `Import file` (open `*.tfi;*.vgi;*.dmp` → copy into the
  user-imported root), `Export▾` (save a 42-byte TFI or 43-byte VGI),
  `+ Add Folder…` (choose a directory → register a custom root).
- **Drag-and-drop** uses a **native `juce::FileDragAndDropTarget` on the
  editor**, *not* HTML5 drop (`05-ui-ux.md` *File drag-and-drop*): an HTML5 drop
  yields only `File` objects, not real paths, and cannot enumerate a dropped
  folder. Dropped `.tfi`/`.vgi`/`.dmp` files import into the user-imported
  root; a dropped folder registers as a custom root. The editor resolves the
  OS paths and forwards them to the patch system.

## Scope

- (a) Create `<userAppData>/GenVst/patches/saved/` and `…/imported/` on
  startup via idempotent `fs::create_directories`.
- (b) Route `savePatchAsTfi()` into `…/saved/`; route `importPatchFile()` and
  the drag-and-drop file path into `…/imported/`.
- (c) Add `PatchRootKind::UserSaved` / `UserImported`, retire the old `User`
  value, and update `rootsAsJson()` to serialise the two new kinds.
- (d) Wire the main-window `INSTRUMENTS` list to `kind: "factory"`, the
  PRESETS tab to `kind: "user-saved"`, and the IMPORT tab to
  `kind: "user-imported"` (replacing the Task 11 placeholder binding).
- (e) Show the two writable roots as named top-level branches in the modal
  browser's folder tree (`Saved` and `Imported`), alongside `Factory` and any
  custom roots.
- The patch browser modal UI: search, folder-tree pane, patch-list pane, the
  button row, `Preview`, `Close`. Folder icon opens the modal.
- `Import file`, `Export▾` (TFI + VGI), `Delete`, `+ Add Folder…` via native
  `juce::FileChooser`.
- Native `juce::FileDragAndDropTarget` drag-and-drop for files and folders.

## Out of scope

- The patch-system backend itself (roots, scanning, index, native functions) —
  done in Task 09.
- The loaders/exporters — done in Tasks 04/08.
- Persisting custom-root paths in plugin state → Task 16.

## Implementation steps

1. Build the patch browser modal layout per view 4, using the shared modal
   framework.
2. Populate the folder tree and patch list from the Task 09 native functions;
   implement lazy expansion (call the backend on first expand) and per-folder
   counts.
3. Implement the search box over the background index; show each hit's folder
   path.
4. Implement patch load on click/`Enter` (into the selected part), `Preview`,
   and `Close`.
5. Wire the main-window quick-access lists and the folder icon → modal.
6. Implement `Import file` / `Export▾` / `+ Add Folder…` via native
   `juce::FileChooser`; disable `Delete` for the Factory root.
7. Implement the native `juce::FileDragAndDropTarget` for file and folder drops.

## Deliverables

`ui/src/modals/patch-browser.*`, updates to `ui/src/views/fm-view.*` (the
quick-access lists + folder icon), updates to `src/PluginEditor.{h,cpp}` (the
file choosers and `FileDragAndDropTarget`), and any Task 09 native function
extensions the UI needs.

## Verification

1. Open the browser from the folder icon — it covers the window, the main UI is
   dimmed.
2. The folder tree shows Factory (with a lock glyph), Saved, Imported, and any
   custom roots; expanding a folder lazily scans it and fills in its patch
   count. Point a custom root at `extra/` and expand into it — no UI stall on
   the 30k-file tree.
3. Selecting a folder lists its patches; single-click / `Enter` loads a patch
   into the currently selected part — the sound changes, the modal stays open.
4. `Preview` plays a ~1 s middle-C on the active part.
5. Search by patch name returns hits across roots, each showing its folder path.
6. On a fresh user-data directory: `…/saved/` and `…/imported/` are created
   on first launch; the main-window PRESETS and IMPORT tabs both render
   empty.
7. `Import file` copies a `.tfi`/`.vgi`/`.dmp` into `…/imported/` — it appears
   in the IMPORT tab and as a child of the `Imported` tree node.
   `savePatch` from the FM editor writes a TFI into `…/saved/` — it appears in
   the PRESETS tab and as a child of the `Saved` tree node.
   `Export▾` writes a valid TFI and a valid VGI to a user-chosen location
   (re-import them to confirm). `Delete` removes a patch from a writable root
   and is **disabled** for Factory.
8. `+ Add Folder…` registers a custom root that appears in the tree.
9. Drag a `.tfi` file onto the plugin window → it imports into `…/imported/`
   (appears in the IMPORT tab); drag a folder onto the window → it registers
   as a custom root.
10. A failed load surfaces a notification toast and does not block the browser.
11. `pluginval --strictness-level 8` passes.

## Done when

- [ ] `…/patches/saved/` and `…/patches/imported/` are auto-created on
      startup; `savePatch` routes to the former, `importPatch` and
      drag-and-drop to the latter.
- [ ] `rootsAsJson()` emits the two new kinds (`"user-saved"`,
      `"user-imported"`); the old `"user"` kind is gone.
- [ ] The patch browser modal works: tree (Factory / Saved / Imported / custom),
      list, search, preview, lazy scan.
- [ ] Patches load into the selected part; the modal stays open.
- [ ] Quick-access lists are wired: INSTRUMENTS → factory, PRESETS → user-saved,
      IMPORT → user-imported; the folder icon opens the modal.
- [ ] Import / Export (TFI + VGI) / Delete / Add Folder work via native choosers;
      Delete is disabled for Factory.
- [ ] Native file/folder drag-and-drop works.
- [ ] `pluginval` passes.
