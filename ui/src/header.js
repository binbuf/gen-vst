// Persistent header — `08-ui-views.md` view 1.
//
// The only persistent region in v2 (the v2 first-pass bottom status bar was
// removed during the post-mockup review; its L/R meters moved here, its
// version string moved into the About modal).
//
// Order, left to right (matches the .hdr grid template in design-system.css):
//
//   NOTE ON cluster  ·  GEN VST wordmark  ·  FM/SQ/D pill  ·  patch cluster
//   (◀ LCD ▶ 📂)     ·  Output filter switch  ·  Ladder rocker
//   ·  stacked L/R meters  ·  DAC PRESC knob  ·  VOL knob  ·  TIPS toggle
//   ·  gear icon
//
// Every interactive control routes through binding.js relays — apvts IDs
// match `createParameterLayout()` in src/PluginProcessor.cpp.

import {
  bindSlider,
  bindToggle,
  bindCombo,
} from "./binding.js";

import { mount as mountNoteOn }       from "./widgets/note-on-led.js";
import { mount as mountPatchNameLcd } from "./widgets/patch-name-lcd.js";
import { mount as mountKnob }         from "./widgets/knob.js";
import { mount as mountLevelMeter }   from "./widgets/level-meter.js";
import { mount as mountToggle }       from "./widgets/toggle-switch.js";
import { applyTooltip }               from "./widgets/tooltip-content.js";

import { getNativeFunction } from "./juce/index.js";

const MODE_FM = 0;
const MODE_SQ = 1;
const MODE_D  = 2;

// View 1 LCD placeholder text — `AUDIO FX` when the user is in D mode, blank
// (em-dash sentinel) otherwise until Task 09's preset browser hands over a
// patch name.
const D_LCD_PLACEHOLDER  = "AUDIO FX";
const IDLE_LCD_PLACEHOLDER = "— EMPTY —";

