/*
 * Patch browser modal — 08-ui-views.md view 4 (ADR-0006, 04-patch-system.md
 * "Patch Browser Design"). Full-window overlay: left folder-tree pane + right
 * patch-list pane + a search box on top + a button row at the bottom
 * (Import / Export▾ / Delete / + Add Folder… / Preview / Close).
 *
 * Backend contract (all native functions registered in PluginEditor.cpp):
 *
 *   getRoots()                        -> root summaries (kind, id, path, ...)
 *   getPatchList(folderPath)          -> children of `folderPath` (lazy scan)
 *   searchPatches(query)              -> { name, path, rootId, folderPath }[]
 *   loadInstrument(path) / loadPreset(path)
 *                                     -> apply to currently selected FM part
 *   savePatch(name)                   -> writes TFI into the user-saved root
 *   importFileDialog() / exportFileDialog(format) / addFolderDialog()
 *                                     -> native juce::FileChooser
 *   deletePatch(path)                 -> writable roots only
 *   previewPatch(durationMs)          -> middle-C ~1s into the selected part
 *
 * The browser stays open between loads so the user can audition many patches.
 * A `patchRootsChanged` C++ event fires after any root mutation; we rebuild
 * the tree in place when that lands.
 */

import { openModal } from "./modal-host.js";
import * as Juce from "../juce/index.js";

// Native function handles cached at module scope so the modal builds without
// a JUCE round-trip per click. The Promise wrapper in juce/index.js is created
// once per name.
const getRoots         = Juce.getNativeFunction("getRoots");
const getPatchList     = Juce.getNativeFunction("getPatchList");
const searchPatches    = Juce.getNativeFunction("searchPatches");
const loadInstrument   = Juce.getNativeFunction("loadInstrument");
const importFileDialog = Juce.getNativeFunction("importFileDialog");
const exportFileDialog = Juce.getNativeFunction("exportFileDialog");
const addFolderDialog  = Juce.getNativeFunction("addFolderDialog");
const deletePatch      = Juce.getNativeFunction("deletePatch");
const previewPatch     = Juce.getNativeFunction("previewPatch");

let unsubscribeRoots = null;

// State the modal owns while open. Reset on every openModal() so a reopen
// starts in a clean tree state. The DOM nodes (tree pane, patch pane, delete
// button) are filled in by build() once the panel exists, so the renderers
// don't need them threaded through every call.
function makeState() {
  return {
    roots: [],                   // top-level roots from getRoots()
    expanded: new Set(),         // folder paths whose children are visible
    folders: new Map(),          // path -> folder data (lazy-scanned)
    selectedFolderPath: null,    // currently highlighted folder
    selectedPatchPath: null,     // currently highlighted patch row
    selectedRootId: null,        // root of the selected folder (for Delete)
    searchQuery: "",
    searchHits: null,            // null = tree mode; array = search results
    treePane: null,
    patchPane: null,
    deleteBtn: null,
  };
}

