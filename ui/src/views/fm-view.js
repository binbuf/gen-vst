/*
 * FM view — the plugin's primary screen (08-ui-views.md view 1; full visual
 * spec in genny-ui.md). Builds the four-region layout and mounts every FM
 * widget against the apvts via the Task 10 binding helpers.
 *
 * FM channel paging contract (05-ui-ux.md):
 *   - FM relays are named WITHOUT a `_part<n>` suffix (e.g. `atk_op1`).
 *   - JS calls `selectChannel(n)` -> the editor rebuilds every FM attachment
 *     against part n -> each relay's valueChangedEvent fires -> every FM
 *     widget repaints in one batch.
 *   - The CHANNELS 1..6 selector here is the visible side of that contract.
 *
 * The center-column MIDI / TRANSPOSE / RNG / DEL pieces and the polyphony
 * group are layout placeholders for this task — their parameters land in
 * later tasks (Task 13 routing modal, Task 15 polyphony). PAN binds to `lr`
 * (the YM2612 L/R output-enable field).
 */

import {
  Knob, Slider, LedReadout, LcdList, SectionTabs,
  SegDisplay, AlgoButtons, AlgoDiagram, OperatorPanel, Wordmark, GearIcon,
  FolderIcon,
  Oscilloscope, VuMeter, VoiceLeds, ClipLed, TrueStereoToggle,
  StepField,
  RangeSlider, InstrumentRack,
  bindSlider, bindToggle, bindCombo,
} from "../widgets/index.js";
import { setupPixelCanvas, palette, drawBevel, drawLedReadout, GLYPH_H } from "../widgets/pixel.js";

import * as Juce from "../juce/index.js";
import { openPatchBrowserModal } from "../modals/patch-browser.js";
import { confirmModal } from "../modals/modal-host.js";

const NUM_PARTS = 6;
const NUM_OPS = 4;

const selectChannelFn = Juce.getNativeFunction("selectChannel");
const selectSectionFn = Juce.getNativeFunction("selectSection");

// Build the four FM regions inside `chassis`. Returns nothing — the widgets
// hold themselves alive via their relay subscriptions.
export function mountFmView(chassis) {
  mountHeader(chassis.querySelector("#header"));
  const middle = chassis.querySelector("#middle");
  mountLeftColumn (middle.querySelector("#col-left"));
  mountCenter    (middle.querySelector("#col-center"));
  mountRightCol  (middle.querySelector("#col-right"));
  mountBottom    (chassis.querySelector("#bottom"));

  // Restore persisted UI selection state (the editor C++ side already paged
  // selectedPart's FM attachments before opening the WebView, so we just
  // need to flip the JS-side highlights to match). Falls back to (0, 0) if
  // the backend doesn't provide the call (older binaries) or on first run.
  applyInitialUiState();
}

async function applyInitialUiState() {
  let selectedPart = 0;
  let presetTab    = 0;
  try {
    const fn = Juce.getNativeFunction("getInitialUiState");
    const r  = await fn();
    if (r && r.ok) {
      if (typeof r.selectedPart === "number") selectedPart = r.selectedPart;
      if (typeof r.presetTab    === "number") presetTab    = r.presetTab;
    }
  } catch { /* native fn missing — keep defaults */ }

  // Apply selectedPart to the channels row's highlight (the FM attachment
  // rebind was already done C++-side from processor.uiSelectedPart()).
  fmViewState.selectedPart = selectedPart;
  if (typeof fmViewState.setChannelsSelected === "function")
    fmViewState.setChannelsSelected(selectedPart);

  // Apply presetTab to the right-column tabs binding.
  if (fmViewState.presetTabsBinding) {
    fmViewState.presetTabsBinding.setIndex(presetTab);
  }

  // After paging, ask which patch is loaded into the restored part so the
  // Instruments/Presets/Import highlight is correct.
  refreshActivePatchPathForPart(selectedPart);
}

/* -------------------------------------------------------------------------- */
/* Header — wordmark, VU, oscilloscope, 7-seg patch display, voice + clip LEDs */
/* -------------------------------------------------------------------------- */

function mountHeader(header) {
  // Canvas-drawn wordmark (no bitmap image asset — 05-ui-ux.md).
  const wm = header.querySelector("#wordmark");
  new Wordmark(wm);

  // Task 25 — TRUE STEREO toggle cell. Click flips the global apvts param;
  // off = sum L+R to mono in processBlock (PluginProcessor.cpp).
  const trueStereoCanvas = header.querySelector("#true-stereo");
  if (trueStereoCanvas)
    new TrueStereoToggle(trueStereoCanvas, bindToggle("true_stereo"));

  // Header meter bay (08-ui-views.md "Header meter bay"). All four widgets
  // are driven by the C++→JS `meterData` event pushed at ~30 Hz; we hold
  // references so the event handler can route each field to the right widget.
  const vuMeter   = new VuMeter      (header.querySelector("#vu-meter"));
  const scope     = new Oscilloscope (header.querySelector("#scope"));
  const voiceLeds = new VoiceLeds    (header.querySelector("#voice-leds"));
  const clipLed   = new ClipLed      (header.querySelector("#clip-led"));

  // Gear icon (Settings modal entry point — wiring is Task 13).
  new GearIcon(header.querySelector("#gear"));

  // 7-segment patch-name display — the FM-view module exposes a refreshable
  // hook so the channel-selector can repaint it when paging parts.
  const seg = new SegDisplay(header.querySelector("#seg-display"), {
    text: "GEN VST",
  });
  fmViewState.seg = seg;

  window.__JUCE__.backend.addEventListener("meterData", (event) => {
    if (!event) return;
    if (Array.isArray(event.scope))         scope.setSamples(event.scope);
    if (typeof event.vuL === "number" || typeof event.vuR === "number")
      vuMeter.setLevels(event.vuL, event.vuR);
    if (typeof event.voiceMask === "number") voiceLeds.setMask(event.voiceMask);
    if (event.clip) clipLed.setClip(true);
    // Task 34 — per-rack-row activity LEDs. Backend emits one 10-bit mask per
    // row in the current rack ordering; the rack widget skips a repaint when
    // the masks haven't changed.
    if (Array.isArray(event.rowActiveMasks))
      fmViewState.rack?.setActiveMasks(event.rowActiveMasks);
  });
}

