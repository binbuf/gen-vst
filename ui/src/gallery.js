// Widget gallery — one section per widget kind in ui/src/widgets/, each
// mounted against a scratch apvts param (gallery_* declared in
// createParameterLayout). Each section shows:
//   - The widget itself.
//   - A "VALUE:" readout that mirrors the bound parameter's normalised value,
//     proving the two-way binding works (changing the value in the host's
//     generic editor moves the readout; widget interaction updates the host
//     automation lane).
//
// Page is dev-only in spirit (mostly opened via `npm run dev` or
// `localhost:5173/gallery.html` in a separate browser tab), but it ships in
// the embedded bundle too via the Vite multi-page entries — opening
// /gallery.html inside the WebView (e.g. via GENVST_DEV_PAGE=gallery.html
// in dev builds) works the same way.

import "./styles/design-system.css";

import {
  bindSlider,
  bindToggle,
  bindCombo,
  emitBackendEvent,
} from "./binding.js";

import { mount as mountKnob }       from "./widgets/knob.js";
import { mount as mountDecKnob }    from "./widgets/decimator-knob.js";
import { mount as mountButton }     from "./widgets/button.js";
import { mount as mountStepper }    from "./widgets/stepper.js";
import { mount as mountLcd }        from "./widgets/lcd-readout.js";
import { mount as mountPatchLcd }   from "./widgets/patch-name-lcd.js";
import { mount as mountToggle }     from "./widgets/toggle-switch.js";
import { mount as mountSlider }     from "./widgets/slider.js";
import { mount as mountAlgoGrid }   from "./widgets/algo-grid.js";
import { mount as mountAlgoMini }   from "./widgets/algorithm-mini.js";
import { mount as mountEnvelope }   from "./widgets/envelope-curve.js";
import { mount as mountNoteOn }     from "./widgets/note-on-led.js";
import { mount as mountLevelMeter } from "./widgets/level-meter.js";
import { mount as mountOpBadge }    from "./widgets/op-badge.js";
import { mount as mountMidiWheel }  from "./widgets/midi-wheel.js";
import { mount as mountToast }      from "./widgets/notification-toast.js";
import { installTooltips }          from "./widgets/tooltip.js";

// --- Gallery layout chrome (page-local; no v2 design pretension) -----------
function ensureGalleryStyles() {
  const style = document.createElement("style");
  style.textContent = `
    html, body { width: 100% !important; height: auto !important; min-height: 100vh; overflow: auto !important; }
    body {
      background: #18191c;
      color: var(--label-text);
      padding: 24px;
      font-family: "IBM Plex Mono", monospace;
    }
    #gallery-root {
      max-width: 1200px;
      margin: 0 auto;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 16px;
    }
    .gw {
      background: var(--inset-bg);
      border: 1px solid var(--inset-edge-dark);
      border-radius: 4px;
      box-shadow: inset 1px 1px 0 var(--inset-edge-light),
                  inset 2px 2px 6px rgba(0,0,0,0.6);
      padding: 14px 14px 16px;
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    .gw h2 {
      font: 500 11px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .gw .gw-body {
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 96px;
      padding: 8px 0;
    }
    .gw .gw-readout {
      font: 500 10px/1 "IBM Plex Mono", monospace;
      color: var(--lcd-text-on);
      text-shadow: 0 0 4px var(--lcd-text-glow);
      opacity: 0.92;
      text-align: center;
    }
    .gw .gw-controls {
      display: flex;
      gap: 8px;
      justify-content: center;
      flex-wrap: wrap;
    }
    .gw-page-title {
      grid-column: 1 / -1;
      text-align: center;
      font: 700 18px/1.4 "IBM Plex Mono", monospace;
      letter-spacing: 0.15em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.9;
      padding-bottom: 4px;
    }
  `;
  document.head.appendChild(style);
}

function makeSection(title) {
  const wrap = document.createElement("section");
  wrap.className = "gw";
  const h = document.createElement("h2");
  h.textContent = title;
  const body = document.createElement("div");
  body.className = "gw-body";
  const readout = document.createElement("div");
  readout.className = "gw-readout";
  readout.textContent = "—";
  wrap.appendChild(h);
  wrap.appendChild(body);
  wrap.appendChild(readout);
  return { wrap, body, readout };
}