function ensureStyles() {
  if (document.getElementById("genvst-header-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-header-style";
  style.textContent = `
    .hdr {
      /* design-system.css already sizes the .hdr grid (88 px tall,
       * 11-column grid template). These rules style the inner cells. */
      color: var(--label-text);
    }
    .hdr .hdr-cell {
      display: inline-flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .hdr .hdr-cell .hdr-cap {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .hdr .hdr-wordmark {
      cursor: pointer;
      padding: 4px 6px;
    }
    .hdr .hdr-mode-pill { display: inline-flex; }
    .hdr .hdr-mode-pill .btn { padding: 5px 10px; }
    .hdr .hdr-patch {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      min-width: 0;
    }
    .hdr .hdr-patch.is-disabled {
      opacity: 0.45;
      pointer-events: none;
    }
    .hdr .hdr-patch .lcd-patch-host {
      flex: 1 1 auto;
    }
    .hdr .hdr-output {
      display: inline-flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .hdr .hdr-output .hdr-output-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .hdr .hdr-meters {
      display: inline-flex;
      flex-direction: column;
      gap: 2px;
    }
    .hdr .hdr-meters .meter-row {
      display: inline-flex;
      align-items: center;
      gap: 4px;
    }
    .hdr .hdr-meters .meter-row .meter-tag {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      color: var(--label-text);
      opacity: 0.75;
      width: 6px;
    }
    .hdr .ladder-rocker.is-disabled,
    .hdr .filter-switch.is-disabled,
    .hdr .dac-cell.is-disabled {
      opacity: 0.35;
      pointer-events: none;
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

// Build & mount the header into `host` (the <header class="hdr inset"> in
// index.html). Caller wires the gear icon's onClick + the wordmark's onClick
// to the Settings / About modals respectively (main.js handles that).
export function mount(host, opts = {}) {
  const {
    onOpenSettings = () => {},
    onOpenAbout    = () => {},
    onOpenBrowser  = () => {},
  } = opts;

  ensureStyles();
  host.classList.add("hdr");
  host.innerHTML = "";

  // --- 1. NOTE ON cluster --------------------------------------------------
  const noteOnHost = el("div");
  const noteOn = mountNoteOn(noteOnHost, {
    fromTelemetry: true,
    caption: "NOTE ON",
  });
  host.appendChild(noteOnHost);

  // --- 2. GEN VST wordmark ------------------------------------------------
  const wordmark = el("div", {
    className: "hdr-wordmark t-wordmark",
    text: "GEN VST",
  });
  wordmark.addEventListener("click", () => onOpenAbout());
  applyTooltip(wordmark, "wordmark");
  host.appendChild(wordmark);

  // --- 3. FM / SQ / D mode pill ------------------------------------------
  const modePillWrap = el("div", { className: "hdr-mode-pill btn-pill" });
  const modeCombo = bindCombo("mode_select");
  const modeBtns = ["FM", "SQ", "D"].map((label, idx) => {
    const btn = el("button", { className: "btn" });
    btn.type = "button";
    btn.textContent = label;
    btn.addEventListener("click", () => modeCombo.setIndex(idx));
    modePillWrap.appendChild(btn);
    return btn;
  });
  applyTooltip(modePillWrap, "mode_select");
  host.appendChild(modePillWrap);

  // --- 4. Patch cluster (◀ LCD ▶ 📂) -------------------------------------
  const patchWrap = el("div", { className: "hdr-patch" });
  const prevBtn  = el("button", { className: "icon-btn", text: "◀" });
  prevBtn.type = "button";
  const lcdHost  = el("div", { className: "lcd lcd-patch lcd-patch-host" });
  const patchLcd = mountPatchNameLcd(lcdHost, { initialText: IDLE_LCD_PLACEHOLDER });
  const nextBtn  = el("button", { className: "icon-btn", text: "▶" });
  nextBtn.type = "button";
  const browseBtn = el("button", { className: "icon-btn", text: "📂" });
  browseBtn.type = "button";

  // Patch nav stubs — Task 09 supplies the real PatchSystem extension. For
  // now wire the buttons to the native `patchNav` function (which returns
  // the current path unchanged in this task) and to the browser opener (a
  // no-op toast until Task 09).
  let patchNavFn = null;
  try {
    patchNavFn = getNativeFunction("patchNav");
  } catch (e) {
    patchNavFn = null;
  }
  prevBtn.addEventListener("click", () => { if (patchNavFn) patchNavFn(-1); });
  nextBtn.addEventListener("click", () => { if (patchNavFn) patchNavFn(+1); });
  browseBtn.addEventListener("click", () => onOpenBrowser());

  applyTooltip(prevBtn,   "patch_prev");
  applyTooltip(nextBtn,   "patch_next");
  applyTooltip(browseBtn, "patch_browse");

  patchWrap.appendChild(prevBtn);
  patchWrap.appendChild(lcdHost);
  patchWrap.appendChild(nextBtn);
  patchWrap.appendChild(browseBtn);
  host.appendChild(patchWrap);

  // --- 5. Output Filter switch -------------------------------------------
  const filterWrap = el("div", { className: "hdr-output filter-switch" });
  filterWrap.appendChild(el("div", { className: "hdr-output-label", text: "OUTPUT" }));
  const filterHost = el("div");
  filterWrap.appendChild(filterHost);
  const filterBind = bindToggle("output_filter");
  mountToggle(filterHost, {
    bind: filterBind,
    variant: "two-way",
    labels: ["CRYSTAL CLEAR", "LEGACY"],
    tipId: "output_filter",
  });
  host.appendChild(filterWrap);

  // --- 6. Ladder Effect rocker -------------------------------------------
  const ladderWrap = el("div", { className: "hdr-output ladder-rocker" });
  ladderWrap.appendChild(el("div", { className: "hdr-output-label", text: "LADDER" }));
  const ladderHost = el("div");
  ladderWrap.appendChild(ladderHost);
  const ladderBind = bindToggle("ladder_effect");
  mountToggle(ladderHost, { bind: ladderBind, tipId: "ladder_effect" });
  host.appendChild(ladderWrap);

  // --- 7. Stacked L/R meters --------------------------------------------
  const metersWrap = el("div", { className: "hdr-meters" });
  const lRow = el("div", { className: "meter-row" });
  lRow.appendChild(el("span", { className: "meter-tag", text: "L" }));
  const lHost = el("div", { className: "level-meter level-meter-mini" });
  mountLevelMeter(lHost, {
    width: 96, height: 9, segments: 20, channel: "L",
  });
  lRow.appendChild(lHost);
  metersWrap.appendChild(lRow);
  const rRow = el("div", { className: "meter-row" });
  rRow.appendChild(el("span", { className: "meter-tag", text: "R" }));
  const rHost = el("div", { className: "level-meter level-meter-mini" });
  mountLevelMeter(rHost, {
    width: 96, height: 9, segments: 20, channel: "R",
  });
  rRow.appendChild(rHost);
  metersWrap.appendChild(rRow);
  host.appendChild(metersWrap);

  // --- 8. DAC PRESCALER knob (mode-aware binding) ------------------------
  const dacCell = el("div", { className: "hdr-cell dac-cell" });
  const dacHost = el("div");
  dacCell.appendChild(dacHost);
  dacCell.appendChild(el("div", { className: "hdr-cap", text: "DAC PRESC" }));
  applyTooltip(dacCell, "dac_prescaler");
  host.appendChild(dacCell);

  // bindSliderByMode: subscribe to mode_select, swap the underlying slider
  // bind on each mode change. In SQ mode no entry → grey the cell.
  let dacKnob = null;
  let dacUnbind = null;
  const dacBindMap = { [MODE_FM]: "fm_dac_prescaler", [MODE_D]: "prescaler" };
  const rebindDac = (modeIdx) => {
    if (dacKnob) { dacKnob.dispose(); dacKnob = null; }
    if (dacUnbind) { dacUnbind = null; }
    dacHost.innerHTML = "";
    const paramId = dacBindMap[modeIdx];
    if (paramId == null) {
      dacCell.classList.add("is-disabled");
      // Still draw an inert knob body so the cell doesn't collapse.
      const placeholder = el("div");
      placeholder.style.width = "32px";
      placeholder.style.height = "32px";
      placeholder.className = "knob knob-sm";
      placeholder.dataset.knobLive = "1";
      dacHost.appendChild(placeholder);
      return;
    }
    dacCell.classList.remove("is-disabled");
    const bind = bindSlider(paramId);
    dacKnob = mountKnob(dacHost, { bind, size: 32, extraClass: "knob-sm" });
    dacUnbind = bind;
  };

  // --- 9. VOL knob ------------------------------------------------------
  const volCell = el("div", { className: "hdr-cell" });
  const volHost = el("div");
  volCell.appendChild(volHost);
  volCell.appendChild(el("div", { className: "hdr-cap", text: "VOL" }));
  mountKnob(volHost, {
    bind: bindSlider("master_volume"),
    size: 32,
    extraClass: "knob-sm",
    tipId: "master_volume",
  });
  host.appendChild(volCell);

  // --- 10. TIPS toggle --------------------------------------------------
  const tipsCell = el("div", { className: "hdr-cell" });
  const tipsHost = el("div");
  tipsCell.appendChild(tipsHost);
  tipsCell.appendChild(el("div", { className: "hdr-cap", text: "TIPS" }));
  mountToggle(tipsHost, {
    bind: bindToggle("tooltips_enabled"),
    tipId: "tooltips_enabled",
  });
  host.appendChild(tipsCell);

  // --- 11. ⚙ Gear icon (open Settings) ---------------------------------
  const gearBtn = el("button", { className: "icon-btn", text: "⚙" });
  gearBtn.type = "button";
  applyTooltip(gearBtn, "settings");
  gearBtn.addEventListener("click", () => onOpenSettings());
  host.appendChild(gearBtn);

  // --- Greying + lock subscriptions -------------------------------------

  // D-mode patch cluster greying. When mode_select == D the entire ◀ LCD ▶
  // 📂 cluster becomes non-interactive and the LCD displays AUDIO FX.
  // Mirrors the SQ/D greying rules in view 1.
  const applyModeGrey = (modeIdx) => {
    const isD = (modeIdx === MODE_D);
    patchWrap.classList.toggle("is-disabled", isD);
    patchLcd.setText(isD ? D_LCD_PLACEHOLDER : IDLE_LCD_PLACEHOLDER);
    // Ladder greyed in SQ (no audible effect on the PSG path).
    const isSq = (modeIdx === MODE_SQ);
    ladderWrap.classList.toggle("is-disabled", isSq);
    // DAC PRESCALER mode-aware rebind (also greys in SQ where the map has
    // no entry).
    rebindDac(modeIdx);
  };
  const unsubMode = modeCombo.onChange((idx) => {
    applyModeGrey(idx);
    // Visual highlighting of the active pill segment.
    modeBtns.forEach((btn, i) => btn.classList.toggle("is-active", i === idx));
  });

  // HARDWARE STRICT lock: when on, both the Output Filter switch and Ladder
  // rocker are forced-on and locked. The C++ side also enforces this in the
  // audio path so a stale apvts read can't bypass strict semantics.
  const strictBind = bindToggle("hardware_strict");
  const unsubStrict = strictBind.onChange((on) => {
    const locked = Boolean(on);
    if (locked) {
      filterBind.set(true);
      ladderBind.set(true);
    }
    filterWrap.classList.toggle("is-disabled", locked);
    // Ladder lock layers on top of the SQ-mode grey — keep the SQ grey
    // active in SQ regardless of strict.
    const modeIdx = modeCombo.getIndex();
    const isSq = (modeIdx === MODE_SQ);
    ladderWrap.classList.toggle("is-disabled", locked || isSq);
  });

  return {
    setLcdText(text) { patchLcd.setText(String(text ?? IDLE_LCD_PLACEHOLDER)); },
    dispose() {
      if (unsubMode)   unsubMode();
      if (unsubStrict) unsubStrict();
      noteOn.dispose();
      if (dacKnob) dacKnob.dispose();
      host.innerHTML = "";
    },
  };
}
