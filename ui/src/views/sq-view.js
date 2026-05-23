/*
 * SQ (PSG) section view — 08-ui-views.md view 2 (Task 23 rewrite).
 *
 * Four envelope panels (3 tone + 1 noise) using the same `operator-panel`
 * + `adsr-graph` widgets the FM bottom row uses, plus a section-header band
 * with `PSG MIX` + `LAYER`. The per-channel envelope is a **software** ADSR
 * computed in `SN76489Engine` and applied as a 0..1 multiplier against the
 * chip's mix gain — there is no envelope hardware on the SN76489.
 *
 * Bindings:
 *   - Envelope (per channel n ∈ {ch1, ch2, ch3, noise}):
 *       ATK→psg_atk_n, DR1→psg_dr1_n, SUS→psg_sus_n, DR2→psg_dr2_n,
 *       RR→psg_rr_n, DETUNE→psg_detune_n, FREQ→psg_freq_n,
 *       ENV SCALE→psg_ksr_n, LFO (AMON slot)→psg_vel_n, SSG→psg_ssg_n.
 *   - Noise extras: SHFT→psg_noise_rate, PERIODIC→psg_noise_type;
 *     TYPE→psg_noise_type, RATE→psg_noise_rate, AUTO→psg_noise_auto.
 *   - PSG mix / layer → psg_mix / psg_layer (section-header band).
 *   - Per-instrument routing (MIDI, transpose, range, detune, balance) is
 *     owned by the Task 22 rack — the panel itself no longer carries those.
 *
 * The `note` readout / per-panel MIDI step-field that the pre-Task-23 layout
 * had move to the rack routing strip in the center column (08-ui-views.md
 * view 1 revised).
 */

import {
  Knob, Slider, Toggle, LedReadout, SectionTabs, OperatorPanel,
  bindSlider, bindToggle, bindCombo,
} from "../widgets/index.js";

const CHANNELS = [
  { id: "ch1",   label: "TONE 1", badge: "1" },
  { id: "ch2",   label: "TONE 2", badge: "2" },
  { id: "ch3",   label: "TONE 3", badge: "3" },
  { id: "noise", label: "NOISE",  badge: "N", noise: true },
];

export function mountSqView(host) {
  host.innerHTML = "";
  host.className = "sq-section";

  // Section-header band: PSG MIX slider + LAYER toggle, plus the section title.
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

  // Four envelope panels (3 tone + 1 noise). Each reuses the FM `OperatorPanel`
  // widget unchanged — the bindings are remapped to per-channel PSG params so
  // ATK/DR1/SUS/DR2/RR drive the software envelope and the rest of the
  // controls are visual stubs that align with the FM operator vocabulary.
  const panels = document.createElement("div");
  panels.className = "sq-panels";
  host.appendChild(panels);

  for (const ch of CHANNELS)
    panels.appendChild(makeChannelPanel(ch));
}

function makeChannelPanel(ch) {
  const wrap = document.createElement("div");
  wrap.className = "sq-op-wrap bevel-raised" + (ch.noise ? " sq-noise-wrap" : "");

  // Channel-label band on top — the OperatorPanel widget itself renders only
  // a numeric badge, so the textual "TONE 1 / NOISE" label is added here.
  const labelRow = document.createElement("div");
  labelRow.className = "sq-op-label";
  const labelText = document.createElement("span");
  labelText.className = "label";
  labelText.textContent = ch.label;
  labelRow.appendChild(labelText);
  wrap.appendChild(labelRow);

  // OperatorPanel host — the FM widget styles itself with `.op-panel` so the
  // four envelope panels share the chassis + green-LCD look of the FM row.
  const opHost = document.createElement("div");
  opHost.className = "sq-op-host";
  wrap.appendChild(opHost);

  new OperatorPanel(opHost, {
    opNumber: ch.badge,
    bindings: {
      ar:   bindSlider(`psg_atk_${ch.id}`),
      dr:   bindSlider(`psg_dr1_${ch.id}`),
      sl:   bindSlider(`psg_sus_${ch.id}`),
      sr:   bindSlider(`psg_dr2_${ch.id}`),
      rr:   bindSlider(`psg_rr_${ch.id}`),
      dt:   bindSlider(`psg_detune_${ch.id}`),
      mul:  bindSlider(`psg_freq_${ch.id}`),
      ks:   bindSlider(`psg_ksr_${ch.id}`),
      amon: bindSlider(`psg_vel_${ch.id}`),
      ssg:  bindSlider(`psg_ssg_${ch.id}`),
    },
  });

  if (ch.noise) {
    wrap.appendChild(makeNoiseExtras());
  }

  return wrap;
}