/* -------------------------------------------------------------------------- */
/* Left column — LFO / AMS / FMS knobs + ALGORITHM row + diagram + FEEDBACK   */
/* -------------------------------------------------------------------------- */

function mountLeftColumn(col) {
  // LFO knob: bound to lfo_rate (0..7). The "small red power dot" is wired to
  // lfo_enable so clicking it flips the LFO on/off.
  mountKnob(col.querySelector("#lfo-knob"),    "lfo_rate", "LFO");
  mountLed (col.querySelector("#lfo-readout"), "lfo_rate", { offWhenZero: true });

  mountKnob(col.querySelector("#ams-knob"),    "ams", "AMS");
  mountLed (col.querySelector("#ams-readout"), "ams");

  mountKnob(col.querySelector("#fms-knob"),    "pms", "FMS");
  mountLed (col.querySelector("#fms-readout"), "pms");

  // Algorithm row + diagram. Both bind to the same `alg` relay so a click on
  // any of the eight buttons paints both widgets.
  const algBinding = bindSlider("alg");
  new AlgoButtons (col.querySelector("#algo-buttons"), algBinding);
  new AlgoDiagram(col.querySelector("#algo-diagram"), algBinding);

  // Feedback knob: 0..7, 0 displays as "OFF".
  mountKnob(col.querySelector("#fb-knob"),    "fb", "FEEDBACK");
  mountLed (col.querySelector("#fb-readout"), "fb", { offWhenZero: true });
}

/* -------------------------------------------------------------------------- */
/* Center column — INSTRUMENTS lcd-list + section pills + CHANNELS 1..6 +     */
/* MIDI / TRANSPOSE / RNG / DEL / PAN stack + polyphony placeholder           */
/* -------------------------------------------------------------------------- */

function mountCenter(col) {
  // Task 22 — Instrument rack replaces the old fixed Instruments LCD. The
  // rack is the user-curated list of loaded slots; per-instrument routing
  // controls (MIDI / TRPS / RNG / DET / BAL) bind to the selected row's
  // apvts params via heap-pinned relays.
  mountInstrumentRack(col);

  // FM / SQ / D section pills are now read-only — the rack row click sets the
  // type implicitly. Kept rendered so the visual idiom matches Genny.
  mountSectionPills(col.querySelector("#section-pills"));

  // CHANNELS row — read-only slot indicator for the selected rack row's type.
  mountChannelsRow(col.querySelector("#channels-row"));

  // Per-instrument routing strip — bound to the active rack row.
  mountRackRoutingStrip(col);

  // View 10 — per-part polyphony controls. All three relays (poly_mode,
  // mono_glide, unison_spread) re-bind on selectChannel via the standard
  // FM paging contract, so a part change repaints them automatically.
  mountPolyphonyGroup(col);
}

/* -------------------------------------------------------------------------- */
/* Task 22 — Instrument rack widget + add/remove plumbing                     */
/* -------------------------------------------------------------------------- */

const getRackStateFn   = Juce.getNativeFunction("getRackState");
const selectPartFn     = Juce.getNativeFunction("selectPart");
const clearPartFn      = Juce.getNativeFunction("clearPart");
const addInstrumentFn  = Juce.getNativeFunction("addInstrument");
const reorderRackRowFn = Juce.getNativeFunction("reorderRackRow");
const copySlotFn       = Juce.getNativeFunction("copySlot");
const pasteSlotFn      = Juce.getNativeFunction("pasteSlot");

// Human-readable slot-type label for cross-type paste toasts (Task 33). The
// rack widget hides the glyph on incompatible rows, so the toast path only
// fires for the defensive backend rejection — but the wording should still
// match what the user expects to see.
const TYPE_LABEL = { fm: "FM", sq: "PSG", d: "DAC" };

function mountInstrumentRack(col) {
  const canvas = col.querySelector("#instrument-rack");
  if (!canvas) return;

  const rack = new InstrumentRack(canvas, {
    rows: [],
    selected: -1,
    onSelect:  (row) => onRackRowSelected(row),
    onAdd:     (cx, cy) => openAddPopover(cx, cy),
    onRemove:  (row) => removeRow(row),
    onReorder: (fromIdx, toIdx) => reorderRow(fromIdx, toIdx),
    onCopy:    (row) => copyRow(row),
    onPaste:   (row) => pasteRow(row),
  });
  fmViewState.rack = rack;

  // Header buttons mirror the inline + / - cells so the user can still hit
  // them when scrolling past the rack contents.
  const addBtn = document.getElementById("rack-add-btn");
  const remBtn = document.getElementById("rack-remove-btn");
  if (addBtn) addBtn.addEventListener("click", (e) => openAddPopover(e.clientX, e.clientY));
  if (remBtn) remBtn.addEventListener("click", () => {
    const row = fmViewState.rackRows?.[fmViewState.rackSelectedIndex];
    if (row) removeRow(row);
  });

  refreshRack();
}

async function refreshRack() {
  let resp;
  try { resp = await getRackStateFn(); }
  catch { resp = null; }
  const rows = (resp && Array.isArray(resp.rows)) ? resp.rows : [];
  fmViewState.rackRows = rows;
  fmViewState.rackPool = (resp && resp.pool) ? resp.pool
                       : { fm: 5, sq: 4, d: 1 };
  // Keep the previous selection if its row still exists; else pick the first
  // row (or -1 when the rack is empty).
  let selIdx = fmViewState.rackSelectedIndex ?? 0;
  if (selIdx >= rows.length) selIdx = rows.length - 1;
  if (selIdx < 0 && rows.length > 0) selIdx = 0;
  fmViewState.rackSelectedIndex = selIdx;
  fmViewState.rack?.setRows(rows, selIdx);

  // Rebind the routing strip to the freshly-selected row.
  const sel = rows[selIdx];
  if (sel) {
    bindRackRoutingStripToRow(sel);
    applyChannelsRowForType(sel);
  }
  else clearRackRoutingStrip();

  // Refresh seg display + section if the selection points at a sensible row.
  if (sel) {
    const sectionForType = { fm: "FM", sq: "SQ", d: "D" }[sel.type] ?? "FM";
    document.body.dataset.section = sectionForType;
    if (sel.patchName) fmViewState.seg?.setText(sel.patchName);
  }
}

