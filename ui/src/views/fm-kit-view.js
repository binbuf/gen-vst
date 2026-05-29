// FM drum-kit panel (ADR-0021 amendment). Shown in FM mode when a `.gnkit`
// drum kit is active. A 4-bank × 8-pad grid maps MIDI notes to FM patches,
// each played at a fixed pitch with per-pad volume / decay — the RX1200-style
// pad workflow, host-sequenced (no internal sequencer).
//
// All state lives on the C++ side; this view drives it through the kit native
// functions (getKit / setKitSlot / clearKitSlot / saveKit / playPad / exitKit)
// and re-renders from the { active, kit } object each one returns.

import { getNativeFunction } from "../juce/index.js";
import { open as openPresetBrowser } from "../modals/preset-browser.js";

const NUM_PADS     = 32;
const PADS_PER_BANK = 8;
const NUM_BANKS     = 4;

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
function noteName(n) {
  if (n < 0 || n > 127) return "—";
  return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
}

function safeNative(name) {
  try { return getNativeFunction(name); }
  catch (_e) { return null; }
}

function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  if (opts.attrs)     for (const [k, v] of Object.entries(opts.attrs)) node.setAttribute(k, v);
  return node;
}

function ensureStyles() {
  if (document.getElementById("genvst-fm-kit-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-fm-kit-style";
  style.textContent = `
    .fm-kit {
      width: 100%; height: 100%;
      display: grid;
      grid-template-rows: auto auto 1fr;
      gap: 8px;
      padding: 12px 12px 8px;
      box-sizing: border-box;
    }
    .fm-kit .kit-header {
      display: flex; align-items: center; gap: 10px;
    }
    .fm-kit .kit-title {
      font: 600 11px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.22em; text-transform: uppercase;
      color: var(--label-text);
    }
    .fm-kit .kit-name-input {
      flex: 1; max-width: 220px;
      background: rgba(0,0,0,0.25); border: 1px solid rgba(0,0,0,0.4);
      color: var(--lcd-text, #cfe);
      font: 500 11px/1 "IBM Plex Mono", monospace;
      padding: 5px 7px; border-radius: 3px;
    }
    .fm-kit .kit-header .btn { padding: 5px 10px; }
    .fm-kit .kit-spacer { flex: 1; }

    .fm-kit .bank-tabs { display: flex; gap: 4px; }
    .fm-kit .bank-tabs .btn { padding: 4px 12px; }
    .fm-kit .bank-tabs .btn.is-active {
      background: var(--accent, #4a9); color: #07110d;
    }

    .fm-kit .kit-body {
      display: grid;
      grid-template-columns: 1fr 230px;
      gap: 10px;
      min-height: 0;
    }
    .fm-kit .pad-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      grid-template-rows: repeat(2, 1fr);
      gap: 6px;
    }
    .fm-kit .pad {
      position: relative;
      display: flex; flex-direction: column; justify-content: space-between;
      background: rgba(0,0,0,0.20); border: 1px solid rgba(0,0,0,0.45);
      border-radius: 4px; padding: 6px 7px; cursor: pointer;
      min-height: 54px; user-select: none;
      transition: background 80ms, border-color 80ms;
    }
    .fm-kit .pad:hover { background: rgba(255,255,255,0.05); }
    .fm-kit .pad.is-empty { opacity: 0.5; }
    .fm-kit .pad.is-selected { border-color: var(--accent, #4a9); box-shadow: 0 0 0 1px var(--accent,#4a9); }
    .fm-kit .pad.is-flash { background: var(--accent, #4a9); }
    .fm-kit .pad .pad-idx {
      font: 500 7px/1 "IBM Plex Mono", monospace; opacity: 0.55;
      letter-spacing: 0.1em; color: var(--label-text);
    }
    .fm-kit .pad .pad-label {
      font: 600 10px/1.1 "IBM Plex Mono", monospace; color: var(--lcd-text,#cfe);
      overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    }
    .fm-kit .pad .pad-note {
      font: 500 8px/1 "IBM Plex Mono", monospace; opacity: 0.7;
      color: var(--label-text);
    }

    .fm-kit .inspector {
      background: rgba(0,0,0,0.18); border: 1px solid rgba(0,0,0,0.4);
      border-radius: 4px; padding: 10px; overflow: auto;
      display: flex; flex-direction: column; gap: 8px;
    }
    .fm-kit .inspector .insp-title {
      font: 600 9px/1 "IBM Plex Mono", monospace; letter-spacing: 0.18em;
      text-transform: uppercase; color: var(--label-text); opacity: 0.85;
    }
    .fm-kit .inspector .patch-name {
      font: 500 11px/1.2 "IBM Plex Mono", monospace; color: var(--lcd-text,#cfe);
      word-break: break-word;
    }
    .fm-kit .field { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
    .fm-kit .field label {
      font: 500 8px/1 "IBM Plex Mono", monospace; letter-spacing: 0.12em;
      text-transform: uppercase; color: var(--label-text); opacity: 0.85;
    }
    .fm-kit .field input[type=number] {
      width: 70px; background: rgba(0,0,0,0.25); border: 1px solid rgba(0,0,0,0.4);
      color: var(--lcd-text,#cfe); font: 500 10px/1 "IBM Plex Mono", monospace;
      padding: 3px 5px; border-radius: 3px;
    }
    .fm-kit .field input[type=range] { width: 110px; }
    .fm-kit .field .val { font: 500 9px/1 "IBM Plex Mono", monospace; color: var(--label-text); min-width: 30px; text-align: right; }
    .fm-kit .inspector .btn { width: 100%; text-align: center; padding: 6px; }
    .fm-kit .inspector .btn.danger { color: #ff8a8a; }
    .fm-kit .insp-empty { font: 500 10px/1.4 "IBM Plex Mono", monospace; opacity: 0.6; color: var(--label-text); }
  `;
  document.head.appendChild(style);
}

// Expand the sparse kit.slots[] (only enabled pads) into a dense 32-entry array
// keyed by pad index, so the grid can render empty pads too.
function densePads(kit) {
  const out = new Array(NUM_PADS).fill(null);
  const slots = (kit && Array.isArray(kit.slots)) ? kit.slots : [];
  for (const s of slots) {
    if (s && typeof s.pad === "number" && s.pad >= 0 && s.pad < NUM_PADS) out[s.pad] = s;
  }
  return out;
}

export function mount(root, opts = {}) {
  ensureStyles();
  root.classList.add("fm-kit");
  root.innerHTML = "";

  const nat = {
    getKit:       safeNative("getKit"),
    setKitSlot:   safeNative("setKitSlot"),
    clearKitSlot: safeNative("clearKitSlot"),
    saveKit:      safeNative("saveKit"),
    playPad:      safeNative("playPad"),
    exitKit:      safeNative("exitKit"),
  };

  let kit = { name: "", slots: [] };
  let pads = densePads(kit);
  let activeBank = 0;
  let selPad = -1;

  // --- Header ---------------------------------------------------------------
  const header = el("div", { className: "kit-header" });
  header.appendChild(el("div", { className: "kit-title", text: "Drum Kit" }));
  const nameInput = el("input", { className: "kit-name-input", attrs: { type: "text", placeholder: "kit name" } });
  header.appendChild(nameInput);
  const saveBtn = el("button", { className: "btn", text: "SAVE" });
  saveBtn.type = "button";
  const exitBtn = el("button", { className: "btn", text: "FM PATCH ▸" });
  exitBtn.type = "button";
  header.appendChild(saveBtn);
  header.appendChild(exitBtn);
  root.appendChild(header);

  // --- Bank tabs ------------------------------------------------------------
  const bankTabs = el("div", { className: "bank-tabs" });
  const bankBtns = [];
  for (let b = 0; b < NUM_BANKS; ++b) {
    const btn = el("button", { className: "btn", text: `BANK ${b + 1}` });
    btn.type = "button";
    btn.addEventListener("click", () => { activeBank = b; render(); });
    bankTabs.appendChild(btn);
    bankBtns.push(btn);
  }
  root.appendChild(bankTabs);

  // --- Body: pad grid + inspector ------------------------------------------
  const body = el("div", { className: "kit-body" });
  const grid = el("div", { className: "pad-grid" });
  const inspector = el("div", { className: "inspector" });
  body.appendChild(grid);
  body.appendChild(inspector);
  root.appendChild(body);

  function applyKitResult(result) {
    if (result && typeof result === "object") {
      if (result.active === false && typeof opts.onExit === "function") {
        // The engine left kit mode (e.g. exitKit) — hand control back.
        opts.onExit();
        return;
      }
      kit = result.kit || { name: "", slots: [] };
      pads = densePads(kit);
      if (nameInput.value !== (kit.name || "")) nameInput.value = kit.name || "";
      render();
    }
  }

  async function refresh() {
    if (!nat.getKit) return;
    applyKitResult(await nat.getKit());
  }

  function flashPad(pad) {
    const cell = grid.querySelector(`[data-pad="${pad}"]`);
    if (!cell) return;
    cell.classList.add("is-flash");
    setTimeout(() => cell.classList.remove("is-flash"), 120);
  }

  function renderPads() {
    grid.innerHTML = "";
    const base = activeBank * PADS_PER_BANK;
    for (let i = 0; i < PADS_PER_BANK; ++i) {
      const pad = base + i;
      const slot = pads[pad];
      const cell = el("div", { className: "pad" });
      cell.dataset.pad = String(pad);
      if (!slot) cell.classList.add("is-empty");
      if (pad === selPad) cell.classList.add("is-selected");
      cell.appendChild(el("div", { className: "pad-idx", text: `PAD ${pad + 1}` }));
      cell.appendChild(el("div", { className: "pad-label", text: slot ? (slot.label || "patch") : "—" }));
      cell.appendChild(el("div", { className: "pad-note",
        text: slot ? noteName(slot.note) : "empty" }));
      cell.addEventListener("click", () => {
        selPad = pad;
        if (slot && nat.playPad) { nat.playPad(pad); flashPad(pad); }
        render();
      });
      grid.appendChild(cell);
    }
  }

  function fieldRow(labelText, inputEl, valEl) {
    const row = el("div", { className: "field" });
    row.appendChild(el("label", { text: labelText }));
    row.appendChild(inputEl);
    if (valEl) row.appendChild(valEl);
    return row;
  }

  async function assignPatch(pad, defaults) {
    if (!nat.setKitSlot) return;
    openPresetBrowser({
      initialChip: "FM",
      pick: async (path) => {
        const note = defaults.note;
        applyKitResult(await nat.setKitSlot(pad, path, note, note,
                                            defaults.volume, defaults.decayRr));
        selPad = pad;
      },
    });
  }

  // Param-only edit (empty path keeps the embedded patch + label).
  async function updateParams(pad, slot) {
    if (!nat.setKitSlot) return;
    applyKitResult(await nat.setKitSlot(pad, "", slot.note, slot.fixedNote,
                                        slot.volume, slot.decayRr));
  }

  function renderInspector() {
    inspector.innerHTML = "";
    if (selPad < 0) {
      inspector.appendChild(el("div", { className: "insp-empty",
        text: "Select a pad to edit. Click a filled pad to audition it." }));
      return;
    }
    inspector.appendChild(el("div", { className: "insp-title", text: `Pad ${selPad + 1}` }));
    const slot = pads[selPad];

    if (!slot) {
      inspector.appendChild(el("div", { className: "insp-empty", text: "Empty pad." }));
      const assignBtn = el("button", { className: "btn", text: "ASSIGN PATCH" });
      assignBtn.type = "button";
      assignBtn.addEventListener("click", () =>
        assignPatch(selPad, { note: 36 + selPad, volume: 1.0, decayRr: -1 }));
      inspector.appendChild(assignBtn);
      return;
    }

    inspector.appendChild(el("div", { className: "patch-name", text: slot.label || "patch" }));

    // NOTE (trigger) stepper.
    const noteIn = el("input", { attrs: { type: "number", min: "0", max: "127", value: String(slot.note) } });
    noteIn.addEventListener("change", () => {
      slot.note = Math.max(0, Math.min(127, parseInt(noteIn.value, 10) || 0));
      updateParams(selPad, slot);
    });
    inspector.appendChild(fieldRow("Note", noteIn, el("span", { className: "val", text: noteName(slot.note) })));

    // FIXED pitch stepper.
    const fixedIn = el("input", { attrs: { type: "number", min: "0", max: "127", value: String(slot.fixedNote) } });
    fixedIn.addEventListener("change", () => {
      slot.fixedNote = Math.max(0, Math.min(127, parseInt(fixedIn.value, 10) || 0));
      updateParams(selPad, slot);
    });
    inspector.appendChild(fieldRow("Fixed Pitch", fixedIn, el("span", { className: "val", text: noteName(slot.fixedNote) })));

    // VOLUME slider 0..1.
    const volVal = el("span", { className: "val", text: `${Math.round(slot.volume * 100)}%` });
    const volIn = el("input", { attrs: { type: "range", min: "0", max: "100", value: String(Math.round(slot.volume * 100)) } });
    volIn.addEventListener("input", () => { volVal.textContent = `${volIn.value}%`; });
    volIn.addEventListener("change", () => {
      slot.volume = (parseInt(volIn.value, 10) || 0) / 100;
      updateParams(selPad, slot);
    });
    inspector.appendChild(fieldRow("Volume", volIn, volVal));

    // DECAY (RR override): -1 = use patch RR, 0..15 = override.
    const decVal = el("span", { className: "val", text: slot.decayRr < 0 ? "patch" : String(slot.decayRr) });
    const decIn = el("input", { attrs: { type: "number", min: "-1", max: "15", value: String(slot.decayRr) } });
    decIn.addEventListener("change", () => {
      slot.decayRr = Math.max(-1, Math.min(15, parseInt(decIn.value, 10)));
      decVal.textContent = slot.decayRr < 0 ? "patch" : String(slot.decayRr);
      updateParams(selPad, slot);
    });
    inspector.appendChild(fieldRow("Decay (RR)", decIn, decVal));

    const changeBtn = el("button", { className: "btn", text: "CHANGE PATCH" });
    changeBtn.type = "button";
    changeBtn.addEventListener("click", () =>
      assignPatch(selPad, { note: slot.note, volume: slot.volume, decayRr: slot.decayRr }));
    inspector.appendChild(changeBtn);

    const clearBtn = el("button", { className: "btn danger", text: "CLEAR PAD" });
    clearBtn.type = "button";
    clearBtn.addEventListener("click", async () => {
      if (nat.clearKitSlot) applyKitResult(await nat.clearKitSlot(selPad));
    });
    inspector.appendChild(clearBtn);
  }

  function render() {
    bankBtns.forEach((b, i) => b.classList.toggle("is-active", i === activeBank));
    renderPads();
    renderInspector();
  }

  // --- Header wiring --------------------------------------------------------
  saveBtn.addEventListener("click", async () => {
    if (!nat.saveKit) return;
    const name = nameInput.value.trim() || "kit";
    await nat.saveKit(name);
  });
  exitBtn.addEventListener("click", async () => {
    if (nat.exitKit) await nat.exitKit();
    if (typeof opts.onExit === "function") opts.onExit();
  });

  render();
  refresh();

  return {
    dispose() {
      root.classList.remove("fm-kit");
      root.innerHTML = "";
    },
  };
}
