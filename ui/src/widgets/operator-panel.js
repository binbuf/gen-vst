/*
 * operator-panel — composite that lays out one FM operator's controls
 * (genny-ui.md "Bottom Row — Four Operator Panels"):
 *   1. Operator number badge + red status dot.
 *   2. Wide green-LCD ADSR envelope graph.
 *   3. Five blue knobs labelled ATK / DR1 / SUS / DR2 / RR.
 *   4. Four horizontal sliders DETUNE / FREQ / ENV SCALE / LFO+SSG, each
 *      with a small red 5x7 dot-matrix readout.
 *
 * Labels are sound-engineering shorthand; they map to the apvts patch fields:
 *   ATK  -> ar      DETUNE  -> dt
 *   DR1  -> dr      FREQ    -> mul
 *   SUS  -> sl      ENV SC. -> ks
 *   DR2  -> sr      LFO     -> amon (0/1)
 *   RR   -> rr      SSG     -> ssg  (0..15)
 *
 * The host element is created by the caller and passed in; this widget mounts
 * sub-canvases/labels inside it so the four panels share an identical layout
 * (the "shared baseline grid" called out in genny-ui.md). Bindings for the
 * named parameters are supplied by the FM-view orchestrator.
 */

import { Knob } from "./knob.js";
import { Slider } from "./slider.js";
import { LedReadout } from "./led-readout.js";
import { AdsrGraph } from "./adsr-graph.js";

const KNOB_LABELS = [
  ["ATK", "ar"],
  ["DR1", "dr"],
  ["SUS", "sl"],
  ["DR2", "sr"],
  ["RR",  "rr"],
];

const SLIDER_LABELS = [
  ["DETUNE",    "dt",  6,    false],
  ["FREQ",      "mul", 15,   false],
  ["ENV SCALE", "ks",  3,    false],
];

export class OperatorPanel {
  constructor(host, options = {}) {
    this.host = host;
    this.opNumber = options.opNumber ?? 1;   // 1..4
    this.bindings = options.bindings ?? {};   // { ar, dr, sl, sr, rr, dt, mul, ks, amon, ssg }
    this.widgets = [];

    this._build();
  }

  _build() {
    const host = this.host;
    host.classList.add("op-panel");

    // Top row — badge + status dot + label.
    const top = document.createElement("div");
    top.className = "op-top";
    const badge = document.createElement("span");
    badge.className = "badge";
    badge.textContent = String(this.opNumber);
    top.appendChild(badge);
    const dot = document.createElement("span");
    dot.className = "status-dot";
    top.appendChild(dot);
    const oplabel = document.createElement("span");
    oplabel.className = "label";
    oplabel.textContent = `OP${this.opNumber}`;
    top.appendChild(oplabel);
    host.appendChild(top);

    // ADSR graph — wide LCD inset. Sized for the 4-panel grid: each bottom
    // cell is ~232px wide minus 8px padding -> 220px content; the ADSR canvas
    // fits with a small horizontal margin.
    const envCanvas = document.createElement("canvas");
    envCanvas.className = "op-env bevel-inset";
    envCanvas.width = 200;
    envCanvas.height = 50;
    envCanvas.style.width = "200px";
    envCanvas.style.height = "50px";
    host.appendChild(envCanvas);
    const env = new AdsrGraph(envCanvas, {
      bindings: {
        ar: this.bindings.ar,
        dr: this.bindings.dr,
        sl: this.bindings.sl,
        sr: this.bindings.sr,
        rr: this.bindings.rr,
      },
    });
    this.widgets.push(env);

    // Knob row — five blue knobs + their labels.
    const knobRow = document.createElement("div");
    knobRow.className = "op-knob-row bevel-inset";
    host.appendChild(knobRow);
    for (const [labelText, paramKey] of KNOB_LABELS) {
      const col = document.createElement("div");
      col.className = "op-knob-col";
      const c = document.createElement("canvas");
      c.className = "op-knob";
      c.style.width = "30px";
      c.style.height = "30px";
      c.width = 30;
      c.height = 30;
      col.appendChild(c);
      const lab = document.createElement("span");
      lab.className = "label";
      lab.textContent = labelText;
      col.appendChild(lab);
      knobRow.appendChild(col);

      const b = this.bindings[paramKey];
      if (b) {
        const k = new Knob(c, b, { defaultNormalised: 0 });
        this.widgets.push(k);
      }
    }

    // Slider rows — three numeric sliders, then the LFO/SSG pair.
    const sliders = document.createElement("div");
    sliders.className = "op-sliders";
    host.appendChild(sliders);

    for (const [labelText, paramKey, max, signed] of SLIDER_LABELS) {
      const row = this._buildSliderRow(labelText, paramKey, max, signed);
      sliders.appendChild(row);
    }

    // LFO toggle + SSG readout — both share a single row labelled "LFO / SSG".
    const lfoSsgRow = document.createElement("div");
    lfoSsgRow.className = "op-slider-row";
    const lfoLabel = document.createElement("span");
    lfoLabel.className = "label";
    lfoLabel.textContent = "LFO";
    lfoSsgRow.appendChild(lfoLabel);
    const lfoDot = document.createElement("canvas");
    lfoDot.style.width = "12px";
    lfoDot.style.height = "12px";
    lfoDot.width = 12;
    lfoDot.height = 12;
    lfoSsgRow.appendChild(lfoDot);
    const ssgLabel = document.createElement("span");
    ssgLabel.className = "label";
    ssgLabel.textContent = "SSG";
    lfoSsgRow.appendChild(ssgLabel);
    const ssgReadoutC = document.createElement("canvas");
    ssgReadoutC.width = 36;
    ssgReadoutC.height = 18;
    lfoSsgRow.appendChild(ssgReadoutC);
    sliders.appendChild(lfoSsgRow);

    // The AMON dot uses a tiny clickable Toggle-style canvas — flips amon
    // between 0 and 1 via the bound slider parameter.
    if (this.bindings.amon) this._mountAmonDot(lfoDot, this.bindings.amon);

    if (this.bindings.ssg) {
      const ssgReadout = new LedReadout(ssgReadoutC, {
        binding: this.bindings.ssg,
        widthChars: 2,
        offWhenZero: true,
      });
      this.widgets.push(ssgReadout);
    }
  }