function onRackRowSelected(row) {
  // Find row index from the rack rows array.
  const rows = fmViewState.rackRows ?? [];
  const idx = rows.findIndex(r => r.type === row.type && r.slotIndex === row.slotIndex);
  if (idx >= 0) fmViewState.rackSelectedIndex = idx;

  // Backend selectPart pages FM attachments for FM slots and tags the bottom
  // region for SQ / D slots.
  selectPartFn(row.type, row.slotIndex).then((r) => {
    if (r && r.section) document.body.dataset.section = r.section;
  }).catch(() => {});

  bindRackRoutingStripToRow(row);
  applyChannelsRowForType(row);

  if (row.patchName) fmViewState.seg?.setText(row.patchName);
}

// Repaint the read-only channels row to match the currently selected rack
// row's type + slot index. FM rows highlight cells 0..4; SQ rows show M1..M4
// and highlight the slot index; D rows show just "6" and highlight it.
function applyChannelsRowForType(row) {
  if (typeof fmViewState.setChannelsLabels === "function")
    fmViewState.setChannelsLabels(row.type);

  if (typeof fmViewState.setChannelsSelected === "function") {
    if (row.type === "sq")      fmViewState.setChannelsSelected(row.slotIndex);
    else if (row.type === "d")  fmViewState.setChannelsSelected(5);
    else /* fm */               fmViewState.setChannelsSelected(row.slotIndex);
  }
}

async function removeRow(row) {
  await clearPartFn(row.type, row.slotIndex).catch(() => {});
  await refreshRack();
}

// Task 27 — commit a drag-drop reorder. The widget already moved the row
// locally (so the ghost lands cleanly); this just persists the new order
// through the apvts/PartManager bridge and re-fetches the canonical state.
async function reorderRow(fromIdx, toIdx) {
  // Track the moved row's identity so the post-refresh selection lands on the
  // same instrument the user dragged.
  const rows = fmViewState.rackRows ?? [];
  const moved = rows[fromIdx];
  // refreshRack -> setRows resets scrollY to 0, which would jerk a scrolled
  // rack back to the top after a drag inside it. Capture and restore.
  const savedScroll = fmViewState.rack?.scrollY ?? 0;

  try { await reorderRackRowFn(fromIdx, toIdx); }
  catch (e) { console.error("reorderRackRow failed", e); }

  // Re-fetch authoritative state from C++. setRows() inside refreshRack will
  // restore selection by index; we then pin it to the moved row's slot.
  await refreshRack();

  if (moved && fmViewState.rackRows) {
    const newIdx = fmViewState.rackRows.findIndex(
      r => r.type === moved.type && r.slotIndex === moved.slotIndex);
    if (newIdx >= 0) {
      fmViewState.rackSelectedIndex = newIdx;
      fmViewState.rack?.setSelected(newIdx);
    }
  }

  if (fmViewState.rack) {
    const max = fmViewState.rack._maxScroll();
    fmViewState.rack.scrollY = Math.max(0, Math.min(max, savedScroll));
    fmViewState.rack.render();
  }
}

// Task 33 — Per-slot copy/paste. The clipboard lives in this module (editor-
// session scoped): closing the editor or reloading the project wipes it. The
// payload is the opaque blob returned by the native copySlot — JS only inspects
// `type` so the rack widget can show the paste glyph on compatible rows.
async function copyRow(row) {
  const rows = fmViewState.rackRows ?? [];
  const idx = rows.findIndex(r => r.type === row.type && r.slotIndex === row.slotIndex);
  if (idx < 0) return;

  let resp;
  try { resp = await copySlotFn(idx); }
  catch (e) { console.error("copySlot failed", e); return; }
  if (!resp || !resp.ok) {
    showToast("error", resp?.error ?? "Could not copy slot.");
    return;
  }

  fmViewState.clipboardRow = { type: resp.type, payload: resp.payload };
  fmViewState.rack?.setClipboardType(resp.type);
}

async function pasteRow(row) {
  const clip = fmViewState.clipboardRow;
  if (!clip) return;
  if (clip.type !== row.type) {
    showToast("warn",
      `Cannot paste ${TYPE_LABEL[clip.type] ?? clip.type} patch into `
      + `${TYPE_LABEL[row.type] ?? row.type} slot`);
    return;
  }
  const rows = fmViewState.rackRows ?? [];
  const idx = rows.findIndex(r => r.type === row.type && r.slotIndex === row.slotIndex);
  if (idx < 0) return;

  let resp;
  try { resp = await pasteSlotFn(idx, clip.payload); }
  catch (e) { console.error("pasteSlot failed", e); return; }
  if (!resp || !resp.ok) {
    // The C++ side also rejects type mismatches defensively; map its error
    // back to the same friendly wording as the JS-side guard above.
    if (resp?.error === "type mismatch") {
      showToast("warn",
        `Cannot paste ${TYPE_LABEL[clip.type] ?? clip.type} patch into `
        + `${TYPE_LABEL[row.type] ?? row.type} slot`);
    } else {
      showToast("error", resp?.error ?? "Could not paste slot.");
    }
    return;
  }

  // The C++ side emits patchRootsChanged after a successful paste; the
  // existing listener calls refreshRack, which rebinds the routing strip
  // and refreshes the rack labels. Nothing more to do here.
}