export function openPatchBrowserModal() {
  const state = makeState();

  openModal({
    title: "PATCH BROWSER",
    width: 880,
    height: 480,
    build: (body, ctx) => {
      body.classList.add("patch-browser-body");

      // Search box — debounced; an empty query reverts to tree mode.
      const searchRow = document.createElement("div");
      searchRow.className = "pb-search-row";
      const searchInput = document.createElement("input");
      searchInput.type = "text";
      searchInput.placeholder = "Search patches…";
      searchInput.className = "pb-search-input label";
      searchRow.appendChild(searchInput);
      body.appendChild(searchRow);

      // Two-pane split: tree on the left, patches on the right.
      const split = document.createElement("div");
      split.className = "pb-split";
      const treePane = document.createElement("div");
      treePane.className = "pb-tree bevel-inset";
      const patchPane = document.createElement("div");
      patchPane.className = "pb-patches bevel-inset";
      split.appendChild(treePane);
      split.appendChild(patchPane);
      body.appendChild(split);

      // Bottom button row.
      const buttonRow = document.createElement("div");
      buttonRow.className = "pb-button-row";
      const importBtn  = makeButton("IMPORT FILE");
      const exportBtn  = makeExportButton();
      const deleteBtn  = makeButton("DELETE");
      const addFolderBtn = makeButton("+ ADD FOLDER…");
      const spacer     = document.createElement("span");
      spacer.className = "pb-spacer";
      const previewBtn = makeButton("▶ PREVIEW");
      const closeBtn   = makeButton("CLOSE");
      buttonRow.append(importBtn, exportBtn.host, deleteBtn, addFolderBtn,
                       spacer, previewBtn, closeBtn);
      body.appendChild(buttonRow);

      state.treePane  = treePane;
      state.patchPane = patchPane;
      state.deleteBtn = deleteBtn;

      // ------- Wiring ------------------------------------------------------

      let debounceHandle = 0;
      searchInput.addEventListener("input", () => {
        const q = searchInput.value.trim();
        state.searchQuery = q;
        if (debounceHandle) clearTimeout(debounceHandle);
        debounceHandle = setTimeout(async () => {
          if (q === "") {
            state.searchHits = null;
            renderPatches(state);
            return;
          }
          state.searchHits = (await searchPatches(q)) ?? [];
          renderPatches(state);
        }, 120);
      });

      importBtn.addEventListener("click", () => importFileDialog());
      exportBtn.tfi.addEventListener("click", () => exportFileDialog("tfi"));
      exportBtn.vgi.addEventListener("click", () => exportFileDialog("vgi"));

      deleteBtn.addEventListener("click", async () => {
        if (!state.selectedPatchPath) return;
        if (state.selectedRootId === "factory") return;   // double-guard
        await deletePatch(state.selectedPatchPath);
        state.selectedPatchPath = null;
        // The patchRootsChanged C++ event will refresh on its own.
      });

      addFolderBtn.addEventListener("click", () => addFolderDialog());

      previewBtn.addEventListener("click", () => previewPatch(1000));
      closeBtn.addEventListener("click", () => ctx.close());

      // ------- Initial population + subscriptions --------------------------

      const refreshAll = async () => {
        state.roots = (await getRoots()) ?? [];
        // Drop any expanded path that no longer exists (custom root removed).
        const liveTreePaths = collectAllPaths(state);
        for (const p of [...state.expanded])
          if (!liveTreePaths.has(p)) state.expanded.delete(p);
        renderTree(state);
        renderPatches(state);
        updateDeleteButtonState(state);
      };

      // Listen for backend-driven root mutations (import / save / delete /
      // drop / add-folder). The listener is removed when the modal closes.
      const onRootsChanged = () => { refreshAll(); };
      window.__JUCE__.backend.addEventListener("patchRootsChanged", onRootsChanged);
      unsubscribeRoots = () => {
        // JUCE has no removeEventListener in the same shape; the no-op
        // pattern used elsewhere (settings, midi-routing) is to leave the
        // listener attached. The modal is rebuilt on each open so a fresh
        // state object replaces the closure's captured one.
      };

      const closeOrig = ctx.close;
      ctx.close = (...args) => {
        if (debounceHandle) clearTimeout(debounceHandle);
        if (unsubscribeRoots) { unsubscribeRoots(); unsubscribeRoots = null; }
        exportBtn.cleanup?.();
        closeOrig.apply(ctx, args);
      };

      // Keyboard: Enter loads the selected patch (matches single-click).
      body.tabIndex = 0;
      body.addEventListener("keydown", (e) => {
        if (e.key === "Enter" && state.selectedPatchPath) {
          loadInstrument(state.selectedPatchPath);
        }
      });

      refreshAll();
    },
  });
}

/* -------------------------------------------------------------------------- */
/* Small DOM factories                                                        */
/* -------------------------------------------------------------------------- */

function makeButton(text) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "pb-button bevel-raised label";
  b.textContent = text;
  return b;
}

// Export▾ is a tiny popover with TFI / VGI choices — keep it inline so the
// modal owns the click-away logic instead of importing a popover lib.
function makeExportButton() {
  const host = document.createElement("div");
  host.className = "pb-export-host";

  const main = makeButton("EXPORT▾");
  host.appendChild(main);

  const menu = document.createElement("div");
  menu.className = "pb-export-menu bevel-raised";
  menu.style.display = "none";
  const tfi = makeButton("AS TFI");
  const vgi = makeButton("AS VGI");
  tfi.classList.add("pb-export-item");
  vgi.classList.add("pb-export-item");
  menu.append(tfi, vgi);
  host.appendChild(menu);

  const hide = () => { menu.style.display = "none"; };
  main.addEventListener("click", (e) => {
    e.stopPropagation();
    menu.style.display = (menu.style.display === "none") ? "block" : "none";
  });
  tfi.addEventListener("click", hide);
  vgi.addEventListener("click", hide);
  document.addEventListener("click", hide);

  // Caller invokes this on modal close so the document click listener is not
  // stranded after the menu element is removed from the DOM.
  const cleanup = () => document.removeEventListener("click", hide);
  return { host, tfi, vgi, cleanup };
}

