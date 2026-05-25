// Tagged unified preset browser — `08-ui-views.md` view 5 (ADR-0025).
//
// One modal that shows every FM (.tfi/.vgi/.dmp/.y12/.opm) and SQ (.psg)
// preset across every root (Factory / Saved / Imported / custom). Loading
// a preset auto-switches the instance's mode if the preset's tag differs
// from the current mode; D mode has no preset format and does not appear.
//
// Layout (view 5):
//   [All] [FM] [SQ]            [ Search patches…    🔍 ]
//   ┌────────────────────┬──────────────────────────────┐
//   │ ▼ Factory          │ FM  Bass Guitar              │
//   │ ▼ extra  (custom)  │ FM  Techno Lead              │
//   │   ▶ 01      (842)  │ FM  ▶ Synth Brass  ← sel     │
//   │   ▼ 02      (915)  │ SQ  Pulse Arp                │
//   │ [+ Add Folder…]    │ ...                          │
//   └────────────────────┴──────────────────────────────┘
//   [Import file] [Export▾] [Delete]            [Close]
//
// Native API (see PluginEditor.cpp):
//   getPatchRoots()                 → root tree summaries
//   expandFolder(absolutePath)      → folder children + per-child counts
//   getPatchList()                  → flat list of every preset for search
//   loadPatch(absolutePath)         → applies + auto-switches mode
//   savePatch(name)                 → writes current mode → user-saved root
//   importPatch()                   → file picker + copy to user-imported root
//   exportPatch(format)             → file picker + write
//   addPatchRoot()                  → directory picker
//   deletePatch(absolutePath)       → remove from a writable root
//   patchNav(direction)             → -1 = prev, +1 = next within active mode

import { openModal } from "./modal-host.js";
import { bindCombo } from "../binding.js";
import { getNativeFunction } from "../juce/index.js";

const MODE_FM = 0;
const MODE_SQ = 1;
const MODE_D  = 2;