function openAddPopover(clientX, clientY) {
  // 3-row inline popover: FM / SQ / D. Each calls addInstrument(type) then
  // refreshes the rack. FM additionally opens the patch browser scoped to
  // FM. SQ rows have no patch concept (PSG is parameter-driven), so the
  // popover just enables the slot. D rows pop the WAV loader.
  closeAddPopover();

  const pop = document.createElement("div");
  pop.className = "rack-add-popover bevel-raised";
  pop.style.position = "fixed";
  pop.style.zIndex = "100";

  const pool = fmViewState.rackPool ?? { fm: 5, sq: 4, d: 1 };
  const activeByType = countActiveByType(fmViewState.rackRows ?? []);
  const choices = [
    { type: "fm", label: "FM",  available: (activeByType.fm ?? 0) < pool.fm },
    { type: "sq", label: "SQ",  available: (activeByType.sq ?? 0) < pool.sq },
    { type: "d",  label: "D",   available: (activeByType.d  ?? 0) < pool.d  },
  ];

  for (const c of choices) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "rack-add-row" + (c.available ? "" : " disabled");
    btn.textContent = "+ " + c.label;
    if (c.available) btn.addEventListener("click", () => {
      closeAddPopover();
      handleAddInstrument(c.type);
    });
    pop.appendChild(btn);
  }

  document.body.appendChild(pop);
  // Position the popover under the click. Clamp so it stays on-screen.
  const margin = 4;
  const rect = pop.getBoundingClientRect();
  pop.style.left = Math.min(window.innerWidth  - rect.width  - margin, Math.max(margin, clientX))   + "px";
  pop.style.top  = Math.min(window.innerHeight - rect.height - margin, Math.max(margin, clientY + 4)) + "px";

  // Click-away dismiss.
  const onAwayClick = (ev) => {
    if (!pop.contains(ev.target)) {
      closeAddPopover();
      document.removeEventListener("mousedown", onAwayClick, true);
    }
  };
  setTimeout(() => document.addEventListener("mousedown", onAwayClick, true), 0);

  fmViewState.addPopover = pop;
}

function closeAddPopover() {
  fmViewState.addPopover?.remove();
  fmViewState.addPopover = null;
}

function countActiveByType(rows) {
  const out = { fm: 0, sq: 0, d: 0 };
  for (const r of rows) {
    if (out[r.type] !== undefined) out[r.type] += 1;
  }
  return out;
}

async function handleAddInstrument(type) {
  let resp;
  try { resp = await addInstrumentFn(type); }
  catch (e) { console.error(e); return; }
  if (!resp || !resp.ok) {
    showToast("error", resp?.error ?? "Could not add instrument.");
    return;
  }
  // Re-fetch rack state so the new row appears, then run the per-type
  // follow-up (patch browser / WAV loader / nothing).
  await refreshRack();

  if (type === "fm") {
    // Open the patch browser scoped to FM. When a patch is loaded it goes
    // into selectedPart (which addInstrument already set to the new slot).
    openPatchBrowserModal({
      scope: "fm",
      titleSuffix: "FM",
      onLoaded: () => { refreshRack(); },
    });
  } else if (type === "d") {
    // Pop the WAV loader; on success the DAC slot's name updates.
    try {
      const loadWav = Juce.getNativeFunction("loadWavDialog");
      await loadWav();
    } catch (e) { console.error(e); }
    refreshRack();
  }
  // SQ slots are parameter-only; addInstrument already enabled the slot.
}

function showToast(level, message) {
  // Defer to the standard notify event so the user-facing toast pipeline
  // is the same regardless of error source.
  try {
    window.__JUCE__.backend.emitEvent("notify", { level, message });
  } catch { /* dev harness */ }
}

/* -------------------------------------------------------------------------- */
/* Task 22 — Per-instrument routing strip (bound to the selected rack row)    */
/* -------------------------------------------------------------------------- */

function mountRackRoutingStrip(col) {
  // The DOM hosts (midi cell, trps cells, range slider+readout, detune
  // slider+readout, glide slider+readout, balance slider) live in
  // index.html; we recreate the bound widgets each time the selected row
  // changes via bindRackRoutingStripToRow.
  fmViewState.rackRoutingHosts = {
    midi:        col.querySelector("#rack-midi-cell"),
    trpsSt:      col.querySelector("#rack-trps-st-cell"),
    trpsOct:     col.querySelector("#rack-trps-oct-cell"),
    rangeCanvas: col.querySelector("#rack-range-slider"),
    rangeReadout:col.querySelector("#rack-range-readout"),
    detuneCanvas:col.querySelector("#rack-detune-slider"),
    detuneReadout: col.querySelector("#rack-detune-readout"),
    glideLabel:  col.querySelector("#rack-glide-label"),
    glideCell:   col.querySelector("#rack-glide-cell"),
    glideCanvas: col.querySelector("#rack-glide-slider"),
    glideReadout: col.querySelector("#rack-glide-readout"),
    balanceCanvas: col.querySelector("#rack-balance-slider"),
  };
  clearRackRoutingStrip();
}

function clearRackRoutingStrip() {
  const h = fmViewState.rackRoutingHosts;
  if (!h) return;
  // Destroy previous widget bindings (their onChange subscriptions live for
  // the lifetime of the relay state, so re-creating the widget is the way
  // to re-target the bindings).
  for (const w of fmViewState.rackRoutingWidgets ?? []) {
    try { w.destroy?.(); } catch { /* ignore */ }
  }
  fmViewState.rackRoutingWidgets = [];

  // Empty out the DOM hosts so a fresh row build starts clean.
  h.midi.innerHTML = "";
  h.trpsSt.innerHTML = "";
  h.trpsOct.innerHTML = "";
  // The canvases stay — bindRackRoutingStripToRow rebinds them.
}

