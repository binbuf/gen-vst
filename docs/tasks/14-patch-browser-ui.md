# Task 14 — Patch browser UI, import/export & drag-and-drop

> **Milestone:** Full UI — every view, section, and modal is built and wired.
> **Depends on:** Task 09, Task 13.
> **Design references:** `docs/design/08-ui-views.md` (primary — view 4 *Patch
> browser*, view 11 *Native file choosers*), `docs/design/04-patch-system.md`
> (*Patch Browser Design*, *Folders, Import & Export*), `docs/design/05-ui-ux.md`
> (*File drag-and-drop*), ADR-0006.

## Objective

Build the **patch browser modal** on top of the Task 09 backend, wire the
main-window quick-access patch lists, and implement **import / export / delete /
add-folder** and native **drag-and-drop**. This completes the UI.

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
- **Quick-access lists:** the main window's center `INSTRUMENTS` list and right
  `PRESETS`/`IMPORT` lists are quick-access views over the same backend —
  `INSTRUMENTS` = the active folder's patches, `PRESETS` = the factory bank,
  `IMPORT` = the User root. The folder icon in the Presets/Import tab header
  opens the browser modal. Choosing a folder in the browser updates what
  `INSTRUMENTS` shows.
- **Native file choosers** (`08-ui-views.md` view 11) — `juce::FileChooser`,
  not WebView content: `Import file` (open `*.tfi;*.vgi;*.dmp` → copy into the
  User root), `Export▾` (save a 42-byte TFI or 43-byte VGI), `+ Add Folder…`
  (choose a directory → register a custom root).
- **Drag-and-drop** uses a **native `juce::FileDragAndDropTarget` on the
  editor**, *not* HTML5 drop (`05-ui-ux.md` *File drag-and-drop*): an HTML5 drop
  yields only `File` objects, not real paths, and cannot enumerate a dropped
  folder. Dropped `.tfi`/`.vgi`/`.dmp` files import into the User root; a
  dropped folder registers as a custom root. The editor resolves the OS paths
  and forwards them to the patch system.

## Scope

- The patch browser modal UI: search, folder-tree pane, patch-list pane, the
  button row, `Preview`, `Close`.
- Wiring the main-window `INSTRUMENTS` / `PRESETS` / `IMPORT` quick-access lists
  to the backend; the folder icon opens the modal.
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
2. The folder tree shows Factory (with a lock glyph), User, and any custom
   roots; expanding a folder lazily scans it and fills in its patch count.
   Point a custom root at `extern/patches/extra/` and expand into it — no UI
   stall on the 30k-file tree.
3. Selecting a folder lists its patches; single-click / `Enter` loads a patch
   into the currently selected part — the sound changes, the modal stays open.
4. `Preview` plays a ~1 s middle-C on the active part.
5. Search by patch name returns hits across roots, each showing its folder path.
6. `Import file` copies a `.tfi`/`.vgi`/`.dmp` into the User root and it appears
   there. `Export▾` writes a valid TFI and a valid VGI (re-import them to
   confirm). `Delete` removes a patch from a writable root and is **disabled**
   for Factory.
7. `+ Add Folder…` registers a custom root that appears in the tree.
8. Drag a `.tfi` file onto the plugin window → it imports into the User root;
   drag a folder onto the window → it registers as a custom root.
9. A failed load surfaces a notification toast and does not block the browser.
10. `pluginval --strictness-level 8` passes.

## Done when

- [ ] The patch browser modal works: tree, list, search, preview, lazy scan.
- [ ] Patches load into the selected part; the modal stays open.
- [ ] Quick-access INSTRUMENTS/PRESETS/IMPORT lists are wired; the folder icon
      opens the modal.
- [ ] Import / Export (TFI + VGI) / Delete / Add Folder work via native choosers;
      Delete is disabled for Factory.
- [ ] Native file/folder drag-and-drop works.
- [ ] `pluginval` passes.