  _buildSliderRow(labelText, paramKey, max, signed) {
    const row = document.createElement("div");
    row.className = "op-slider-row";

    const lab = document.createElement("span");
    lab.className = "label";
    lab.textContent = labelText;
    row.appendChild(lab);

    const sliderCanvas = document.createElement("canvas");
    sliderCanvas.style.width = "60px";
    sliderCanvas.style.height = "10px";
    sliderCanvas.width = 60;
    sliderCanvas.height = 10;
    row.appendChild(sliderCanvas);

    const readoutCanvas = document.createElement("canvas");
    readoutCanvas.width = 40;
    readoutCanvas.height = 18;
    row.appendChild(readoutCanvas);

    const b = this.bindings[paramKey];
    if (b) {
      const s = new Slider(sliderCanvas, b);
      const ro = new LedReadout(readoutCanvas, {
        binding: b,
        widthChars: 3,
        signed,
      });
      this.widgets.push(s, ro);
    }
    return row;
  }

  _mountAmonDot(canvas, binding) {
    // Tiny LED toggle: bound to a slider relay carrying an int 0..1. Click
    // flips it, the indicator dot lights when on.
    const ctx = canvas.getContext("2d");
    ctx.imageSmoothingEnabled = false;

    const palStyle = getComputedStyle(document.documentElement);
    const ledOn   = palStyle.getPropertyValue("--led-on").trim();
    const ledBase = palStyle.getPropertyValue("--led-base").trim();

    const render = () => {
      const v = Math.round(binding.getScaled());
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = v >= 1 ? ledOn : ledBase;
      ctx.fillRect(2, 2, 8, 8);
    };

    canvas.addEventListener("click", () => {
      const cur = Math.round(binding.getScaled());
      const next = cur >= 1 ? 0 : 1;
      const props = binding.properties ?? {};
      const start = props.start ?? 0;
      const end = props.end ?? 1;
      const norm = end === start ? 0 : (next - start) / (end - start);
      binding.beginGesture();
      binding.setNormalised(Math.max(0, Math.min(1, norm)));
      binding.endGesture();
    });

    const off = binding.onChange(render);
    this.widgets.push({ destroy: () => off?.() });
    render();
  }

  destroy() {
    for (const w of this.widgets) w?.destroy?.();
    this.widgets = [];
  }
}