function bindRackRoutingStripToRow(row) {
  clearRackRoutingStrip();
  const h = fmViewState.rackRoutingHosts;
  if (!h || !row) return;

  const suffix = row.paramSuffix ?? "";

  // MIDI channel — step field 0..16 (0 = off, 16 = max). widthChars: 3 fits
  // a 2-digit "16" plus a leading space; the StepField widget right-aligns.
  {
    const canvas = document.createElement("canvas");
    h.midi.appendChild(canvas);
    const binding = bindSlider("midi_ch" + suffix);
    const w = new StepField(canvas, binding, { widthChars: 3 });
    fmViewState.rackRoutingWidgets.push(w);
  }

  // Transpose — semitone + octave step fields. Signed so negative values
  // render as -1, -2 etc.
  {
    const canvas = document.createElement("canvas");
    h.trpsSt.appendChild(canvas);
    const binding = bindSlider("transpose_st" + suffix);
    const w = new StepField(canvas, binding, { widthChars: 3, signed: true });
    fmViewState.rackRoutingWidgets.push(w);
  }
  {
    const canvas = document.createElement("canvas");
    h.trpsOct.appendChild(canvas);
    const binding = bindSlider("transpose_oct" + suffix);
    const w = new StepField(canvas, binding, { widthChars: 2, signed: true });
    fmViewState.rackRoutingWidgets.push(w);
  }

  // Range — two-thumb slider + a small "lo-hi" LED readout.
  {
    const loBinding = bindSlider("note_lo" + suffix);
    const hiBinding = bindSlider("note_hi" + suffix);
    const w = new RangeSlider(h.rangeCanvas, { loBinding, hiBinding });
    fmViewState.rackRoutingWidgets.push(w);

    const readout = mountRangeReadout(h.rangeReadout, loBinding, hiBinding);
    if (readout) fmViewState.rackRoutingWidgets.push(readout);
  }

  // Detune cents — slider + LED readout. Range -100..+100.
  {
    const binding = bindSlider("detune_cents" + suffix);
    const w = new Slider(h.detuneCanvas, binding, { defaultNormalised: 0.5 });
    fmViewState.rackRoutingWidgets.push(w);

    const readout = new LedReadout(h.detuneReadout, {
      binding, widthChars: 3, offWhenZero: false, signed: true,
    });
    fmViewState.rackRoutingWidgets.push(readout);
  }

  // Task 28 — Glide time (portamento) in ms. Only FM parts and PSG tone
  // channels expose a glide_time apvts param; DAC and PSG noise have no
  // pitch (and no param), so the whole GLD row collapses for those slots.
  const glideParamId = glideParamIdForSuffix(suffix);
  if (glideParamId !== null) {
    if (h.glideLabel)  h.glideLabel.style.display = "";
    if (h.glideCell)   h.glideCell.style.display  = "";
    const binding = bindSlider(glideParamId);
    const w = new Slider(h.glideCanvas, binding, { defaultNormalised: 0 });
    fmViewState.rackRoutingWidgets.push(w);

    // Readout shows "OFF" when value is 0, ms value otherwise. 4 chars fits
    // up to "2000" + the "OFF" sentinel.
    const readout = new LedReadout(h.glideReadout, {
      binding, widthChars: 4, offWhenZero: true,
    });
    fmViewState.rackRoutingWidgets.push(readout);
  } else {
    // Hide both the GLD label and the cell so the BAL row collapses up
    // visually for DAC / PSG-noise (which have no pitch and no glide param).
    if (h.glideLabel) h.glideLabel.style.display = "none";
    if (h.glideCell)  h.glideCell.style.display  = "none";
  }

  // Balance — float -1..+1 slider.
  {
    const binding = bindSlider("balance" + suffix);
    const w = new Slider(h.balanceCanvas, binding, { defaultNormalised: 0.5 });
    fmViewState.rackRoutingWidgets.push(w);
  }
}

// Map a rack-row paramSuffix (e.g. "_part1", "_psg_ch2", "_dac") to the
// matching glide_time apvts param id, or null if the row type has no glide.
// FM parts: "glide_time_partN"; PSG tones: "glide_time_psg_chN"; everything
// else (DAC, PSG noise) returns null.
function glideParamIdForSuffix(suffix) {
  if (!suffix) return null;
  if (suffix.startsWith("_part")) return "glide_time" + suffix;
  if (/^_psg_ch[123]$/.test(suffix)) return "glide_time" + suffix;
  return null;
}

// Compact LED readout for the RNG widget: shows the lo-hi pair as
// space-padded MIDI note numbers, e.g. " 60- 72".
function mountRangeReadout(canvas, loBinding, hiBinding) {
  if (!canvas) return null;
  // Mirrors the LedReadout sizing trick: the canvas attributes were already
  // set in index.html. We draw via the shared LED renderer.
  const setup = setupPixelCanvas(canvas);
  const ctx = setup.ctx, w = setup.width, h = setup.height;
  let unsubLo = null, unsubHi = null;

  const render = () => {
    const pal = palette();
    ctx.fillStyle = pal["led-base"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, false);
    const lo = Math.round(loBinding.getScaled?.() ?? 0);
    const hi = Math.round(hiBinding.getScaled?.() ?? 127);
    const text = `${pad(lo, 3)}-${pad(hi, 3)}`;
    drawLedReadout(ctx, 2, Math.floor((h - GLYPH_H) / 2), text, 7);
  };

  unsubLo = loBinding.onChange(() => render());
  unsubHi = hiBinding.onChange(() => render());
  render();
  return {
    destroy: () => { unsubLo?.(); unsubHi?.(); },
  };
}

function pad(n, width) {
  const s = String(n);
  if (s.length >= width) return s;
  return " ".repeat(width - s.length) + s;
}

// Listen for cross-instance / state-restore changes so the rack repaints
// when patches arrive on another path.
if (typeof window !== "undefined" && window.__JUCE__?.backend) {
  window.__JUCE__.backend.addEventListener("patchRootsChanged", () => {
    refreshRack();
  });
}

