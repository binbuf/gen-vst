/*
 * SQ (PSG) section view — 08-ui-views.md view 2.
 *
 * Three tone panels + one noise panel, plus a section-header band carrying
 * the global `PSG MIX` slider and the `LAYER` toggle. Mounted into the bottom
 * region (#bottom) alongside the FM operator panels; visibility is toggled by
 * the FM/SQ/D section pills via body.dataset.section (set by selectSection
 * in fm-view.js).
 *
 * Bindings:
 *   - Per-channel volume / pan / bend → psg_vol_<id> / psg_pan_<id> /
 *     psg_bend_<id> (id ∈ {ch1, ch2, ch3, noise}).
 *   - Noise type/rate/auto → psg_noise_type / psg_noise_rate / psg_noise_auto.
 *   - PSG mix / layer → psg_mix / psg_layer.
 *   - MIDI step-field → not an apvts parameter; reads/writes the MidiRouter
 *     via the getRouting/setRouting native functions.
 *
 * The `note` readout is driven by C++→JS telemetry (`psgState` event); until
 * the telemetry push lands, it shows a blank "--".
 */

import {
  Knob, Slider, Toggle, LedReadout, SectionTabs,
  bindSlider, bindToggle, bindCombo,
} from "../widgets/index.js";
import { routingStepField } from "./routing-controls.js";

const TONE_CHANNELS = [
  { id: "ch1",   label: "TONE 1", defaultMidi: 11, kind: "psg-tone",  index: 0 },
  { id: "ch2",   label: "TONE 2", defaultMidi: 12, kind: "psg-tone",  index: 1 },
  { id: "ch3",   label: "TONE 3", defaultMidi: 13, kind: "psg-tone",  index: 2 },
];

const NOISE_CHANNEL =
  { id: "noise", label: "NOISE",  defaultMidi: 14, kind: "psg-noise", index: 0 };

export function mountSqView(host) {
  host.innerHTML = "";
  host.className = "sq-section";

  // Section header band: PSG MIX slider + LAYER toggle, plus the section title.
  const header = document.createElement("div");
  header.className = "section-header";

  const title = document.createElement("span");
  title.className = "label section-title";
  title.textContent = "SQUARE · SN76489 PSG";
  header.appendChild(title);

  const mixCell = document.createElement("div");
  mixCell.className = "header-cell";
  const mixLabel = document.createElement("span");
  mixLabel.className = "label";
  mixLabel.textContent = "PSG MIX";
  const mixCanvas = document.createElement("canvas");
  mixCanvas.width = 96; mixCanvas.height = 12;
  const mixReadout = document.createElement("canvas");
  mixCell.appendChild(mixLabel);
  mixCell.appendChild(mixCanvas);
  mixCell.appendChild(mixReadout);
  header.appendChild(mixCell);

  const layerCell = document.createElement("div");
  layerCell.className = "header-cell";
  const layerLabel = document.createElement("span");
  layerLabel.className = "label";
  layerLabel.textContent = "LAYER";
  const layerCanvas = document.createElement("canvas");
  layerCanvas.width = 22; layerCanvas.height = 14;
  layerCell.appendChild(layerLabel);
  layerCell.appendChild(layerCanvas);
  header.appendChild(layerCell);

  host.appendChild(header);

  const mixBinding = bindSlider("psg_mix");
  new Slider(mixCanvas, mixBinding, { defaultNormalised: 0.8 });
  new LedReadout(mixReadout, {
    binding: mixBinding, widthChars: 3,
    format: (s) => Math.round(s * 100).toString(),
  });
  new Toggle(layerCanvas, bindToggle("psg_layer"));

  // Four channel panels (3 tone + 1 noise) — mirrors the FM bottom-row's
  // four operator panels.
  const panels = document.createElement("div");
  panels.className = "sq-panels";
  host.appendChild(panels);

  for (const ch of TONE_CHANNELS)
    panels.appendChild(makeTonePanel(ch));
  panels.appendChild(makeNoisePanel(NOISE_CHANNEL));
}