/* -------------------------------------------------------------------------- */
/* Tree pane                                                                  */
/* -------------------------------------------------------------------------- */

function renderTree(state) {
  const treePane = state.treePane;
  treePane.innerHTML = "";

  // Top-level roots in the order PatchBrowser returns them: factory, saved,
  // imported, then any custom roots. Each is a row + a (possibly empty) list
  // of its children below it when expanded.
  for (const root of state.roots) {
    appendNode(state, treePane, {
      path: root.path,
      name: root.displayName,
      patchCount: root.scanned ? root.patchCount : -1,
      kind: root.kind,
      isRoot: true,
      rootId: root.id,
      subfolders: root.subfolders ?? [],
      writable: root.writable,
    }, 0);
  }
}

function appendNode(state, parent, node, depth) {
  const row = document.createElement("div");
  row.className = "pb-tree-row";
  row.style.paddingLeft = `${4 + depth * 12}px`;
  if (state.selectedFolderPath === node.path)
    row.classList.add("pb-selected");

  // Disclosure triangle — only shows when the folder has known children OR
  // hasn't been scanned yet (so the user can try to expand it). We render it
  // even for scanned-empty folders to keep the indent consistent.
  const tri = document.createElement("span");
  tri.className = "pb-tri";
  const expanded = state.expanded.has(node.path);
  tri.textContent = expanded ? "▼" : "▶";

  const label = document.createElement("span");
  label.className = "label pb-name";
  label.textContent = node.name;

  // Read-only padlock glyph on the Factory root (08-ui-views.md view 4).
  if (node.isRoot && node.kind === "factory") {
    const lock = document.createElement("span");
    lock.className = "label pb-lock";
    lock.textContent = "🔒";
    label.appendChild(lock);
  }

  const count = document.createElement("span");
  count.className = "label pb-count";
  if (node.patchCount >= 0) count.textContent = `(${node.patchCount})`;

  row.append(tri, label, count);
  parent.appendChild(row);

  row.addEventListener("click", async (e) => {
    e.stopPropagation();
    if (e.target === tri) {
      await toggleExpand(state, node);
      renderTree(state);
      // Re-rendering the whole tree drops the patch pane's scroll; that's OK
      // — a click on the triangle is an explicit navigation gesture.
    } else {
      // Selecting a row also expands it on first click so the child list is
      // visible (matching standard tree-view behaviour).
      if (!state.expanded.has(node.path))
        await toggleExpand(state, node);
      state.selectedFolderPath = node.path;
      state.selectedRootId    = findRootIdForPath(state, node.path);
      state.searchHits = null;            // selecting a folder exits search
      state.selectedPatchPath = null;
      // Refresh the tree (selection highlight) AND the patch list.
      renderTree(state);
      await loadFolderInto(state, node.path);
      renderPatches(state);
      updateDeleteButtonState(state);
    }
  });

  if (expanded) {
    const childHost = document.createElement("div");
    childHost.className = "pb-tree-children";
    parent.appendChild(childHost);

    // For lazy roots we may not have the children listed in node.subfolders;
    // we always overlay the cached folder snapshot.
    const cached = state.folders.get(node.path);
    const subs   = cached?.subfolders ?? node.subfolders ?? [];
    for (const child of subs) {
      appendNode(state, childHost, {
        path: child.path,
        name: child.name,
        patchCount: child.scanned ? child.patchCount : -1,
        kind: "custom",      // descendant nodes share whatever the parent root is
        isRoot: false,
        rootId: node.rootId,
        subfolders: child.subfolders ?? [],
        writable: node.writable,
      }, depth + 1);
    }
  }
}

async function toggleExpand(state, node) {
  if (state.expanded.has(node.path)) {
    state.expanded.delete(node.path);
    return;
  }
  state.expanded.add(node.path);
  if (!state.folders.has(node.path))
    await loadFolderInto(state, node.path);
}