function mountPolyphonyGroup(col) {
  const modeBinding   = bindCombo("poly_mode");
  const glideBinding  = bindCombo("mono_glide");
  const spreadBinding = bindSlider("unison_spread");

  // Mode pill row: P / M / U for POLY / MONO / UNISON. Single-letter labels
  // keep the row narrow enough to share the 124px center-controls column with
  // the rack's per-instrument routing strip; the row label above the pills
  // ("POLY") plus the tooltip carry the full word for readers.
  const modeCanvas = col.querySelector("#poly-mode-pills");
  if (modeCanvas) {
    new SectionTabs(modeCanvas, modeBinding, {
      style: "pill", fontSize: 8, labels: ["P", "M", "U"],
    });
  }

  // Mono glide pill row — visible only in MONO mode. R / L for RETRIG / LEGATO,
  // same rationale as the mode pills.
  const glideCanvas = col.querySelector("#mono-glide-pills");
  if (glideCanvas) {
    new SectionTabs(glideCanvas, glideBinding, {
      style: "pill", fontSize: 8, labels: ["R", "L"],
    });
  }

  // Unison spread slider + readout in cents — visible only in UNISON mode.
  const spreadCanvas = col.querySelector("#unison-spread-slider");
  if (spreadCanvas)
    new Slider(spreadCanvas, spreadBinding, { defaultNormalised: 12.0 / 50.0 });

  const spreadReadout = col.querySelector("#unison-spread-readout");
  if (spreadReadout) {
    new LedReadout(spreadReadout, {
      binding: spreadBinding,
      widthChars: 2,
      offWhenZero: false,
    });
  }

  // Show / hide the GLIDE and SPREAD sub-rows based on the current mode.
  // The "poly-sub-label" spans are paired with their following control via
  // CSS grid; we toggle data attributes on the parent group so the CSS hides
  // the correct rows together.
  const group = col.querySelector(".poly-group");
  const applyVisibility = () => {
    if (!group) return;
    const idx = modeBinding.getIndex();
    group.dataset.mode = ["poly", "mono", "unison"][idx] ?? "poly";
  };
  modeBinding.onChange(() => applyVisibility());
  applyVisibility();
}

// Populate `lcd` with the top-level patches of the given root kind.
// 08-ui-views.md view 4 *Patch-list data sources*: the three main-window
// lists are *pinned* to one root each — INSTRUMENTS → "factory", PRESETS →
// "user-saved", IMPORT → "user-imported". Choosing a folder in the modal
// browser does not repaint them; the modal is where any other root is
// browsed.
function populatePatchList(lcd, kindFilter) {
  const getPatchList = Juce.getNativeFunction("getPatchList");
  // Always set selectedIndex = -1: the per-list "selected" state is owned by
  // updateActiveHighlights now, which picks one list based on the current
  // active patch path. Calling setItems with 0 would re-introduce the bug
  // where both lists showed an inverse-video row.
  getPatchList().then((roots) => {
    const root = roots?.find?.((r) => r.kind === kindFilter);
    if (!root) { lcd.setItems([], -1); updateActiveHighlights(); return; }
    getPatchList(root.path).then((folder) => {
      const items = [];
      if (Array.isArray(folder?.patches)) {
        for (const p of folder.patches)
          items.push({ id: p.path, label: p.name });
      }
      lcd.setItems(items, -1);
      updateActiveHighlights();
    }).catch(() => { lcd.setItems([], -1); updateActiveHighlights(); });
  }).catch(() => { lcd.setItems([], -1); updateActiveHighlights(); });
}

// Re-populate every pinned list. Called by the patchRootsChanged C++ event
// after a save / import / delete / drop / add-folder. Task 22: the Instruments
// list is gone (replaced by the rack), so we only refresh the right-column
// Presets/Import list here.
function refreshPinnedLists() {
  refreshPresetList();
}

function refreshPresetList() {
  if (!fmViewState.presetList) return;
  const kind = fmViewState.presetTab === 0 ? "user-saved" : "user-imported";
  populatePatchList(fmViewState.presetList, kind);
}

function mountSectionPills(canvas) {
  // The section selector is purely a UI affordance — it does not map to an
  // apvts parameter. Until Task 13 introduces an SQ/D `selectSection` relay,
  // we manage the choice state locally via a small in-page proxy that
  // mimics the SectionTabs binding interface (the canvas widget only needs
  // getChoices/getIndex/setIndex/onChange).
  const proxy = makeLocalChoiceBinding(["FM", "SQ", "D"], 0, (idx, label) => {
    selectSectionFn(label).catch(() => {});
    document.body.dataset.section = label;
  });
  new SectionTabs(canvas, proxy, { style: "pill" });
}

function mountChannelsRow(host) {
  // Task 22: this row is now a *read-only* slot indicator for the currently
  // selected rack row. FM rows show "1 2 3 4 5"; SQ rows show "M1 M2 M3 M4";
  // D rows show just "6" (the DAC chip channel). User-driven slot reassignment
  // is a post-MVP nicety — the row is purely visual context here.
  host.innerHTML = "";
  const cells = [];
  const labels = ["1", "2", "3", "4", "5", "6"];
  for (let i = 0; i < labels.length; ++i) {
    const cell = document.createElement("div");
    cell.className = "channel-cell bevel-raised";
    cell.textContent = labels[i];
    cell.dataset.slot = String(i);
    host.appendChild(cell);
    cells.push(cell);
  }

  // Reset button for the currently selected part. Click opens a confirm
  // modal so a slip can't undo a tweak. The native function snaps every
  // parameter with the `_part<n>` suffix to its juce default, which fires
  // the FM-relay valueChangedEvent and repaints the panel. Stays interactive
  // even though the channel cells themselves are read-only this pass.
  const resetBtn = document.createElement("button");
  resetBtn.type = "button";
  resetBtn.className = "channel-cell channel-reset bevel-raised";
  resetBtn.textContent = "R";
  resetBtn.dataset.tip = "RESET CURRENT PART TO DEFAULTS";
  resetBtn.addEventListener("click", () => {
    const partLabel = String(fmViewState.selectedPart + 1);
    confirmModal({
      title: "RESET PART",
      message: `Reset all parameters on FM part ${partLabel} to defaults?`,
      confirmLabel: "RESET PART",
      onConfirm: async () => {
        const fn = Juce.getNativeFunction("resetCurrentPart");
        try { await fn(); } catch (e) { console.error(e); }
        // Clearing active patch path also clears the highlight + seg display.
        fmViewState.activePatchPath = null;
        fmViewState.seg?.setText("GEN VST");
        updateActiveHighlights();
      },
    });
  });
  host.appendChild(resetBtn);

  // Apply the read-only highlight + labels for the currently selected rack
  // row's slot. setChannelsSelected is exposed so applyInitialUiState() (on
  // project reload) can repaint the highlight from the persisted state.
  const setSelected = (i) => {
    for (let j = 0; j < cells.length; ++j)
      cells[j].classList.toggle("selected", j === i);
    fmViewState.selectedPart = i;
  };
  fmViewState.setChannelsSelected = setSelected;
  fmViewState.setChannelsLabels = (rowType) => {
    // FM rows show 1..5, SQ rows show M1..M4, D rows show "6" alone.
    if (rowType === "sq") {
      const sqLabels = ["M1", "M2", "M3", "M4", "", ""];
      for (let i = 0; i < cells.length; ++i) {
        cells[i].textContent = sqLabels[i];
        cells[i].style.visibility = sqLabels[i] === "" ? "hidden" : "";
      }
    } else if (rowType === "d") {
      for (let i = 0; i < cells.length; ++i) {
        cells[i].textContent = i === 5 ? "6" : "";
        cells[i].style.visibility = i === 5 ? "" : "hidden";
      }
    } else {
      const fmLabels = ["1", "2", "3", "4", "5", ""];
      for (let i = 0; i < cells.length; ++i) {
        cells[i].textContent = fmLabels[i];
        cells[i].style.visibility = fmLabels[i] === "" ? "hidden" : "";
      }
    }
  };
  setSelected(fmViewState.selectedPart || 0);
}