function makePanelShell(label) {
  const root = document.createElement("div");
  root.className = "sq-panel bevel-raised";

  const head = document.createElement("div");
  head.className = "sq-panel-head";
  const dot = document.createElement("span");
  dot.className = "status-dot";
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = label;
  head.appendChild(dot);
  head.appendChild(lbl);
  root.appendChild(head);

  const body = document.createElement("div");
  body.className = "sq-panel-body";
  root.appendChild(body);

  return { root, body, dot };
}

function makeMidiRow(ch) {
  const row = document.createElement("div");
  row.className = "sq-row sq-midi-row";
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = "MIDI";
  row.appendChild(lbl);

  // Routing step-field: edits MidiRouter via the native getRouting/setRouting
  // functions, not an apvts parameter. The destination is fixed per panel.
  const stepHost = document.createElement("div");
  row.appendChild(stepHost);
  routingStepField(stepHost, { kind: ch.kind, index: ch.index });
  return row;
}

function makeKnobCell(name, paramId, defaultNorm = 0) {
  const cell = document.createElement("div");
  cell.className = "sq-knob-cell";
  const canvas = document.createElement("canvas");
  canvas.width = 28; canvas.height = 28;
  cell.appendChild(canvas);
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = name;
  cell.appendChild(lbl);
  new Knob(canvas, bindSlider(paramId), { defaultNormalised: defaultNorm });
  return cell;
}

function makePanSlider(paramId) {
  const wrap = document.createElement("div");
  wrap.className = "sq-pan-cell";
  const canvas = document.createElement("canvas");
  canvas.width = 80; canvas.height = 12;
  wrap.appendChild(canvas);
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = "PAN";
  wrap.appendChild(lbl);
  new Slider(canvas, bindSlider(paramId), { defaultNormalised: 0.5 });
  return wrap;
}

function makeToggleCell(name, paramId) {
  const cell = document.createElement("div");
  cell.className = "sq-toggle-cell";
  const canvas = document.createElement("canvas");
  canvas.width = 22; canvas.height = 14;
  cell.appendChild(canvas);
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = name;
  cell.appendChild(lbl);
  new Toggle(canvas, bindToggle(paramId));
  return cell;
}

function makeChoiceCell(name, paramId, opts = {}) {
  const cell = document.createElement("div");
  cell.className = "sq-choice-cell";
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = name;
  cell.appendChild(lbl);
  const canvas = document.createElement("canvas");
  canvas.width  = opts.width  ?? 90;
  canvas.height = opts.height ?? 14;
  cell.appendChild(canvas);
  new SectionTabs(canvas, bindCombo(paramId), { style: "pill", fontSize: 8 });
  return cell;
}

function makeNoteReadout() {
  const row = document.createElement("div");
  row.className = "sq-row sq-note-row";
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = "NOTE";
  row.appendChild(lbl);
  const span = document.createElement("span");
  span.className = "sq-note-value label";
  span.textContent = "--";
  row.appendChild(span);
  return { row, span };
}

function makeTonePanel(ch) {
  const { root, body } = makePanelShell(ch.label);

  body.appendChild(makeMidiRow(ch));

  const knobs = document.createElement("div");
  knobs.className = "sq-knob-row";
  knobs.appendChild(makeKnobCell("VOL", `psg_vol_${ch.id}`, 1.0));
  body.appendChild(knobs);

  body.appendChild(makePanSlider(`psg_pan_${ch.id}`));

  body.appendChild(makeToggleCell("BEND", `psg_bend_${ch.id}`));

  const note = makeNoteReadout();
  body.appendChild(note.row);

  return root;
}

function makeNoisePanel(ch) {
  const { root, body } = makePanelShell(ch.label);

  body.appendChild(makeMidiRow(ch));

  const knobs = document.createElement("div");
  knobs.className = "sq-knob-row";
  knobs.appendChild(makeKnobCell("VOL", `psg_vol_${ch.id}`, 1.0));
  body.appendChild(knobs);

  body.appendChild(makePanSlider(`psg_pan_${ch.id}`));

  body.appendChild(makeChoiceCell("TYPE", "psg_noise_type"));
  body.appendChild(makeChoiceCell("RATE", "psg_noise_rate", { width: 110 }));
  body.appendChild(makeToggleCell("AUTO", "psg_noise_auto"));

  return root;
}