function makeReadout(bind, formatter = (v) => `value: ${v.toFixed(3)}`) {
  // Returns a function that the section's readout cell uses for live updates.
  return (el) => bind.onChange((v) => { el.textContent = formatter(v); });
}

function init() {
  ensureGalleryStyles();
  const root = document.getElementById("gallery-root");

  const title = document.createElement("div");
  title.className = "gw-page-title";
  title.textContent = "Gen VST — Widget Gallery";
  root.appendChild(title);

  // -------------------------------------------------------------- knob
  {
    const s = makeSection("knob — gallery_knob_a");
    const bind = bindSlider("gallery_knob_a");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountKnob(host, { bind, tipId: "gallery_knob_a" });
    makeReadout(bind)(s.readout);
    root.appendChild(s.wrap);
  }

  // ----------------------------------------------------- decimator-knob
  {
    const s = makeSection("decimator-knob — gallery_knob_b");
    const bind = bindSlider("gallery_knob_b");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountDecKnob(host, { bind, tipId: "gallery_knob_b" });
    makeReadout(bind)(s.readout);
    root.appendChild(s.wrap);
  }

  // ------------------------------------------------------------- button
  {
    const s = makeSection("button (toggle-bound) — gallery_toggle_a");
    const bind = bindToggle("gallery_toggle_a");
    const host = document.createElement("button");
    host.type = "button";
    s.body.appendChild(host);
    mountButton(host, { bind, label: "ARM", tipId: "gallery_toggle_a" });
    bind.onChange((on) => {
      s.readout.textContent = on ? "active" : "inactive";
    });
    root.appendChild(s.wrap);
  }

  // ------------------------------------------------------------ stepper
  {
    const s = makeSection("stepper — gallery_stepper (0..999)");
    const bind = bindSlider("gallery_stepper");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountStepper(host, { bind, tipId: "gallery_stepper" });
    bind.onChange((norm) => {
      const start = bind.state.properties.start || 0;
      const end = bind.state.properties.end || 999;
      const intVal = Math.round(start + norm * (end - start));
      s.readout.textContent = `value: ${intVal}`;
    });
    root.appendChild(s.wrap);
  }

  // -------------------------------------------------------- lcd-readout
  {
    const s = makeSection("lcd-readout — driven by gallery_knob_c");
    const bind = bindSlider("gallery_knob_c");
    const host = document.createElement("div");
    s.body.appendChild(host);
    const ctrl = mountLcd(host, { width: 96, height: 24, fontPx: 14 });
    bind.onChange((v) => {
      const txt = (v * 1000).toFixed(0).padStart(4, "0");
      ctrl.setText(txt);
      s.readout.textContent = `value: ${v.toFixed(3)}`;
    });
    root.appendChild(s.wrap);
  }

  // ----------------------------------------------------- patch-name-lcd
  {
    const s = makeSection("patch-name-lcd — combo-driven label");
    const bind = bindCombo("gallery_combo_a");
    const host = document.createElement("div");
    s.body.appendChild(host);
    const ctrl = mountPatchLcd(host);
    bind.onChange((idx) => {
      const choices = bind.choices();
      const text = choices.length > idx ? choices[idx] : `Item ${idx}`;
      ctrl.setText(text.toUpperCase());
      s.readout.textContent = `index: ${idx}`;
    });
    root.appendChild(s.wrap);
  }

  // ------------------------------------------------------- toggle-switch
  {
    const s = makeSection("toggle-switch — gallery_toggle_b");
    const bind = bindToggle("gallery_toggle_b");
    const host = document.createElement("span");
    s.body.appendChild(host);
    mountToggle(host, { bind, tipId: "gallery_toggle_b" });
    bind.onChange((on) => { s.readout.textContent = on ? "on" : "off"; });
    root.appendChild(s.wrap);
  }

  // ------------------------------------------- toggle-switch (two-way)
  {
    const s = makeSection("toggle-switch (two-way) — gallery_toggle_c");
    const bind = bindToggle("gallery_toggle_c");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountToggle(host, {
      bind,
      variant: "two-way",
      labels: ["LEGACY", "CRYSTAL CLEAR"],
      tipId: "gallery_toggle_c",
    });
    bind.onChange((on) => {
      s.readout.textContent = on ? "CRYSTAL CLEAR" : "LEGACY";
    });
    root.appendChild(s.wrap);
  }

  // -------------------------------------------------------------- slider
  {
    const s = makeSection("slider — gallery_knob_d");
    const bind = bindSlider("gallery_knob_d");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountSlider(host, { bind, tipId: "gallery_knob_d" });
    makeReadout(bind)(s.readout);
    root.appendChild(s.wrap);
  }

  // -------------------------------------------- algo-grid + algorithm-mini
  {
    const s = makeSection("algo-grid + algorithm-mini — gallery_algo (0..7)");
    s.body.style.gap = "16px";

    const gridHost = document.createElement("div");
    const miniHost = document.createElement("div");
    s.body.appendChild(gridHost);
    s.body.appendChild(miniHost);

    const bind = bindSlider("gallery_algo");
    const mini = mountAlgoMini(miniHost, { size: 112 });
    mountAlgoGrid(gridHost, {
      bind,
      onSelect: (idx) => mini.setAlgorithm(idx),
      tipId: "gallery_algo",
    });
    bind.onChange((norm) => {
      const start = bind.state.properties.start || 0;
      const end = bind.state.properties.end || 7;
      const idx = Math.round(start + norm * (end - start));
      s.readout.textContent = `algorithm: ${idx + 1}`;
    });
    root.appendChild(s.wrap);
  }

  // ----------------------------------------------------- envelope-curve
  {
    const s = makeSection("envelope-curve — driven by gallery knobs a..d");
    const host = document.createElement("div");
    s.body.appendChild(host);
    const env = mountEnvelope(host, { width: 240, height: 110 });

    // The 5 envelope values: AR/DR/SL/SR/RR. Spread the four gallery knobs
    // across them so dragging any knob redraws the curve; SR shares with
    // SL knob for visibility.
    const a = bindSlider("gallery_knob_a");
    const b = bindSlider("gallery_knob_b");
    const c = bindSlider("gallery_knob_c");
    const d = bindSlider("gallery_knob_d");
    const state = { ar: 25, dr: 10, sl: 6, sr: 6, rr: 7 };
    const apply = () => env.setEnvelope(state.ar, state.dr, state.sl, state.sr, state.rr);
    a.onChange((v) => { state.ar = Math.round(v * 31); apply(); });
    b.onChange((v) => { state.dr = Math.round(v * 31); apply(); });
    c.onChange((v) => { state.sl = Math.round(v * 15); state.sr = Math.round(v * 31); apply(); });
    d.onChange((v) => { state.rr = Math.round(v * 15); apply(); });
    s.readout.textContent = "drag knobs a..d to reshape";
    root.appendChild(s.wrap);
  }

  // ------------------------------------------------ note-on-led (cluster)
  {
    const s = makeSection("note-on-led — gallery_noteon");
    const bind = bindToggle("gallery_noteon");
    const host = document.createElement("div");
    s.body.appendChild(host);
    mountNoteOn(host, { bind, tipId: "gallery_noteon" });

    // Manual toggle so the user can light the LED without needing a real
    // note-on. Sits below the LED so the gallery is self-contained.
    const ctrls = document.createElement("div");
    ctrls.className = "gw-controls";
    const toggleBtn = document.createElement("button");
    toggleBtn.type = "button";
    ctrls.appendChild(toggleBtn);
    s.wrap.insertBefore(ctrls, s.readout);
    mountButton(toggleBtn, { bind, label: "Light LED" });

    bind.onChange((on) => { s.readout.textContent = on ? "lit" : "dark"; });
    root.appendChild(s.wrap);
  }

  // ------------------------------------------------------------ level-meter
  {
    const s = makeSection("level-meter — gallery_level (slider-driven)");
    const bind = bindSlider("gallery_level");
    const host = document.createElement("div");
    s.body.style.flexDirection = "column";
    s.body.style.gap = "8px";
    const meterHost = document.createElement("div");
    s.body.appendChild(meterHost);
    const sliderHost = document.createElement("div");
    s.body.appendChild(sliderHost);
    s.body.appendChild(host);
    const meter = mountLevelMeter(meterHost, { width: 220, height: 18 });
    mountSlider(sliderHost, { bind });
    bind.onChange((v) => {
      meter.setLevel(v);
      s.readout.textContent = `level: ${v.toFixed(3)}`;
    });
    root.appendChild(s.wrap);
  }

  // --------------------------------------------------------------- op-badge
  {
    const s = makeSection("op-badge — local UI state (not bound)");
    const host = document.createElement("div");
    host.style.display = "flex";
    host.style.gap = "6px";
    s.body.appendChild(host);
    const badges = [];
    let activeOp = 1;
    for (let i = 1; i <= 4; ++i) {
      const el = document.createElement("div");
      host.appendChild(el);
      const ctrl = mountOpBadge(el, {
        index: i,
        onClick: (idx) => {
          activeOp = idx;
          badges.forEach((b, ii) => b.setActive(ii + 1 === activeOp));
          s.readout.textContent = `active op: ${activeOp}`;
        },
      });
      badges.push(ctrl);
    }
    badges[0].setActive(true);
    s.readout.textContent = `active op: ${activeOp}`;
    root.appendChild(s.wrap);
  }

  // ----------------------------------------------------- midi-wheel (PB / MW)
  {
    const s = makeSection("midi-wheel — pb + mw variants (gallery_wheel)");
    s.body.style.gap = "20px";
    const bind = bindSlider("gallery_wheel");

    const pbCell = document.createElement("div");
    pbCell.className = "midi-wheel-cell";
    const pbWheel = document.createElement("span");
    pbCell.appendChild(pbWheel);
    const pbLabel = document.createElement("span");
    pbLabel.className = "wheel-label";
    pbLabel.textContent = "PB";
    pbCell.appendChild(pbLabel);

    const mwCell = document.createElement("div");
    mwCell.className = "midi-wheel-cell";
    const mwWheel = document.createElement("span");
    mwCell.appendChild(mwWheel);
    const mwLabel = document.createElement("span");
    mwLabel.className = "wheel-label";
    mwLabel.textContent = "MW";
    mwCell.appendChild(mwLabel);

    s.body.appendChild(pbCell);
    s.body.appendChild(mwCell);

    mountMidiWheel(pbWheel, { bind, variant: "pb", tipId: "gallery_wheel" });
    mountMidiWheel(mwWheel, { bind, variant: "mw", tipId: "gallery_wheel" });

    // Drag-controllable slider so the user can move the read-only wheels.
    const ctrls = document.createElement("div");
    ctrls.className = "gw-controls";
    const sliderHost = document.createElement("div");
    ctrls.appendChild(sliderHost);
    s.wrap.insertBefore(ctrls, s.readout);
    mountSlider(sliderHost, { bind });

    bind.onChange((v) => { s.readout.textContent = `value: ${v.toFixed(3)}`; });
    root.appendChild(s.wrap);
  }

  // -------------------------------------------------- notification-toast
  {
    const s = makeSection("notification-toast — synthetic notify events");
    const ctrls = document.createElement("div");
    ctrls.className = "gw-controls";
    const mkBtn = (label, level, message) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "btn";
      b.textContent = label;
      b.addEventListener("click", () => {
        emitBackendEvent("notify", { level, message });
      });
      ctrls.appendChild(b);
    };
    mkBtn("Info",    "info",    "Patch loaded.");
    mkBtn("Warning", "warning", "Patch loaded with defaults.");
    mkBtn("Error",   "error",   "Patch failed to parse.");
    s.body.appendChild(ctrls);
    s.readout.textContent = "click a button -> toast bottom-right (stack 2, queue extras)";
    root.appendChild(s.wrap);
  }

  // ---------------------------------------------- mount global facilities
  // Toast host — same code path the chassis uses.
  const toastHost = document.createElement("div");
  document.body.appendChild(toastHost);
  mountToast(toastHost);

  // Tooltips installed on the gallery root.
  installTooltips(root);

  // Signal mount for symmetry with index.js.
  if (window.__JUCE__ && window.__JUCE__.backend) {
    window.__JUCE__.backend.emitEvent("uiReady", {});
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