async function loadFolderInto(state, path) {
  const data = await getPatchList(path);
  if (data && typeof data === "object") {
    state.folders.set(path, data);
  }
}

function findRootIdForPath(state, path) {
  // Walk roots; the first one whose path is a prefix wins. With the lazy
  // tree we don't always know the descendant root, so prefix-match by string.
  for (const r of state.roots)
    if (path === r.path || path.startsWith(r.path)) return r.id;
  return null;
}

function collectAllPaths(state) {
  const out = new Set();
  const walk = (folders) => {
    for (const f of folders) {
      out.add(f.path);
      if (f.subfolders) walk(f.subfolders);
    }
  };
  for (const r of state.roots) {
    out.add(r.path);
    if (r.subfolders) walk(r.subfolders);
  }
  // Also include anything we've fetched lazily.
  for (const [p, data] of state.folders) {
    out.add(p);
    if (data?.subfolders) walk(data.subfolders);
  }
  return out;
}

/* -------------------------------------------------------------------------- */
/* Patch-list pane                                                            */
/* -------------------------------------------------------------------------- */

function renderPatches(state) {
  const patchPane = state.patchPane;
  patchPane.innerHTML = "";

  // Search mode wins over folder selection so the user can type while a
  // folder is selected without losing their query.
  if (state.searchHits !== null) {
    if (state.searchHits.length === 0) {
      patchPane.appendChild(makeEmptyMessage("No matches."));
      return;
    }
    for (const hit of state.searchHits)
      patchPane.appendChild(makePatchRow(state, hit, true));
    return;
  }

  if (!state.selectedFolderPath) {
    patchPane.appendChild(makeEmptyMessage(
      "Select a folder on the left to see its patches."));
    return;
  }

  const folder = state.folders.get(state.selectedFolderPath);
  const patches = folder?.patches ?? [];
  if (patches.length === 0) {
    patchPane.appendChild(makeEmptyMessage("No patches in this folder."));
    return;
  }
  for (const p of patches)
    patchPane.appendChild(makePatchRow(state, {
      name: p.name, path: p.path, folderPath: state.selectedFolderPath,
    }, false));
}

function makePatchRow(state, hit, showFolderPath) {
  const row = document.createElement("div");
  row.className = "pb-patch-row";
  if (state.selectedPatchPath === hit.path)
    row.classList.add("pb-selected");

  const name = document.createElement("span");
  name.className = "label pb-name";
  name.textContent = hit.name;
  row.appendChild(name);

  if (showFolderPath) {
    const sub = document.createElement("span");
    sub.className = "label pb-folder-hint";
    sub.textContent = compactPath(hit.folderPath);
    row.appendChild(sub);
  }

  row.addEventListener("click", async () => {
    state.selectedPatchPath = hit.path;
    if (hit.rootId)
      state.selectedRootId = hit.rootId;
    else if (hit.folderPath)
      state.selectedRootId = findRootIdForPath(state, hit.folderPath);
    // Highlight, then load into the selected FM part. The modal stays open
    // so the user can audition several patches in a row.
    [...row.parentElement.querySelectorAll(".pb-selected")]
        .forEach(el => el.classList.remove("pb-selected"));
    row.classList.add("pb-selected");
    updateDeleteButtonState(state);
    await loadInstrument(hit.path);
  });

  row.addEventListener("dblclick", async () => {
    await loadInstrument(hit.path);
  });

  return row;
}

function makeEmptyMessage(text) {
  const el = document.createElement("div");
  el.className = "pb-empty label";
  el.textContent = text;
  return el;
}

// Path display: the search results show the folder path of each hit.
// We keep the last two path segments to avoid the full filesystem path
// dominating the row — the underlying `path` is still the load target.
function compactPath(p) {
  if (!p) return "";
  const norm = p.replaceAll("\\", "/");
  const parts = norm.split("/").filter(Boolean);
  if (parts.length <= 2) return norm;
  return "…/" + parts.slice(-2).join("/");
}

function updateDeleteButtonState(state) {
  const deleteBtn = state.deleteBtn;
  if (!deleteBtn) return;
  // Factory root is read-only; with no selection or a Factory hit, the
  // button is disabled. The backend enforces the same rule.
  const canDelete = state.selectedPatchPath
                  && state.selectedRootId
                  && state.selectedRootId !== "factory";
  deleteBtn.toggleAttribute("disabled", !canDelete);
}
