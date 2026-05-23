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
  Oscilloscope, VuMeter, VoiceLeds, ClipLed,
  bindSlider, bindCombo,
} from "../widgets/index.js";

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
  // INSTRUMENTS — pinned to the factory root (08-ui-views.md view 4
  // *Relationship to the main-window lists*). The full folder-tree navigator
  // is the patch-browser modal; this list never repaints to a different root.
  //
  // selected: -1 means "no highlight yet" — the FM view coordinates so that
  // exactly one of Instruments / Presets / Import is highlighted at any time,
  // matching the part's actual loaded patch. See updateActiveHighlights().
  const lcd = new LcdList(col.querySelector("#instruments-list"), {
    items: [],
    selected: -1,
    onSelect: async (item) => {
      const loadInstrument = Juce.getNativeFunction("loadInstrument");
      await loadInstrument(item.id);
      fmViewState.activePatchPath = item.id;
      fmViewState.seg?.setText(item.label);
      updateActiveHighlights();
    },
  });
  populatePatchList(lcd, "factory");
  fmViewState.instrumentsList = lcd;

  // FM / SQ / D section pills. The selector calls selectSection(); for this
  // task only FM is live, so a non-FM choice tags the bottom region for the
  // CSS to dim. Task 13 fills in SQ/D contents.
  mountSectionPills(col.querySelector("#section-pills"));

  // CHANNELS 1..6 — the visible side of the FM channel paging contract.
  mountChannelsRow(col.querySelector("#channels-row"));

  // MIDI / TRANSPOSE / RNG / DEL / PAN stack. PAN binds to `lr`; the rest
  // are visual placeholders pending Task 13 (routing modal) and beyond.
  mountPanSlider(col.querySelector("#pan-slider"));

  // View 10 — per-part polyphony controls. All three relays (poly_mode,
  // mono_glide, unison_spread) re-bind on selectChannel via the standard
  // FM paging contract, so a part change repaints them automatically.
  mountPolyphonyGroup(col);
}

function mountPolyphonyGroup(col) {
  const modeBinding   = bindCombo("poly_mode");
  const glideBinding  = bindCombo("mono_glide");
  const spreadBinding = bindSlider("unison_spread");

  // Mode pill row: POLY / MONO / UNISON. Uses the same pill widget as the
  // FM/SQ/D section pills so the visual idiom stays consistent.
  const modeCanvas = col.querySelector("#poly-mode-pills");
  if (modeCanvas) {
    new SectionTabs(modeCanvas, modeBinding, {
      style: "pill", fontSize: 8, labels: ["POLY", "MONO", "UNISON"],
    });
  }

  // Mono glide pill row — visible only in MONO mode.
  const glideCanvas = col.querySelector("#mono-glide-pills");
  if (glideCanvas) {
    new SectionTabs(glideCanvas, glideBinding, {
      style: "pill", fontSize: 8, labels: ["RETRIG", "LEGATO"],
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
// after a save / import / delete / drop / add-folder.
function refreshPinnedLists() {
  if (fmViewState.instrumentsList)
    populatePatchList(fmViewState.instrumentsList, "factory");
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
  // Six clickable cells plus a small "R" reset button at the end. The
  // selected cell gets a red `selected` class. A click calls the native
  // selectChannel(n); the editor's rebuildFmAttachments pushes every
  // part-n value into the FM relays, which in turn fires valueChangedEvent
  // on every FM widget -> the whole panel repaints in one batch
  // (genny-ui.md "Selecting a list item ... repaints every knob, slider,
  // LED ... in one batch").
  host.innerHTML = "";
  const cells = [];
  for (let i = 0; i < NUM_PARTS; ++i) {
    const cell = document.createElement("button");
    cell.type = "button";
    cell.className = "channel-cell bevel-raised";
    cell.textContent = String(i + 1);
    cell.dataset.part = String(i);
    cell.dataset.tip = `SELECT FM CHANNEL ${i + 1}`;
    cell.addEventListener("click", async () => {
      await selectChannelFn(i);
      setSelected(i);
      // Selecting a different part means a different active patch — repaint
      // the Instruments / Presets highlight to match the part's patch.
      refreshActivePatchPathForPart(i);
    });
    host.appendChild(cell);
    cells.push(cell);
  }

  // Reset button for the currently selected part. Lives in the channels row
  // because that's the part-selector context; clicking opens a confirm modal
  // so a slip can't undo a tweak. The native function snaps every parameter
  // with the `_part<n>` suffix to its juce default, which fires the FM-relay
  // valueChangedEvent and repaints the panel.
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

  const setSelected = (i) => {
    for (let j = 0; j < cells.length; ++j)
      cells[j].classList.toggle("selected", j === i);
    fmViewState.selectedPart = i;
  };
  // Expose so applyInitialUiState() (on project reload) can drive the
  // highlight from the persisted selectedPart.
  fmViewState.setChannelsSelected = setSelected;
  setSelected(fmViewState.selectedPart || 0);
}

function mountPanSlider(canvas) {
  // The `lr` parameter is a 2-bit field (bit1=L, bit0=R). The slider exposes
  // it as a continuous 0..3 control; visually the cap glides between the
  // groove ends. A double-click resets to 3 (both channels, the patch
  // default — see PatchSystem.h).
  const binding = bindSlider("lr");
  new Slider(canvas, binding, { defaultNormalised: 1.0 });
}

/* -------------------------------------------------------------------------- */
/* Right column — PRESETS / IMPORT tabs + LCD list                            */
/* -------------------------------------------------------------------------- */

function mountRightCol(col) {
  // PRESETS tab → user-saved root; IMPORT tab → user-imported root. Both
  // start empty on a fresh install and fill in as the user saves / imports
  // (08-ui-views.md view 1 *Patch-list data sources*).
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

  const tabs = makeLocalChoiceBinding(["PRESETS", "IMPORT"], 0, (idx) => {
    fmViewState.presetTab = idx;
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
};

// Walk both pinned lists and highlight only the row matching activePatchPath
// (if any); clear the highlight on the other list. Safe to call any time —
// lists may be empty or unmounted.
function updateActiveHighlights() {
  const path = fmViewState.activePatchPath;
  const instLcd  = fmViewState.instrumentsList;
  const presLcd  = fmViewState.presetList;

  const findIn = (lcd) => {
    if (!lcd || !path) return -1;
    const items = lcd.items || [];
    for (let i = 0; i < items.length; ++i)
      if (items[i].id === path) return i;
    return -1;
  };
  const instIdx = findIn(instLcd);
  const presIdx = findIn(presLcd);
  // Prefer Instruments (factory) over Presets when paths happen to collide;
  // in practice the user-imported and factory roots have disjoint paths.
  if (instIdx >= 0) {
    instLcd?.setSelected(instIdx);
    presLcd?.setSelected(-1);
  } else if (presIdx >= 0) {
    instLcd?.setSelected(-1);
    presLcd?.setSelected(presIdx);
  } else {
    instLcd?.setSelected(-1);
    presLcd?.setSelected(-1);
  }
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