// Noise-only footer with the "SN76489 · SHFT · PERIODIC" branding strip and
// the TYPE / RATE / AUTO controls. SHFT and PERIODIC are alternate display
// surfaces for the existing `psg_noise_rate` and `psg_noise_type` apvts
// params — kept alongside the canonical TYPE / RATE choice cells per task
// scope so neither display style is the only entry point.
function makeNoiseExtras() {
  const extras = document.createElement("div");
  extras.className = "sq-noise-extras";

  // Branding + alternate-display strip.
  const strip = document.createElement("div");
  strip.className = "sq-noise-strip";

  const brand = document.createElement("span");
  brand.className = "label sq-noise-brand";
  brand.textContent = "SN76489";
  strip.appendChild(brand);

  // SHFT — a small knob bound to the 4-state `psg_noise_rate` choice. The
  // Knob widget speaks the SliderBinding interface, but the param is a
  // ComboBox on the backend, so we wrap the combo binding in a small
  // slider-shaped adapter (the only-once-here cost is < 20 lines).
  const shftCell = document.createElement("div");
  shftCell.className = "sq-noise-knob-cell";
  const shftCanvas = document.createElement("canvas");
  shftCanvas.width = 26; shftCanvas.height = 26;
  shftCell.appendChild(shftCanvas);
  const shftLbl = document.createElement("span");
  shftLbl.className = "label";
  shftLbl.textContent = "SHFT";
  shftCell.appendChild(shftLbl);
  strip.appendChild(shftCell);
  new Knob(shftCanvas, comboAsSlider(bindCombo("psg_noise_rate")));

  // PERIODIC — small LED-style toggle, mirrors the AMON-dot pattern in
  // operator-panel.js. Bound through the same combo-as-slider adapter so
  // `binding.getScaled()` returns the choice index (0 = periodic, 1 = white)
  // and click flips it.
  const periodicCell = document.createElement("div");
  periodicCell.className = "sq-noise-toggle-cell";
  const periodicCanvas = document.createElement("canvas");
  periodicCanvas.width = 12; periodicCanvas.height = 12;
  periodicCell.appendChild(periodicCanvas);
  const periodicLbl = document.createElement("span");
  periodicLbl.className = "label";
  periodicLbl.textContent = "PERIODIC";
  periodicCell.appendChild(periodicLbl);
  strip.appendChild(periodicCell);
  mountPeriodicToggle(periodicCanvas, comboAsSlider(bindCombo("psg_noise_type")));

  extras.appendChild(strip);

  // Canonical TYPE / RATE / AUTO row — wider, labelled controls beneath the
  // brand strip for users who prefer the original choice-cell display.
  const row = document.createElement("div");
  row.className = "sq-noise-row";
  row.appendChild(makeChoiceCell("TYPE", "psg_noise_type"));
  row.appendChild(makeChoiceCell("RATE", "psg_noise_rate", { width: 110 }));
  row.appendChild(makeToggleCell("AUTO", "psg_noise_auto"));
  extras.appendChild(row);

  return extras;
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

// Combo-as-slider adapter: wraps a ComboBinding (Choice param) so it can be
// passed to widgets that expect a SliderBinding (Knob, the click-flip toggle
// helper below). Used twice in the noise extras strip — once for SHFT
// (4-choice) and once for PERIODIC (2-choice). Subscribers to the underlying
// combo's events repaint when the choice changes; widget interactions push
// back through setIndex via the normalised → index round-trip.
function comboAsSlider(combo) {
  const numChoices = () => Math.max(1, combo.getChoices().length);
  return {
    kind: "slider",
    properties: combo.properties,
    getNormalised: () => {
      const n = numChoices();
      return n > 1 ? combo.getIndex() / (n - 1) : 0;
    },
    getScaled: () => combo.getIndex(),
    setNormalised: (v) => {
      const n = numChoices();
      const clamped = Math.max(0, Math.min(1, v));
      combo.setIndex(Math.round(clamped * (n - 1)));
    },
    defaultNormalised: (fallback) =>
      typeof fallback === "number" ? fallback : 0,
    beginGesture: () => {},
    endGesture:   () => {},
    onChange:     (fn) => combo.onChange(fn),
    onProperties: (fn) => combo.onProperties(fn),
  };
}

// Tiny click-to-flip LED dot, bound to a slider-shaped binding carrying an
// int 0..1 (mirrors operator-panel.js's `_mountAmonDot` pattern so we don't
// need to invent a new widget for this 2-state choice display).
function mountPeriodicToggle(canvas, binding) {
  const ctx = canvas.getContext("2d");
  ctx.imageSmoothingEnabled = false;

  const palStyle = getComputedStyle(document.documentElement);
  const ledOn   = palStyle.getPropertyValue("--led-on").trim();
  const ledBase = palStyle.getPropertyValue("--led-base").trim();

  const render = () => {
    const v = Math.round(binding.getScaled());
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    // PERIODIC is *on* when noise type == 0 (the periodic / pitched buzz);
    // off when noise type == 1 (white noise).
    ctx.fillStyle = v === 0 ? ledOn : ledBase;
    ctx.fillRect(2, 2, 8, 8);
  };

  canvas.addEventListener("click", () => {
    const cur = Math.round(binding.getScaled());
    const next = cur === 0 ? 1 : 0;
    const props = binding.properties ?? {};
    const start = props.start ?? 0;
    const end   = props.end   ?? 1;
    const norm  = end === start ? 0 : (next - start) / (end - start);
    binding.beginGesture();
    binding.setNormalised(Math.max(0, Math.min(1, norm)));
    binding.endGesture();
  });

  binding.onChange(render);
  render();
}