// Note: the legacy global PAN slider that bound to `lr` is replaced by the
// per-instrument BAL slider in the rack-routing strip (Task 22). The `lr`
// parameter is still bound by the FM CC dispatch (CC 10) — only the visible
// widget is gone.

/* -------------------------------------------------------------------------- */
/* Right column — PRESETS / IMPORT tabs + LCD list                            */
/* -------------------------------------------------------------------------- */

function mountRightCol(col) {
  // PRESETS tab → user-saved patch list. IMPORT tab (Task 24) → the
  // 8-button action stack. The PRESETS list and the IMPORT actions both
  // live in the panel body and swap visibility on tab change — only one
  // is visible at a time (Genny's IMPORT tab is purely an action menu;
  // the imported patches themselves live in the patch browser modal).
  const presetList = new LcdList(col.querySelector("#preset-list"), {
    items: [],
    selected: -1,
    onSelect: async (item) => {
      const loadPreset = Juce.getNativeFunction("loadPreset");
      await loadPreset(item.id);
      fmViewState.activePatchPath = item.id;
      fmViewState.seg?.setText(item.label);
      updateActiveHighlights();
    },
  });
  fmViewState.presetList = presetList;
  populatePatchList(presetList, "user-saved");

  // Task 24 — IMPORT tab action stack. Eight vertical buttons in Genny's
  // top-to-bottom order. Each button calls a native fn and lets the C++
  // side handle toast feedback via the existing `notify` event channel.
  const importActions = mountImportActions(col.querySelector(".panel-body"));
  fmViewState.importActions = importActions;

  // Initial visibility — applyInitialUiState updates this once the
  // persisted tab choice is known.
  importActions.style.display = "none";

  const tabs = makeLocalChoiceBinding(["PRESETS", "IMPORT"], 0, (idx) => {
    fmViewState.presetTab = idx;
    // Swap visibility: PRESETS shows the patch list; IMPORT shows the
    // action stack. The presetTabsBinding's initial value (idx=0) and the
    // applyInitialUiState restore path both feed through here.
    const showImport = (idx === 1);
    const presetCanvas = col.querySelector("#preset-list");
    if (presetCanvas) presetCanvas.style.display = showImport ? "none" : "";
    importActions.style.display = showImport ? "flex" : "none";
    refreshPresetList();
    // Persist the choice C++-side so reopening the DAW project picks it up.
    try {
      const setTab = Juce.getNativeFunction("setPresetTab");
      setTab(idx).catch(() => {});
    } catch { /* native fn missing — non-fatal */ }
  });
  fmViewState.presetTabsBinding = tabs;
  new SectionTabs(col.querySelector("#preset-tabs"), tabs, { style: "tab" });

  // Folder icon in the tab header — opens the patch-browser modal
  // (08-ui-views.md view 4). Created here in JS so we don't have to thread
  // it through index.html; positioned absolutely against the panel header.
  const header = col.querySelector(".panel-header");
  if (header) {
    header.style.position  = "relative";
    header.style.justifyContent = "space-between";
    const iconCanvas = document.createElement("canvas");
    iconCanvas.width  = 16;
    iconCanvas.height = 12;
    iconCanvas.className = "right-col-folder-icon";
    iconCanvas.style.cursor = "pointer";
    iconCanvas.title = "Open patch browser";
    iconCanvas.dataset.tip = "OPEN PATCH BROWSER";
    header.appendChild(iconCanvas);
    new FolderIcon(iconCanvas);
    iconCanvas.addEventListener("click", () => openPatchBrowserModal());
  }
}

/* -------------------------------------------------------------------------- */
/* Task 24 — IMPORT tab action stack                                          */
/* -------------------------------------------------------------------------- */

// Genny-style 8-button vertical stack. Each entry maps a visible label to a
// native function name + an optional tooltip. Click handlers await the native
// fn and surface errors via the standard `notify` toast pipeline; success
// feedback is emitted by the C++ side, so the JS click handler stays trivial.
const IMPORT_ACTIONS = [
  { label: "IMPORT BANK",       fn: "importBankDialog",
    tip: "IMPORT EVERY PATCH FROM A VGM/VGZ FILE OR A SAVED .GNBANK BUNDLE" },
  { label: "EXPORT BANK",       fn: "exportBankDialog",
    tip: "EXPORT THE CURRENT RACK TO A .GNBANK JSON BUNDLE" },
  { label: "LOAD STATE",        fn: "loadStateDialog",
    tip: "LOAD A SAVED .GNVST PLUGIN STATE" },
  { label: "SAVE STATE",        fn: "saveStateDialog",
    tip: "SAVE THE FULL PLUGIN STATE TO A .GNVST FILE" },
  { label: "IMPORT INSTRUMENT", fn: "importInstrumentDialog",
    tip: "IMPORT A SINGLE PATCH FILE (TFI / VGI / DMP / Y12 / OPM)" },
  { label: "EXPORT INSTRUMENT", fn: "exportInstrumentDialog",
    tip: "EXPORT THE SELECTED PART AS A TFI OR VGI FILE" },
  { label: "LOG VGM",           fn: "toggleVgmLogging",
    tip: "RECORD A VGM LOG OF EVERY CHIP REGISTER WRITE" },
  { label: "IMPORT TUNING",     fn: "importTuningDialog",
    tip: "APPLY A SCALA .SCL TUNING FILE" },
];

