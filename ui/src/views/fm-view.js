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
  Oscilloscope, VuMeter, VoiceLeds, ClipLed,
  bindSlider,
} from "../widgets/index.js";

import * as Juce from "../juce/index.js";

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
  // INSTRUMENTS list — quick-access view of the active folder; the full
  // browser modal lands in Task 14. Items are fetched via the existing
  // getPatchList native function.
  const lcd = new LcdList(col.querySelector("#instruments-list"), {
    items: [],
    onSelect: async (item) => {
      const loadInstrument = Juce.getNativeFunction("loadInstrument");
      await loadInstrument(item.id);
      fmViewState.seg?.setText(item.label);
    },
  });
  populatePatchList(lcd, "factory");

  // FM / SQ / D section pills. The selector calls selectSection(); for this
  // task only FM is live, so a non-FM choice tags the bottom region for the
  // CSS to dim. Task 13 fills in SQ/D contents.
  mountSectionPills(col.querySelector("#section-pills"));

  // CHANNELS 1..6 — the visible side of the FM channel paging contract.
  mountChannelsRow(col.querySelector("#channels-row"));

  // MIDI / TRANSPOSE / RNG / DEL / PAN stack. PAN binds to `lr`; the rest
  // are visual placeholders pending Task 13 (routing modal) and beyond.
  mountPanSlider(col.querySelector("#pan-slider"));

  // Polyphony group — view 10 placeholder; Task 15 adds the live controls.
}

// Populate `lcd` with the top-level patches of the given root kind
// ("factory" or "user"). Per 08-ui-views.md view 4 the main-window lists
// are quick-access views: INSTRUMENTS and PRESETS both currently feed off
// the factory bank; IMPORT/the modal browser surface the user root and
// custom roots in Task 14.
function populatePatchList(lcd, kindFilter) {
  const getPatchList = Juce.getNativeFunction("getPatchList");
  getPatchList().then((roots) => {
    const root = roots?.find?.((r) => r.kind === kindFilter);
    if (!root) { lcd.setItems([], 0); return; }
    getPatchList(root.path).then((folder) => {
      const items = [];
      if (Array.isArray(folder?.patches)) {
        for (const p of folder.patches)
          items.push({ id: p.path, label: p.name });
      }
      lcd.setItems(items, 0);
    }).catch(() => lcd.setItems([], 0));
  }).catch(() => lcd.setItems([], 0));
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
  // Six clickable cells. The selected one gets a red `selected` class. A
  // click calls the native selectChannel(n); the editor's
  // rebuildFmAttachments pushes every part-n value into the FM relays, which
  // in turn fires valueChangedEvent on every FM widget -> the whole panel
  // repaints in one batch (genny-ui.md "Selecting a list item ... repaints
  // every knob, slider, LED ... in one batch").
  host.innerHTML = "";
  const cells = [];
  for (let i = 0; i < NUM_PARTS; ++i) {
    const cell = document.createElement("button");
    cell.type = "button";
    cell.className = "channel-cell bevel-raised";
    cell.textContent = String(i + 1);
    cell.dataset.part = String(i);
    cell.addEventListener("click", async () => {
      await selectChannelFn(i);
      setSelected(i);
    });
    host.appendChild(cell);
    cells.push(cell);
  }
  const setSelected = (i) => {
    for (let j = 0; j < cells.length; ++j)
      cells[j].classList.toggle("selected", j === i);
    fmViewState.selectedPart = i;
  };
  setSelected(0);
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
  // The PRESETS list is mounted first so the PRESETS/IMPORT tab callback
  // (declared below) can swap its contents between the factory bank and the
  // user root without a temporal dead-zone reference. Both are quick-access
  // views per ADR-0006; the full modal browser is Task 14.
  const presetList = new LcdList(col.querySelector("#preset-list"), {
    items: [],
    onSelect: async (item) => {
      const loadPreset = Juce.getNativeFunction("loadPreset");
      await loadPreset(item.id);
      fmViewState.seg?.setText(item.label);
    },
  });
  populatePatchList(presetList, "factory");

  // The tabs widget needs a binding; use a local proxy since the tabs choice
  // is not currently an apvts parameter.
  const tabs = makeLocalChoiceBinding(["PRESETS", "IMPORT"], 0, (idx) => {
    fmViewState.presetTab = idx;
    populatePatchList(presetList, idx === 0 ? "factory" : "user");
  });
  new SectionTabs(col.querySelector("#preset-tabs"), tabs, { style: "tab" });
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
};

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
