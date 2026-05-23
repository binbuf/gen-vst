/*
 * Widget gallery — Task 10 dev page.
 *
 * Mounts every core widget against the dev-server scratch apvts parameters
 * (declared in PluginProcessor.cpp under GENVST_DEV_SERVER, relayed by
 * PluginEditor.cpp under the same guard). The page is *only* useful in the
 * dev-server build:
 *   * In the embedded-bundle build, the gallery_* relays don't exist, so
 *     bindings here would print "unknown to the backend" warnings.
 *   * In a browser standalone tab (no plugin), check_native_interop.js fills
 *     in a no-op JUCE shim. Widgets still render and respond to clicks (the
 *     shim echoes set/get back), so this is the fastest visual-iteration loop.
 *
 * Each section shows: a label, the widget canvas itself, and a small status
 * line showing the live scaled value (for visible confirmation of two-way
 * binding when the parameter is moved from the DAW).
 */

import "./styles/design-system.css";
import "./styles/gallery.css";
import {
  bindSlider, bindToggle, bindCombo,
} from "./binding.js";
import { Knob } from "./widgets/knob.js";
import { Slider } from "./widgets/slider.js";
import { LedReadout } from "./widgets/led-readout.js";
import { StepField } from "./widgets/step-field.js";
import { Toggle } from "./widgets/toggle.js";
import { SectionTabs } from "./widgets/section-tabs.js";
import { LcdList } from "./widgets/lcd-list.js";

function section(host, title, build) {
  const wrap = document.createElement("section");
  wrap.className = "gallery-section";
  const h = document.createElement("div");
  h.className = "label";
  h.textContent = title;
  wrap.appendChild(h);
  const slot = document.createElement("div");
  slot.className = "gallery-slot";
  wrap.appendChild(slot);
  const status = document.createElement("div");
  status.className = "gallery-status";
  wrap.appendChild(status);
  host.appendChild(wrap);
  build(slot, status);
  return wrap;
}

function newCanvas(w, h, slot) {
  const c = document.createElement("canvas");
  c.style.width = w + "px";
  c.style.height = h + "px";
  c.width = w;
  c.height = h;
  slot.appendChild(c);
  return c;
}

function wireStatus(status, binding, format) {
  const update = () => { status.textContent = format(binding); };
  binding.onChange(update);
  binding.onProperties?.(update);
  update();
}

function mount() {
  const grid = document.getElementById("gallery-grid");

  /* ---- Knob ---- */
  section(grid, "knob — gallery_knob", (slot, status) => {
    const c = newCanvas(64, 64, slot);
    const b = bindSlider("gallery_knob");
    new Knob(c, b, { defaultNormalised: 0.5 });
    wireStatus(status, b, x => x.getScaled().toFixed(3));
  });

  /* ---- Slider + led-readout pair ---- */
  section(grid, "slider — gallery_slider", (slot, status) => {
    const c = newCanvas(160, 16, slot);
    const r = newCanvas(50, 18, slot);
    const b = bindSlider("gallery_slider");
    new Slider(c, b, { defaultNormalised: 0 });
    new LedReadout(r, {
      binding: b,
      widthChars: 3,
      format: (s) => Math.round(s * 100).toString(),
    });
    wireStatus(status, b, x => x.getScaled().toFixed(3));
  });

  /* ---- led-readout standalone (negative + OFF) ---- */
  section(grid, "led-readout — gallery_readout (signed)", (slot, status) => {
    const r = newCanvas(60, 18, slot);
    const b = bindSlider("gallery_readout");
    new LedReadout(r, {
      binding: b,
      widthChars: 3,
      signed: true,
      offWhenZero: true,
    });
    wireStatus(status, b, x => x.getScaled().toFixed(0));
  });

  /* ---- step-field ---- */
  section(grid, "step-field — gallery_step", (slot, status) => {
    const c = newCanvas(60, 20, slot);
    const b = bindSlider("gallery_step");
    new StepField(c, b, { widthChars: 2 });
    wireStatus(status, b, x => x.getScaled().toFixed(0));
  });

  /* ---- toggle (LED button) ---- */
  section(grid, "toggle — gallery_toggle", (slot, status) => {
    const c = newCanvas(24, 24, slot);
    const b = bindToggle("gallery_toggle");
    new Toggle(c, b);
    wireStatus(status, b, x => (x.getValue() ? "ON" : "OFF"));
  });

  /* ---- section-tabs (FM / SQ / D) ---- */
  section(grid, "section-tabs (pill) — gallery_section", (slot, status) => {
    const c = newCanvas(120, 16, slot);
    const b = bindCombo("gallery_section");
    new SectionTabs(c, b, { style: "pill" });
    wireStatus(status, b, x =>
      `[${x.getIndex()}] ${x.getChoices()[x.getIndex()] ?? ""}`);
  });

  /* ---- pill-buttons / tabs style ---- */
  section(grid, "section-tabs (tab) — gallery_tabs", (slot, status) => {
    const c = newCanvas(150, 16, slot);
    const b = bindCombo("gallery_tabs");
    new SectionTabs(c, b, { style: "tab" });
    wireStatus(status, b, x =>
      `[${x.getIndex()}] ${x.getChoices()[x.getIndex()] ?? ""}`);
  });

  /* ---- lcd-list ---- */
  section(grid, "lcd-list — gallery_list", (slot, status) => {
    const c = newCanvas(220, 120, slot);
    const b = bindCombo("gallery_list");
    new LcdList(c, { binding: b });
    wireStatus(status, b, x =>
      `[${x.getIndex()}] ${x.getChoices()[x.getIndex()] ?? ""}`);
  });

  /* ---- Scale picker — emulates the integer-scale UI selector (Task 17). The
   * gallery uses CSS transform to zoom because we don't have the plugin's
   * scale machinery yet, but the result is exactly what crisp 2x/3x must look
   * like: clean integer multiplication of every dot. */
  const root = document.documentElement;
  const applyScale = (n) => {
    document.body.style.transform = `scale(${n})`;
    document.body.style.transformOrigin = "top left";
    document.body.dataset.scale = String(n);
  };
  for (const btn of document.querySelectorAll(".scale-picker button"))
    btn.addEventListener("click", () => applyScale(Number(btn.dataset.scale)));
  applyScale(1);

  // Signal mount, mirroring the main page's lifecycle so the editor logs
  // "uiReady received" when it loads the gallery.
  window.__JUCE__.backend.emitEvent("uiReady", { page: "gallery" });
}

if (document.readyState === "loading")
  window.addEventListener("DOMContentLoaded", mount);
else mount();