// One-time CSS injection. Self-contained so design-system.css doesn't need
// to know about the browser modal.
function ensureStyles() {
  if (document.getElementById("genvst-preset-browser-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-preset-browser-style";
  style.textContent = `
    .modal-panel.preset-browser-panel {
      width: 720px;
      max-width: 720px;
      max-height: 460px;
      min-width: 0;
    }
    .preset-browser {
      display: flex;
      flex-direction: column;
      gap: 8px;
      width: 100%;
      height: 100%;
      min-height: 360px;
    }
    .preset-browser .pb-chiprow {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .preset-browser .pb-chiprow .pb-search {
      flex: 1 1 auto;
      min-width: 0;
    }
    .preset-browser .pb-chiprow input.pb-search-input {
      width: 100%;
      box-sizing: border-box;
      font: 500 11px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.10em;
      color: var(--text-on-chassis);
      background: linear-gradient(180deg,
        var(--lcd-bg-top, #1c1f24) 0%,
        var(--lcd-bg-bottom, #14171b) 100%);
      border: 1px solid var(--chassis-edge);
      border-radius: 3px;
      padding: 6px 8px;
      outline: none;
    }
    .preset-browser .pb-panes {
      flex: 1 1 auto;
      display: grid;
      grid-template-columns: 230px 1fr;
      gap: 8px;
      min-height: 0;
    }
    .preset-browser .pb-tree,
    .preset-browser .pb-list {
      overflow: auto;
      background: linear-gradient(180deg,
        var(--lcd-bg-top, #1c1f24) 0%,
        var(--lcd-bg-bottom, #14171b) 100%);
      border: 1px solid var(--chassis-edge);
      border-radius: 3px;
      padding: 4px 4px;
      min-height: 0;
      color: var(--text-on-chassis);
      font: 500 11px/1.4 "IBM Plex Mono", monospace;
      letter-spacing: 0.06em;
    }
    .preset-browser .pb-node {
      display: flex;
      align-items: center;
      gap: 6px;
      padding: 3px 4px;
      cursor: pointer;
      border-radius: 2px;
      user-select: none;
    }
    .preset-browser .pb-node:hover {
      background: rgba(255, 255, 255, 0.06);
    }
    .preset-browser .pb-node.is-selected {
      background: rgba(120, 180, 255, 0.18);
    }
    .preset-browser .pb-node .pb-twisty {
      display: inline-block;
      width: 12px;
      text-align: center;
      opacity: 0.7;
    }
    .preset-browser .pb-node .pb-lock {
      opacity: 0.6;
      font-size: 10px;
    }
    .preset-browser .pb-node .pb-count {
      margin-left: auto;
      opacity: 0.55;
      font-size: 10px;
    }
    .preset-browser .pb-subtree {
      padding-left: 14px;
      display: flex;
      flex-direction: column;
    }
    .preset-browser .pb-row {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 4px 6px;
      cursor: pointer;
      border-radius: 2px;
    }
    .preset-browser .pb-row:hover {
      background: rgba(255, 255, 255, 0.06);
    }
    .preset-browser .pb-row.is-selected {
      background: rgba(120, 180, 255, 0.18);
    }
    .preset-browser .pb-row .pb-badge {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 22px;
      padding: 1px 0;
      border-radius: 2px;
      font: 700 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.12em;
      color: #fff;
      flex: 0 0 22px;
    }
    .preset-browser .pb-badge.pb-badge-fm { background: #4c6ea3; }
    .preset-browser .pb-badge.pb-badge-sq { background: #6f9c5a; }
    .preset-browser .pb-row .pb-name {
      flex: 1 1 auto;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .preset-browser .pb-row .pb-folder {
      opacity: 0.55;
      font-size: 10px;
      max-width: 240px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .preset-browser .pb-toolbar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      padding-top: 4px;
    }
    .preset-browser .pb-toolbar .pb-toolbar-group {
      display: flex;
      gap: 6px;
      align-items: center;
    }
    .preset-browser .pb-toolbar .btn {
      padding: 5px 9px;
    }
    .preset-browser .pb-toolbar .btn.is-disabled {
      opacity: 0.45;
      pointer-events: none;
    }
    .preset-browser .pb-empty {
      padding: 12px;
      text-align: center;
      color: var(--label-text);
      opacity: 0.7;
      font-size: 11px;
    }
    .preset-browser .pb-chiprow .btn-pill .btn.is-active {
      background: linear-gradient(180deg, #4d6680 0%, #2f4258 100%);
      color: #fff;
    }
    .preset-browser .pb-export-menu {
      position: relative;
    }
    .preset-browser .pb-export-pop {
      position: absolute;
      bottom: calc(100% + 4px);
      right: 0;
      display: flex;
      flex-direction: column;
      background: linear-gradient(180deg,
        var(--chassis-bg-top) 0%,
        var(--chassis-bg-bottom) 100%);
      border: 1px solid var(--chassis-edge);
      border-radius: 3px;
      min-width: 80px;
      z-index: 1;
    }
    .preset-browser .pb-export-pop .btn {
      border-radius: 0;
      padding: 6px 10px;
      text-align: left;
    }
  `;
  document.head.appendChild(style);
}

function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  return node;
}

function safeNative(name) {
  try { return getNativeFunction(name); }
  catch (_e) { return null; }
}

// Default chip filter — current mode (FM/SQ), or `All` when in D mode.
function defaultChipFilter(modeIdx) {
  if (modeIdx === MODE_FM) return "FM";
  if (modeIdx === MODE_SQ) return "SQ";
  return "All";
}

export function open() {
  ensureStyles();

  let teardown = () => {};

  return openModal({
    build: (close) => {
      const modeCombo = bindCombo("mode_select");
      const initialMode = modeCombo.getIndex();

      const panel = el("div", { className: "preset-browser" });

      // ------------------------------------------------------------------
      // Title bar (mirrors Settings modal)
      // ------------------------------------------------------------------
      const titleRow = document.createElement("div");
      titleRow.className = "modal-title";
      titleRow.textContent = "PRESETS";
      panel.appendChild(titleRow);

      const closeX = el("button", { className: "modal-close", text: "X" });
      closeX.type = "button";
      closeX.addEventListener("click", () => close());
      panel.appendChild(closeX);

      // ------------------------------------------------------------------
      // Chip row + search
      // ------------------------------------------------------------------
      const chipRow = el("div", { className: "pb-chiprow" });

      const chipWrap = el("div", { className: "btn-pill" });
      const chipLabels = ["All", "FM", "SQ"];
      const chipBtns = {};
      let activeChip = defaultChipFilter(initialMode);
      chipLabels.forEach((label) => {
        const btn = el("button", { className: "btn", text: label });
        btn.type = "button";
        btn.addEventListener("click", () => setChip(label));
        chipWrap.appendChild(btn);
        chipBtns[label] = btn;
      });
      chipRow.appendChild(chipWrap);

      const searchHost = el("div", { className: "pb-search" });
      const searchInput = el("input", { className: "pb-search-input" });
      searchInput.type = "search";
      searchInput.placeholder = "Search patches…";
      searchHost.appendChild(searchInput);
      chipRow.appendChild(searchHost);

      panel.appendChild(chipRow);

      // ------------------------------------------------------------------
      // Two-pane layout
      // ------------------------------------------------------------------
      const panes = el("div", { className: "pb-panes" });

      const treePane = el("div", { className: "pb-tree" });
      const listPane = el("div", { className: "pb-list" });
      panes.appendChild(treePane);
      panes.appendChild(listPane);
      panel.appendChild(panes);

      // ------------------------------------------------------------------
      // Toolbar
      // ------------------------------------------------------------------
      const toolbar = el("div", { className: "pb-toolbar" });

      const leftGroup = el("div", { className: "pb-toolbar-group" });
      const addFolderBtn = el("button", { className: "btn", text: "+ Add Folder…" });
      addFolderBtn.type = "button";
      const importBtn = el("button", { className: "btn", text: "Import file" });
      importBtn.type = "button";
      const exportMenu = el("div", { className: "pb-export-menu" });
      const exportBtn  = el("button", { className: "btn", text: "Export ▾" });
      exportBtn.type = "button";
      exportMenu.appendChild(exportBtn);
      const deleteBtn  = el("button", { className: "btn", text: "Delete" });
      deleteBtn.type = "button";

      leftGroup.appendChild(addFolderBtn);
      leftGroup.appendChild(importBtn);
      leftGroup.appendChild(exportMenu);
      leftGroup.appendChild(deleteBtn);
      toolbar.appendChild(leftGroup);

      const rightGroup = el("div", { className: "pb-toolbar-group" });
      const closeBtn = el("button", { className: "btn", text: "Close" });
      closeBtn.type = "button";
      closeBtn.addEventListener("click", () => close());
      rightGroup.appendChild(closeBtn);
      toolbar.appendChild(rightGroup);

      panel.appendChild(toolbar);

      // ------------------------------------------------------------------
      // State + native handles
      // ------------------------------------------------------------------
      const nat = {
        loadPatch:     safeNative("loadPatch"),
        savePatch:     safeNative("savePatch"),
        importPatch:   safeNative("importPatch"),
        exportPatch:   safeNative("exportPatch"),
        addPatchRoot:  safeNative("addPatchRoot"),
        deletePatch:   safeNative("deletePatch"),
        expandFolder:  safeNative("expandFolder"),
        getPatchList:  safeNative("getPatchList"),
        getPatchRoots: safeNative("getPatchRoots"),
      };

      // Folder tree state — map of absolute path → expanded/scanned.
      const expanded = new Set();
      let selectedFolderPath = null;
      let selectedPatchPath  = null;
      let folderCache = null;          // raw rootsAsJson result
      let folderContents = new Map();  // path → folderAsJson result (cached)
      let flatList = [];               // flat list (FM + SQ) for search
      let searchTerm = "";

      // ------------------------------------------------------------------
      // Rendering
      // ------------------------------------------------------------------
      function renderChips() {
        chipLabels.forEach((label) => {
          chipBtns[label].classList.toggle("is-active", label === activeChip);
        });
      }

      function badge(tag) {
        const b = el("span", { className: "pb-badge pb-badge-" + tag.toLowerCase(), text: tag });
        return b;
      }

      async function fetchRoots() {
        if (!nat.getPatchRoots) { folderCache = []; return; }
        folderCache = await nat.getPatchRoots();
      }

      async function fetchList() {
        if (!nat.getPatchList) { flatList = []; return; }
        flatList = await nat.getPatchList();
      }

      async function ensureFolderContents(path) {
        if (folderContents.has(path)) return folderContents.get(path);
        if (!nat.expandFolder) return null;
        const data = await nat.expandFolder(path);
        if (data) folderContents.set(path, data);
        return data;
      }

      function chipMatchesTag(tag) {
        if (activeChip === "All") return true;
        return activeChip === tag;
      }

      function renderTree() {
        treePane.innerHTML = "";
        if (!folderCache || folderCache.length === 0) {
          treePane.appendChild(el("div", { className: "pb-empty", text: "No patch roots" }));
          return;
        }
        for (const root of folderCache) {
          treePane.appendChild(renderRootNode(root));
        }
      }

      function renderRootNode(root) {
        const wrap = document.createElement("div");
        const node = el("div", { className: "pb-node" });
        const twisty = el("span", {
          className: "pb-twisty",
          text: expanded.has(root.path) ? "▼" : "▶",
        });
        node.appendChild(twisty);
        const label = el("span", { text: root.displayName });
        node.appendChild(label);
        if (root.kind === "factory") node.appendChild(el("span", { className: "pb-lock", text: "🔒" }));
        if (root.scanned && Number.isFinite(root.patchCount) && root.patchCount >= 0) {
          node.appendChild(el("span", { className: "pb-count", text: "(" + root.patchCount + ")" }));
        }
        if (selectedFolderPath === root.path) node.classList.add("is-selected");

        node.addEventListener("click", () => {
          if (expanded.has(root.path)) expanded.delete(root.path);
          else                          expanded.add(root.path);
          selectedFolderPath = root.path;
          renderTree();
          renderList();
        });
        wrap.appendChild(node);

        if (expanded.has(root.path)) {
          const sub = el("div", { className: "pb-subtree" });
          (root.subfolders || []).forEach((sf) => sub.appendChild(renderFolderNode(sf, 1)));
          wrap.appendChild(sub);
        }
        return wrap;
      }

      function renderFolderNode(folder, depth) {
        const wrap = document.createElement("div");
        const node = el("div", { className: "pb-node" });
        const isOpen = expanded.has(folder.path);
        const twisty = el("span", { className: "pb-twisty", text: isOpen ? "▼" : "▶" });
        node.appendChild(twisty);
        node.appendChild(el("span", { text: folder.name }));
        if (folder.scanned && folder.patchCount >= 0)
          node.appendChild(el("span", { className: "pb-count", text: "(" + folder.patchCount + ")" }));
        if (selectedFolderPath === folder.path) node.classList.add("is-selected");

        node.addEventListener("click", async () => {
          if (isOpen) {
            expanded.delete(folder.path);
          } else {
            expanded.add(folder.path);
            await ensureFolderContents(folder.path);
          }
          selectedFolderPath = folder.path;
          renderTree();
          renderList();
        });
        wrap.appendChild(node);

        if (isOpen) {
          const sub = el("div", { className: "pb-subtree" });
          const data = folderContents.get(folder.path);
          if (data && data.subfolders) {
            for (const sf of data.subfolders) sub.appendChild(renderFolderNode(sf, depth + 1));
          }
          wrap.appendChild(sub);
        }
        return wrap;
      }

      function rowsFromFlatList() {
        // Returns rows filtered by the chip and search term, sorted by name.
        return flatList
          .filter((p) => chipMatchesTag(p.tag))
          .filter((p) => !searchTerm
                          || (p.name || "").toLowerCase().includes(searchTerm))
          .sort((a, b) => (a.name || "").localeCompare(b.name || ""));
      }

      function rowsFromSelectedFolder() {
        // Returns patches in the selected folder honouring the chip filter.
        if (!selectedFolderPath) return [];
        // Search the folderCache (root + subfolder summaries) for the
        // selected folder — for root paths, we need the full folder contents.
        const data = folderContents.get(selectedFolderPath);
        if (!data) return [];
        const list = (data.patches || []).map((p) => {
          // Tags are in flatList — look up by path. Fall back to FM if not found
          // (folder contents don't carry the tag; flatList is the source of truth).
          const hit = flatList.find((e) => e.path === p.path);
          return {
            name: p.name,
            path: p.path,
            tag:  hit ? hit.tag : "FM",
            folderPath: selectedFolderPath,
          };
        }).filter((r) => chipMatchesTag(r.tag));
        return list.sort((a, b) => (a.name || "").localeCompare(b.name || ""));
      }

      function renderList() {
        listPane.innerHTML = "";

        // When the user types in the search box, search across every root +
        // tag and show matches. Otherwise show the selected folder's contents.
        const rows = searchTerm ? rowsFromFlatList() : rowsFromSelectedFolder();
        if (rows.length === 0) {
          listPane.appendChild(el("div", { className: "pb-empty",
            text: searchTerm ? "No matches" : "Select a folder" }));
          return;
        }

        for (const r of rows) {
          const row = el("div", { className: "pb-row" });
          row.appendChild(badge(r.tag));
          row.appendChild(el("span", { className: "pb-name", text: r.name }));
          if (searchTerm)
            row.appendChild(el("span", { className: "pb-folder", text: r.folderPath || "" }));
          if (r.path === selectedPatchPath) row.classList.add("is-selected");

          row.addEventListener("click", async () => {
            selectedPatchPath = r.path;
            renderList();
            if (nat.loadPatch) await nat.loadPatch(r.path);
          });
          listPane.appendChild(row);
        }
      }

      async function refreshAll() {
        await Promise.all([fetchRoots(), fetchList()]);
        // Eagerly fetch the contents of every initially-expanded root so the
        // first render shows immediate-child folders + counts.
        for (const root of folderCache || []) {
          if (root && root.path) {
            expanded.add(root.path);   // expand each root by default
            await ensureFolderContents(root.path);
          }
        }
        renderTree();
        renderList();
      }

      // ------------------------------------------------------------------
      // Wiring
      // ------------------------------------------------------------------
      function setChip(label) {
        activeChip = label;
        renderChips();
        renderList();
      }
      setChip(activeChip);

      searchInput.addEventListener("input", () => {
        searchTerm = searchInput.value.trim().toLowerCase();
        renderList();
      });

      addFolderBtn.addEventListener("click", async () => {
        if (!nat.addPatchRoot) return;
        await nat.addPatchRoot();
        await refreshAll();
      });

      importBtn.addEventListener("click", async () => {
        if (!nat.importPatch) return;
        await nat.importPatch();
        await refreshAll();
      });

      deleteBtn.addEventListener("click", async () => {
        if (!nat.deletePatch || !selectedPatchPath) return;
        const path = selectedPatchPath;
        const res = await nat.deletePatch(path);
        if (res && res.ok) {
          selectedPatchPath = null;
          // Folder contents may be stale.
          if (selectedFolderPath) folderContents.delete(selectedFolderPath);
          await refreshAll();
        }
      });

      // Export ▾ — small popover with the available formats for the active
      // mode. Greyed in D mode (no preset format).
      let exportPop = null;
      function dismissExportPop() {
        if (exportPop && exportPop.parentNode === exportMenu)
          exportMenu.removeChild(exportPop);
        exportPop = null;
      }
      function buildExportPop() {
        dismissExportPop();
        const pop = el("div", { className: "pb-export-pop" });
        const formats = modeCombo.getIndex() === MODE_FM
          ? [["TFI", "tfi"], ["VGI", "vgi"]]
          : modeCombo.getIndex() === MODE_SQ
              ? [["PSG", "psg"]]
              : [];
        if (formats.length === 0) return;
        for (const [label, fmt] of formats) {
          const b = el("button", { className: "btn", text: label });
          b.type = "button";
          b.addEventListener("click", async () => {
            dismissExportPop();
            if (nat.exportPatch) await nat.exportPatch(fmt);
          });
          pop.appendChild(b);
        }
        exportMenu.appendChild(pop);
        exportPop = pop;
      }
      exportBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        if (exportPop) dismissExportPop();
        else           buildExportPop();
      });
      panel.addEventListener("click", (e) => {
        if (exportPop && !exportMenu.contains(e.target)) dismissExportPop();
      });

      // D-mode greying — Export is unavailable. The whole modal is reachable
      // from non-D modes only (the header 📂 is greyed in D), so this is a
      // belt-and-braces guard in case the modal is opened mid-mode-switch.
      function applyExportGrey() {
        const isD = modeCombo.getIndex() === MODE_D;
        exportBtn.classList.toggle("is-disabled", isD);
      }
      const unsubMode = modeCombo.onChange(() => {
        applyExportGrey();
        // Mode change refreshes the chip default — but only the first time;
        // once the user has explicitly clicked a chip, we keep their choice.
      });
      applyExportGrey();

      // First render — fire-and-forget the async refresh.
      refreshAll();

      teardown = () => { if (unsubMode) unsubMode(); };

      return panel;
    },
    onClose: () => teardown(),
  });
}