function mountImportActions(panelBody) {
  const host = document.createElement("div");
  host.className = "import-actions";
  for (const entry of IMPORT_ACTIONS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "pb-button bevel-raised label import-action-btn";
    btn.textContent = entry.label;
    btn.dataset.tip = entry.tip ?? entry.label;
    btn.addEventListener("click", async () => {
      try {
        const fn = Juce.getNativeFunction(entry.fn);
        const r = await fn();
        // Task 29 — LOG VGM is a toggle: flip the button label between
        // "LOG VGM" and "STOP LOG" so the user can tell at a glance whether
        // a capture is running. The C++ side returns {active: boolean} so
        // the JS doesn't have to mirror toggle state.
        if (entry.fn === "toggleVgmLogging"
            && r && typeof r.active === "boolean") {
          btn.textContent = r.active ? "STOP LOG" : "LOG VGM";
        }
      } catch (e) { console.error(`${entry.fn} failed`, e); }
    });
    host.appendChild(btn);
  }
  panelBody.appendChild(host);
  return host;
}

/* -------------------------------------------------------------------------- */
/* Bottom row — four operator panels                                          */
/* -------------------------------------------------------------------------- */

function mountBottom(bottom) {
  bottom.innerHTML = "";
  for (let op = 0; op < NUM_OPS; ++op) {
    const host = document.createElement("div");
    host.className = "op-host bevel-raised";
    bottom.appendChild(host);

    // Per-operator bindings — the FM-relay names use 1-indexed op numbers.
    const b = (base) => bindSlider(`${base}_op${op + 1}`);
    new OperatorPanel(host, {
      opNumber: op + 1,
      bindings: {
        ar:   b("ar"),
        dr:   b("dr"),
        sl:   b("sl"),
        sr:   b("sr"),
        rr:   b("rr"),
        dt:   b("dt"),
        mul:  b("mul"),
        ks:   b("ks"),
        amon: b("amon"),
        ssg:  b("ssg"),
      },
    });
  }
}

/* -------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* -------------------------------------------------------------------------- */

const fmViewState = {
  selectedPart: 0,
  presetTab: 0,
  seg: null,
  instrumentsList: null,
  presetList: null,
  // Absolute path of the patch currently loaded into the selected part.
  // Used to find which pinned list owns it and highlight only that row.
  activePatchPath: null,
  // Exposed by mountChannelsRow + mountRightCol so applyInitialUiState()
  // can restore the persisted highlight + tab choice after project reload.
  setChannelsSelected: null,
  presetTabsBinding:   null,
  // Task 33 — Per-slot copy/paste clipboard ({ type, payload } when set, null
  // otherwise). Editor-session scoped: a new editor mount starts empty.
  clipboardRow: null,
};

// Walk the right-column Presets/Import list and highlight the row matching
// activePatchPath. Task 22: the center column is now the rack widget — it
// owns its own selection state via getRackState(), so this helper no longer
// touches an "Instruments" LCD list.
function updateActiveHighlights() {
  const path = fmViewState.activePatchPath;
  const presLcd  = fmViewState.presetList;

  const findIn = (lcd) => {
    if (!lcd || !path) return -1;
    const items = lcd.items || [];
    for (let i = 0; i < items.length; ++i)
      if (items[i].id === path) return i;
    return -1;
  };
  const presIdx = findIn(presLcd);
  if (presIdx >= 0) presLcd?.setSelected(presIdx);
  else              presLcd?.setSelected(-1);
}

// Ask the backend which patch (if any) is loaded into a given part and
// update activePatchPath accordingly. Used when the user pages channels —
// each part can have its own loaded patch.
async function refreshActivePatchPathForPart(part) {
  try {
    const fn = Juce.getNativeFunction("getActivePatchPath");
    const r = await fn(part);
    fmViewState.activePatchPath = (r && r.ok) ? (r.path || null) : null;
  } catch {
    fmViewState.activePatchPath = null;
  }
  updateActiveHighlights();
}

// Re-fetch every pinned list whenever the backend signals a root mutation
// (save / import / delete / drop / add-folder — see PluginEditor.cpp's
// emitPatchRootsChanged). Registered once at module load so the listener
// survives across modal opens/closes; the registration is idempotent because
// the event name is unique.
if (typeof window !== "undefined" && window.__JUCE__?.backend) {
  window.__JUCE__.backend.addEventListener("patchRootsChanged", () => {
    refreshPinnedLists();
    // The lists may now contain (or no longer contain) the active patch —
    // re-resolve which row to highlight after the items repopulate.
    updateActiveHighlights();
  });
}

function mountKnob(canvas, name, _label) {
  if (!canvas) return null;
  const b = bindSlider(name);
  return new Knob(canvas, b, { defaultNormalised: 0 });
}

function mountLed(canvas, name, opts = {}) {
  if (!canvas) return null;
  const b = bindSlider(name);
  return new LedReadout(canvas, {
    binding: b,
    widthChars: opts.widthChars ?? 3,
    offWhenZero: !!opts.offWhenZero,
    signed: !!opts.signed,
  });
}

// Build a small object that exposes the same getChoices/getIndex/setIndex/
// onChange/onProperties surface as a ComboBinding, but is backed by a JS
// local — for switch-style UIs that aren't yet apvts-parameterised.
function makeLocalChoiceBinding(choices, initial, onChange) {
  const listeners = new Set();
  let index = initial;
  return {
    properties: { choices },
    getChoices: () => choices,
    getIndex:   () => index,
    setIndex:   (i) => {
      const n = choices.length;
      if (n === 0) return;
      const clamped = Math.max(0, Math.min(n - 1, i | 0));
      if (clamped === index) return;
      index = clamped;
      for (const fn of listeners) fn();
      onChange?.(index, choices[index]);
    },
    onChange:     (fn) => { listeners.add(fn); return () => listeners.delete(fn); },
    onProperties: () => () => {},
  };
}
