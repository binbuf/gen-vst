// FM mode panel — `08-ui-views.md` view 2.
//
// Assembled from Task 04's widget library. Mounts:
//   - LFO / RATE / PMS / AMS knobs (top-left LFO block)
//   - POLY + RANGE numeric steppers
//   - LEGATO / RETRIG two-position toggle
//   - Envelope-curve widget (per-operator; click op-badge to change row)
//   - FREQ CTRL MODE 3-button pill (INT MUL / FLOAT MUL / AUTO RETRIG)
//   - RETRIG RATE stepper-readout (visible only in AUTO RETRIG)
//   - OP1 FB knob
//   - 8-button `algo-grid` + larger `algorithm-mini` topology tile
//   - GLOBAL IN: PB + MW `midi-wheel` widgets (read-only mirrors)
//   - Operator grid: 4 rows × (TL, [N], AM, AR, DR, SL, SR, RR, RS, SSG, MUL,
//     FREQ-LCD, FIXED, DT) plus a right-margin VEL knob and a top-row CH VOL
//     master knob fanning out to the per-op TLs.
//
// Every interactive control routes through binding.js relays — the apvts
// param IDs match `createParameterLayout()` in src/PluginProcessor.cpp.

import {
  bindSlider,
  bindToggle,
  bindCombo,
} from "../binding.js";

import { getNativeFunction }        from "../juce/index.js";

import { mount as mountKnob }       from "../widgets/knob.js";
import { mount as mountToggle }     from "../widgets/toggle-switch.js";
import { mount as mountStepper }    from "../widgets/stepper.js";
import { mount as mountLcd }        from "../widgets/lcd-readout.js";
import { mount as mountAlgoGrid }   from "../widgets/algo-grid.js";
import { mount as mountAlgoMini }   from "../widgets/algorithm-mini.js";
import { mount as mountEnvelope }   from "../widgets/envelope-curve.js";
import { mount as mountMidiWheel }  from "../widgets/midi-wheel.js";
import { mount as mountOpBadge }    from "../widgets/op-badge.js";

const FREQ_CTRL_MODE_INT     = 0;
const FREQ_CTRL_MODE_FLOAT   = 1;
const FREQ_CTRL_MODE_RETRIG  = 2;

// SSG-EG: 9 valid hardware states (OFF + 8 named shapes per
// 02-fm-synthesis.md *SSG-EG*). The apvts param is AudioParameterInt(0,15)
// for TFI/VGI/DMP/Y12 round-trip; this table snaps the int onto the 9 valid
// register values and surfaces a name for each. Values 1..7 are hardware-OFF
// (SSGE bit 3 is 0); the stepper's `valueSequence` snap collapses them to 0.
const SSG_VALUE_SEQUENCE = [0, 8, 9, 10, 11, 12, 13, 14, 15];
const SSG_LABELS = {
  0:  "OFF",   // SSG-EG disabled
  8:  "SDR",   // saw down, repeat
  9:  "SDO",   // saw down, one-shot
  10: "ALT",   // alternate (triangle, down-first)
  11: "SDH",   // saw down, then hold
  12: "SUR",   // saw up, repeat
  13: "SUH",   // saw up (rise), then hold
  14: "ALU",   // alternate up (triangle, up-first)
  15: "SUO",   // saw up, one-shot
};

// The four SSG-EG shapes that loop. Hardware behaviour for these requires
// AR=31 on the same operator — see ADR-0027 and 02-fm-synthesis.md
// *UI nudge — SSG-EG loop vs AR*. SDR/ALT/SUR/ALU.
const SSG_LOOPING_VALUES = new Set([8, 10, 12, 14]);

// Tooltip variants for the AR knob. The "normal" copy mirrors
// tooltip-content.js FM_OP_BASE.ar so the AR cell falls back cleanly when
// the nudge clears. The "warn" copy fires when the paired SSG-EG is on a
// looping shape and AR < 31.
const AR_TIP_NORMAL = {
  name: "AR",
  desc: "Attack rate — how quickly the envelope rises from key-on to full level. 0 slowest, 31 instant.",
};
const AR_TIP_SSG_WARN = {
  name: "AR — SSG-EG LOOP",
  desc: "SSG-EG loop needs AR=31 to sound as labelled. Raise AR to 31, or pick a non-looping SSG-EG shape (SDO / SDH / SUH / SUO).",
};

// FM panel CSS — ported from the mvp2/01 mockup (deleted in task 04 per
// docs/tasks/mvp2/01-static-ui-mockup.md). The mockup's mockup-fm.css is
// the visual source-of-truth for the v2 FM layout; this stylesheet inlines
// it scoped to .fm-panel. The mode panel splits into two CSS-grid rows:
//
//   .fm-top  (6 cols)  LFO | ENVELOPE | FREQ CTRL | RETRIG+OP1FB |
//                      ALGO PICKER | TOPOLOGY
//   .fm-mid  (3 cols)  GLOBAL IN (PB/MW) | OPERATOR GRID | VEL
//
// Block titles are absolutely-positioned at top-left of each block so the
// block content can fill the cell — matches the mockup recipe.
function ensureStyles() {
  if (document.getElementById("genvst-fm-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-fm-view-style";
  style.textContent = `
    .fm-panel {
      width: 100%;
      height: 100%;
      display: grid;
      grid-template-rows: 154px 1fr;
      gap: 6px;
      /* Mockup padding — overrides design-system.css's .mode-panel 14px
       * (same selector specificity, later wins). */
      padding: 16px 10px 8px;
      box-sizing: border-box;
    }

    /* Block chrome — inset dark surface with absolutely-positioned title. */
    .fm-panel .fm-block {
      position: relative;
      background: rgba(0, 0, 0, 0.18);
      border: 1px solid rgba(0, 0, 0, 0.4);
      border-radius: 3px;
      padding: 16px 8px 6px;
    }
    .fm-panel .fm-block-title {
      position: absolute;
      top: 4px;
      left: 8px;
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.78;
      pointer-events: none;
      z-index: 2;
      text-shadow: 0 1px 2px rgba(0, 0, 0, 0.7);
    }

    /* ---- Top row: 6 columns with fixed widths from mockup ---------- */
    .fm-panel .fm-top {
      display: grid;
      grid-template-columns: 234px 1fr 130px 96px 86px 132px;
      gap: 6px;
    }

    /* LFO/global block — 4 knobs in a row + POLY/RANGE steppers + note-mode. */
    .fm-panel .lfo-row {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 4px;
      align-items: end;
      padding-top: 0;
    }
    .fm-panel .knob-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .fm-panel .knob-cell .knob-label {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .fm-panel .stepper-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      padding-top: 4px;
    }
    .fm-panel .note-mode-left {
      align-items: flex-start;
      margin-top: 10px;
      padding-left: 2px;
    }
    .fm-panel .stepper-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
    }
    .fm-panel .stepper-cell .stepper-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }

    /* Envelope-curve block — block-title pinned, envelope canvas fills. */
    .fm-panel .fm-env {
      padding: 16px 6px 6px;
    }

    /* FREQ CTRL — 3 stacked pill buttons */
    .fm-panel .fm-freqctrl {
      display: flex;
      flex-direction: column;
      align-items: stretch;
      justify-content: center;
      gap: 4px;
      padding-top: 14px;
    }
    .fm-panel .freq-mode-pill {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .fm-panel .freq-mode-pill .btn { text-align: center; padding: 5px 8px; }

    /* RETRIG RATE + OP1 FB stacked */
    .fm-panel .fm-misc {
      display: grid;
      grid-template-rows: 1fr 1fr;
      gap: 6px;
      padding-top: 14px;
    }
    .fm-panel .fm-misc .stepper-cell,
    .fm-panel .fm-misc .knob-cell {
      align-items: center;
      justify-content: center;
    }
    .fm-panel .retrig-rate.is-disabled { opacity: 0.35; pointer-events: none; }

    /* OP1 FB → op1 connector. Decorative-only short vertical line beneath the
     * OP1 FB knob, hinting that FB affects operator 1's self-feedback only.
     * Per 08-ui-views.md view 2 "routing connector". */
    .fm-panel .op1-fb-cell { position: relative; }
    .fm-panel .op1-fb-cell::after {
      content: "";
      position: absolute;
      left: 50%;
      bottom: -10px;
      width: 1px;
      height: 14px;
      background: var(--label-text);
      opacity: 0.4;
      pointer-events: none;
    }

    /* Algorithm picker — 8-button 2x4 grid */
    .fm-panel .fm-algo-picker {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 14px 4px 4px;
    }

    /* Topology tile */
    .fm-panel .fm-algo-diagram {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: flex-start;
      gap: 3px;
      padding: 14px 4px 4px;
    }

    /* ---- Mid row: PB/MW | operator grid | VEL ---------------------- */
    .fm-panel .fm-mid {
      display: grid;
      grid-template-columns: 88px 1fr 60px;
      gap: 6px;
    }

    .fm-panel .fm-pbmw {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 8px;
      padding: 22px 4px 8px;
    }
    .fm-panel .midi-wheel-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
    }
    .fm-panel .midi-wheel-cell .wheel-label {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
    }
    .fm-panel .fm-pbmw .wheels {
      display: flex;
      flex-direction: row;
      gap: 10px;
      margin-top: 4px;
    }

    /* Operator grid — 14 columns with the first two fixed (TL anchor +
     * op-badge) and the remaining 12 sharing equally (matches mockup). */
    .fm-panel .fm-opgrid {
      padding: 22px 6px 6px 12px;
    }
    .fm-panel .op-grid {
      display: grid;
      grid-template-columns: 32px 28px repeat(12, 1fr);
      align-items: center;
      justify-items: center;
      row-gap: 5px;
      column-gap: 3px;
    }
    .fm-panel .op-grid .col-header {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text-dim);
      text-align: center;
      padding-bottom: 3px;
    }
    .fm-panel .op-grid .knob { margin: 0; }
    .fm-panel .op-grid .freq-lcd { display: flex; justify-content: center; }
    .fm-panel .op-grid .fixed-cell { display: flex; justify-content: center; }
    .fm-panel .op-grid.is-int-mul .fixed-cell { opacity: 0.35; pointer-events: none; }
    .fm-panel .op-row {
      display: contents;   /* grid items continue from parent */
    }

    /* SSG-EG cell — the stepper has to fit a ~1fr column. Tighten the
     * .stepper-readout chrome and the .stepper-btn arrows so the LCD label
     * (3 chars) stays readable without overflowing the column. */
    .fm-panel .op-grid .ssg-cell { width: 100%; max-width: 70px; }
    .fm-panel .op-grid .ssg-cell .stepper-readout { gap: 1px; padding: 1px 2px; }
    .fm-panel .op-grid .ssg-cell .stepper-btn { padding: 0 2px; font-size: 9px; }
    .fm-panel .op-grid .ssg-cell .lcd { min-width: 32px; padding: 1px 2px; }

    /* CH VOL master knob — sits above the operator grid, aligned to TL col. */
    .fm-panel .ch-vol-row {
      display: grid;
      grid-template-columns: 32px 28px 1fr;
      align-items: center;
      column-gap: 3px;
      margin-bottom: 4px;
    }

    /* VEL right-margin column — single per-op knob × 4 rows. */
    .fm-panel .fm-velblock {
      padding: 22px 4px 6px;
    }
    .fm-panel .vel-col {
      display: grid;
      grid-template-columns: 1fr;
      grid-auto-rows: auto;
      align-items: center;
      justify-items: center;
      row-gap: 5px;
    }
    .fm-panel .vel-col .col-header {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text-dim);
    }

    /* SSG-EG loop nudge — see ADR-0027 and 02-fm-synthesis.md "UI nudge —
     * SSG-EG loop vs AR". When ssg ∈ {8,10,12,14} and AR < 31, the AR knob
     * carries .ssg-ar-mismatch and paints with an amber outline + glow. No
     * audio override — visual hint only. */
    .fm-panel .op-grid .knob.ssg-ar-mismatch {
      box-shadow:
        0 0 0 1px rgba(255, 176, 64, 0.85),
        0 0 8px 1px rgba(255, 176, 64, 0.45);
      border-radius: 50%;
    }

    /* KIT entry button — top-right of the FM panel; opens the drum-kit view
     * (ADR-0021 amendment). Absolutely positioned so it doesn't disturb the
     * fixed two-row grid. */
    .fm-panel .fm-kit-enter {
      position: absolute; top: 18px; right: 14px; z-index: 3;
      padding: 4px 10px;
    }
  `;
  document.head.appendChild(style);
}

// Small DOM-element helper to keep the build readable.
function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  if (opts.style)     Object.assign(node.style, opts.style);
  return node;
}

function makeKnobCell(label, paramId, opts = {}) {
  const wrap = el("div", { className: "knob-cell" });
  const knobHost = el("div");
  wrap.appendChild(knobHost);
  if (label) wrap.appendChild(el("div", { className: "knob-label", text: label }));
  const bind = bindSlider(paramId);
  mountKnob(knobHost, { bind, size: opts.size || 32, tipId: paramId,
                        extraClass: opts.extraClass });
  return { host: wrap, bind };
}

function makeStepperCell(label, paramId) {
  const wrap = el("div", { className: "stepper-cell" });
  if (label) wrap.appendChild(el("div", { className: "stepper-label", text: label }));
  const host = el("div");
  wrap.appendChild(host);
  const bind = bindSlider(paramId);
  mountStepper(host, { bind, tipId: paramId });
  return { host: wrap, bind };
}

// Build & mount the FM panel into `root`. Returns a disposer that unmounts
// every widget and removes listeners — main.js calls this when the user
// switches mode away from FM.
export function mount(root, opts = {}) {
  ensureStyles();
  root.classList.add("fm-panel");
  root.innerHTML = "";

  // KIT entry — switches FM mode into the drum-kit pad grid (ADR-0021
  // amendment). Only shown when the host container provides the callback.
  if (typeof opts.onEnterKit === "function") {
    const kitBtn = el("button", { className: "btn fm-kit-enter", text: "KIT" });
    kitBtn.type = "button";
    kitBtn.addEventListener("click", async () => {
      try { const f = getNativeFunction("enterKit"); if (f) await f(); } catch (_e) { /* ignore */ }
      opts.onEnterKit();
    });
    root.appendChild(kitBtn);
  }

  // Top row: LFO/global + envelope + freq ctrl + misc + algo + topology.
  // 6-column grid (234 | 1fr | 130 | 96 | 86 | 132) — matches mvp2/01 mockup.
  const topRow = el("div", { className: "fm-top" });

  // --- LFO + global block ---------------------------------------------------
  const lfoBlock = el("div", { className: "fm-block" });
  lfoBlock.appendChild(el("div", { className: "fm-block-title", text: "LFO / Global" }));
  const lfoRow = el("div", { className: "lfo-row" });
  // The mockup (mvp2/01 reference) draws four knob shapes in the LFO row:
  // LFO · RATE · PMS · AMS. `lfo_enable` is a bool param, so the LFO knob
  // adapts the toggle bind into a slider-shaped controller (0 = off at the
  // 7 o'clock rest, 1 = on at 5 o'clock). The knob's drag/scroll naturally
  // snaps to the two states because 0.5 rounds either way.
  const lfoEnable = (() => {
    const wrap = el("div", { className: "knob-cell" });
    const knobHost = el("div");
    wrap.appendChild(knobHost);
    wrap.appendChild(el("div", { className: "knob-label", text: "LFO" }));
    const tog = bindToggle("lfo_enable");
    const togAsSlider = {
      name: "lfo_enable",
      getNormalised: () => (tog.get() ? 1 : 0),
      setNormalised: (v) => tog.set(v >= 0.5),
      beginGesture: () => {},
      endGesture:   () => {},
      onChange: (cb) => tog.onChange((on) => cb(on ? 1 : 0)),
      defaultNormalised: (fb = 0) => fb,
      dispose: () => (tog.dispose ? tog.dispose() : undefined),
      state: tog.state,
    };
    mountKnob(knobHost, { bind: togAsSlider, size: 32, tipId: "lfo_enable" });
    return wrap;
  })();
  const lfoRate = makeKnobCell("RATE", "lfo_rate");
  const pmsCell = makeKnobCell("PMS", "pms");
  const amsCell = makeKnobCell("AMS", "ams");
  lfoRow.appendChild(lfoEnable);
  lfoRow.appendChild(lfoRate.host);
  lfoRow.appendChild(pmsCell.host);
  lfoRow.appendChild(amsCell.host);
  lfoBlock.appendChild(lfoRow);

  const stepperRow = el("div", { className: "stepper-row" });
  stepperRow.appendChild(makeStepperCell("POLY",  "poly_voices").host);
  stepperRow.appendChild(makeStepperCell("RANGE", "pitch_bend_range").host);
  lfoBlock.appendChild(stepperRow);

  // LEGATO / RETRIG two-way toggle.
  const noteModeWrap = el("div", { className: "knob-cell note-mode-left" });
  const noteModeHost = el("div");
  noteModeWrap.appendChild(noteModeHost);
  noteModeWrap.appendChild(el("div", { className: "knob-label", text: "Note Mode" }));
  mountToggle(noteModeHost, {
    bind: bindToggle("note_mode"),
    variant: "two-way",
    labels: ["RETRIG", "LEGATO"],
    tipId: "note_mode",
  });
  lfoBlock.appendChild(noteModeWrap);

  topRow.appendChild(lfoBlock);

  // --- Envelope-curve block (with op-badge selector) ------------------------
  const envBlock = el("div", { className: "fm-block fm-env" });
  envBlock.appendChild(el("div", { className: "fm-block-title", text: "Envelope · Op 1" }));
  const envHost = el("div");
  const envelope = mountEnvelope(envHost, {
    width: 240, height: 110, tipId: "envelope_curve",
  });
  envBlock.appendChild(envHost);
  topRow.appendChild(envBlock);

  // Active operator tracked by envelope-curve — local UI state, not apvts.
  let activeOp = 1;
  const envBinds = {};   // op (1..4) -> { ar, dr, sl, sr, rr } bindings

  // --- FREQ CTRL block (3-button pill) -------------------------------------
  const freqBlock = el("div", { className: "fm-block fm-freqctrl" });
  freqBlock.appendChild(el("div", { className: "fm-block-title", text: "Freq Ctrl" }));
  const freqPillWrap = el("div", { className: "freq-mode-pill" });
  const freqCtrlCombo = bindCombo("freq_ctrl_mode");
  const freqBtns = ["INT MUL", "FLOAT MUL", "AUTO RETRIG"].map((label, idx) => {
    const btn = el("button", { className: "btn" });
    btn.type = "button";
    btn.textContent = label;
    btn.addEventListener("click", () => freqCtrlCombo.setIndex(idx));
    freqPillWrap.appendChild(btn);
    return btn;
  });
  freqBlock.appendChild(freqPillWrap);
  topRow.appendChild(freqBlock);

  // --- Misc block (RETRIG RATE stepper-readout + OP1 FB knob) --------------
  const miscBlock = el("div", { className: "fm-block fm-misc" });
  miscBlock.appendChild(el("div", { className: "fm-block-title", text: "Misc" }));

  const retrigRateCell = el("div", { className: "stepper-cell retrig-rate" });
  retrigRateCell.appendChild(el("div", { className: "stepper-label", text: "Retrig Rate" }));
  const retrigStepperHost = el("div");
  retrigRateCell.appendChild(retrigStepperHost);
  mountStepper(retrigStepperHost, {
    bind: bindSlider("retrig_rate"),
    tipId: "retrig_rate",
  });
  miscBlock.appendChild(retrigRateCell);

  const op1FbCell = makeKnobCell("OP1 FB", "fb", { size: 32 });
  op1FbCell.host.classList.add("op1-fb-cell");
  miscBlock.appendChild(op1FbCell.host);
  topRow.appendChild(miscBlock);

  // --- Algorithm picker + Topology tile ------------------------------------
  // Two separate blocks per the mvp2/01 mockup so each gets its own grid
  // column with a fixed width. Topology must mount BEFORE the picker
  // because mountAlgoGrid's bind.onChange fires synchronously during
  // mount and the onSelect callback drives algoMini — referencing it
  // before initialisation would throw a TDZ ReferenceError (same class
  // of bug as the tooltip.js fix).
  const topoBlock = el("div", { className: "fm-block fm-algo-diagram" });
  topoBlock.appendChild(el("div", { className: "fm-block-title", text: "Topology" }));
  const algoMiniHost = el("div");
  const algoMini = mountAlgoMini(algoMiniHost, {
    size: 96, tipId: "algorithm_topology",
  });
  topoBlock.appendChild(algoMiniHost);

  const algoBlock = el("div", { className: "fm-block fm-algo-picker" });
  algoBlock.appendChild(el("div", { className: "fm-block-title", text: "Algorithm" }));
  const algoGridHost = el("div");
  mountAlgoGrid(algoGridHost, {
    bind: bindSlider("alg"),
    onSelect: (idx) => algoMini.setAlgorithm(idx),
    tipId: "alg",
  });
  algoBlock.appendChild(algoGridHost);

  // Grid column order from the mockup: algo picker (col 5), topology (col 6).
  topRow.appendChild(algoBlock);
  topRow.appendChild(topoBlock);

  root.appendChild(topRow);

  // --- Mid row: GLOBAL IN block | operator grid | VEL column ---------------
  // 3-column grid (88px | 1fr | 60px) — matches mvp2/01 mockup.
  const midRow = el("div", { className: "fm-mid" });

  // GLOBAL IN — PB + MW midi-wheels.
  const inBlock = el("div", { className: "fm-block fm-pbmw" });
  inBlock.appendChild(el("div", { className: "fm-block-title", text: "Global In" }));
  const pbCell = el("div", { className: "midi-wheel-cell" });
  const pbHost = el("span");
  pbCell.appendChild(pbHost);
  pbCell.appendChild(el("span", { className: "wheel-label", text: "PB" }));
  mountMidiWheel(pbHost, {
    bind: bindSlider("pitch_bend_value"),
    variant: "pb",
    tipId: "pitch_bend_value",
  });
  const mwCell = el("div", { className: "midi-wheel-cell" });
  const mwHost = el("span");
  mwCell.appendChild(mwHost);
  mwCell.appendChild(el("span", { className: "wheel-label", text: "MW" }));
  mountMidiWheel(mwHost, {
    bind: bindSlider("mod_wheel_value"),
    variant: "mw",
    tipId: "mod_wheel_value",
  });
  // Wrap PB + MW in a horizontal flex container so the wheels sit
  // side-by-side under the GLOBAL IN block title (matches mockup layout).
  const wheelsWrap = el("div", { className: "wheels" });
  wheelsWrap.appendChild(pbCell);
  wheelsWrap.appendChild(mwCell);
  inBlock.appendChild(wheelsWrap);
  midRow.appendChild(inBlock);

  // --- Operator grid (4 rows + CH VOL master + header row) -----------------
  const opGridBlock = el("div", { className: "fm-block fm-opgrid" });
  opGridBlock.appendChild(el("div", { className: "fm-block-title", text: "Operator Grid" }));

  const chVolRow = el("div", { className: "ch-vol-row" });
  const chVolCell = makeKnobCell("CH VOL", "channel_tl", { size: 32 });
  chVolRow.appendChild(chVolCell.host);
  opGridBlock.appendChild(chVolRow);

  const opGrid = el("div", { className: "op-grid" });
  // Header row: column labels.
  for (const label of ["TL", "OP", "AM", "AR", "DR", "SL", "SR", "RR",
                       "RS", "SSG-EG", "MUL", "FREQ", "FIXED", "DT"]) {
    opGrid.appendChild(el("div", { className: "col-header", text: label }));
  }

  const opBadgeCtrls = [];

  // Tag every per-op host with dataset.op so the delegated pointerdown
  // listener installed after this loop can identify which operator was
  // touched and focus the envelope on it — see comment by the listener
  // below for why this is pointer-based rather than onChange-based.
  const tagOp = (host, op) => { host.dataset.op = String(op); return host; };

  for (let op = 1; op <= 4; ++op) {
    // TL — leftmost anchor knob (28px per mockup, slightly larger than
    // the 24px regular operator knobs so it reads as the column anchor).
    const tlCell = tagOp(makeKnobCell(null, `tl_op${op}`, { size: 28 }).host, op);
    opGrid.appendChild(tlCell);

    // op-badge: click to swap which operator the envelope-curve tracks.
    const badgeHost = tagOp(el("div"), op);
    const badge = mountOpBadge(badgeHost, {
      index: op,
      onClick: (idx) => {
        activeOp = idx;
        opBadgeCtrls.forEach((b, i) => b.setActive(i + 1 === activeOp));
        envBlock.querySelector(".fm-block-title").textContent = `Envelope · Op ${activeOp}`;
        refreshEnvelope();
      },
      tipId: `op_badge_${op}`,
    });
    opBadgeCtrls.push(badge);
    opGrid.appendChild(badgeHost);

    // AM toggle.
    const amHost = tagOp(el("div"), op);
    mountToggle(amHost, { bind: bindToggle(`amon_op${op}`), tipId: `amon_op${op}` });
    opGrid.appendChild(amHost);

    // AR / DR / SL / SR / RR knobs — bound, plus envelope wiring.
    const arBind = bindSlider(`ar_op${op}`);
    const drBind = bindSlider(`dr_op${op}`);
    const slBind = bindSlider(`sl_op${op}`);
    const srBind = bindSlider(`sr_op${op}`);
    const rrBind = bindSlider(`rr_op${op}`);

    // AR mounts separately from the rate loop so we can capture its host
    // element — the SSG-EG nudge (ADR-0027) toggles a class + tooltip on it
    // when the paired SSG-EG is on a looping shape and AR < 31.
    const arHost = tagOp(el("div"), op);
    mountKnob(arHost, { bind: arBind, size: 24, tipId: `ar_op${op}` });
    opGrid.appendChild(arHost);

    for (const [bind, key, max] of [
      [drBind, "dr", 31], [slBind, "sl", 15],
      [srBind, "sr", 31], [rrBind, "rr", 15],
    ]) {
      const host = tagOp(el("div"), op);
      mountKnob(host, { bind, size: 24, tipId: `${key}_op${op}` });
      opGrid.appendChild(host);
    }
    envBinds[op] = { ar: arBind, dr: drBind, sl: slBind, sr: srBind, rr: rrBind };

    // RS / SSG-EG / MUL knobs.
    const rsHost = tagOp(el("div"), op);
    mountKnob(rsHost, { bind: bindSlider(`ks_op${op}`), size: 24, tipId: `ks_op${op}` });
    opGrid.appendChild(rsHost);

    // SSG-EG: stepper with named-shape readout per design 08-ui-views.md:342
    // (Combo + 9 states). The widget uses ▲/▼ buttons to cycle through the
    // 9 valid SSG-EG register values; the LCD shows the shape's short name.
    // Wrapped in .ssg-cell so the CSS can tighten the stepper to fit the
    // operator-grid column width.
    const ssgBind = bindSlider(`ssg_op${op}`);
    const ssgHost = tagOp(el("div", { className: "ssg-cell" }), op);
    mountStepper(ssgHost, {
      bind: ssgBind,
      sizeMini: true,
      valueSequence: SSG_VALUE_SEQUENCE,
      formatter: (v) => SSG_LABELS[v] || "OFF",
      tipId: `ssg_op${op}`,
    });
    opGrid.appendChild(ssgHost);

    // SSG-EG loop nudge — ADR-0027. Repaints the AR knob amber and swaps
    // its tooltip when the paired SSG-EG is on a looping shape and AR < 31.
    // Recomputes reactively from either binding; clears the moment the
    // condition no longer holds. No audio override, no patch mutation.
    const updateSsgArNudge = () => {
      const arInt  = Math.round(arBind.getNormalised()  * 31);
      const ssgInt = Math.round(ssgBind.getNormalised() * 15);
      const mismatch = SSG_LOOPING_VALUES.has(ssgInt) && arInt < 31;
      arHost.classList.toggle("ssg-ar-mismatch", mismatch);
      const tip = mismatch ? AR_TIP_SSG_WARN : AR_TIP_NORMAL;
      arHost.setAttribute("data-tip-name", tip.name);
      arHost.setAttribute("data-tip-desc", tip.desc);
    };
    arBind.onChange(updateSsgArNudge);
    ssgBind.onChange(updateSsgArNudge);

    const mulHost = tagOp(el("div"), op);
    mountKnob(mulHost, { bind: bindSlider(`mul_op${op}`), size: 24, tipId: `mul_op${op}` });
    opGrid.appendChild(mulHost);

    // FREQ — state-dependent LCD readout. Display updates on every relevant
    // change.
    const freqCell = tagOp(el("div", { className: "freq-lcd" }), op);
    const freqLcd = mountLcd(freqCell, {
      width: 40, height: 16, fontPx: 9, tipId: "freq_lcd",
    });
    opGrid.appendChild(freqCell);

    // FIXED toggle (greyed in INT_MUL).
    const fixedCell = tagOp(el("div", { className: "fixed-cell" }), op);
    const fixedHost = el("div");
    fixedCell.appendChild(fixedHost);
    mountToggle(fixedHost, {
      bind: bindToggle(`fixed_op${op}`),
      tipId: `fixed_op${op}`,
    });
    opGrid.appendChild(fixedCell);

    // DT knob.
    const dtHost = tagOp(el("div"), op);
    mountKnob(dtHost, { bind: bindSlider(`dt_op${op}`), size: 24, tipId: `dt_op${op}` });
    opGrid.appendChild(dtHost);

    // FREQ display behaviour: dependent on freq_ctrl_mode × fixed[op] × mul.
    // Refresh on each input change.
    const refreshFreq = () => {
      const mode = freqCtrlCombo.getIndex();
      const mulInt = Math.round((bindSlider(`mul_op${op}`).getNormalised()) * 15);
      const mulFloat = bindSlider(`mul_float_op${op}`).getNormalised() * 15.49 + 0.5;
      const fixed = bindToggle(`fixed_op${op}`).get();
      const fixedHz = bindSlider(`freq_fixed_hz_op${op}`).getNormalised() * 19980 + 20;

      if (mode === FREQ_CTRL_MODE_INT) {
        // INT MUL — integer multiplier, no fixed-Hz path.
        const label = mulInt === 0 ? "×0.5" : `×${mulInt}`;
        freqLcd.setText(label);
      } else if (fixed) {
        freqLcd.setText(`${Math.round(fixedHz)} Hz`);
      } else {
        freqLcd.setText(mulFloat.toFixed(2));
      }
    };
    refreshFreq();
    bindSlider(`mul_op${op}`).onChange(refreshFreq);
    bindSlider(`mul_float_op${op}`).onChange(refreshFreq);
    bindToggle(`fixed_op${op}`).onChange(refreshFreq);
    bindSlider(`freq_fixed_hz_op${op}`).onChange(refreshFreq);
    freqCtrlCombo.onChange(refreshFreq);
  }
  opGridBlock.appendChild(opGrid);

  // Envelope op-focus follows touch within the operator grid. We use
  // pointerdown — not bind.onChange — so a programmatic setValueNotifyingHost
  // pass (preset load, state restore, host automation) doesn't rapidly
  // cycle the envelope through all four operators. Each per-op host cell
  // carries dataset.op set by tagOp() above; this listener walks up from
  // the pointer target until it finds that attribute. Capture phase so the
  // knob widget's own pointerdown handler can't preempt us via
  // stopPropagation.
  opGrid.addEventListener("pointerdown", (e) => {
    let n = e.target;
    while (n && n !== opGrid) {
      if (n.dataset && n.dataset.op) {
        const op = parseInt(n.dataset.op, 10);
        if (op >= 1 && op <= 4 && op !== activeOp) {
          activeOp = op;
          opBadgeCtrls.forEach((b, i) => b.setActive(i + 1 === activeOp));
          envBlock.querySelector(".fm-block-title").textContent
            = `Envelope · Op ${activeOp}`;
          refreshEnvelope();
        }
        return;
      }
      n = n.parentElement;
    }
  }, true);

  // FREQ_CTRL_MODE → operator-grid styling (greys out FIXED in INT_MUL).
  const updateGridForMode = (mode) => {
    opGrid.classList.toggle("is-int-mul", mode === FREQ_CTRL_MODE_INT);
    // Pill highlight.
    freqBtns.forEach((b, i) => b.classList.toggle("is-active", i === mode));
    // RETRIG RATE greyed (visible but non-interactive) when not in AUTO_RETRIG —
    // matches the FIXED toggle's grey-out pattern in INT_MUL (.is-int-mul above).
    retrigRateCell.classList.toggle("is-disabled",
                                    mode !== FREQ_CTRL_MODE_RETRIG);
  };
  freqCtrlCombo.onChange(updateGridForMode);
  updateGridForMode(freqCtrlCombo.getIndex());

  midRow.appendChild(opGridBlock);

  // --- VEL column (right margin, single column 4-deep) ---------------------
  // No block-title — the VEL col-header inside doubles as the label (mockup).
  const velBlock = el("div", { className: "fm-block fm-velblock" });
  const velCol = el("div", { className: "vel-col" });
  velCol.appendChild(el("div", { className: "col-header", text: "VEL" }));
  for (let op = 1; op <= 4; ++op) {
    const cell = makeKnobCell(null, `vel_op${op}`, { size: 24 }).host;
    velCol.appendChild(cell);
  }
  velBlock.appendChild(velCol);
  midRow.appendChild(velBlock);

  root.appendChild(midRow);

  // --- Envelope-curve refresh wiring --------------------------------------
  function refreshEnvelope() {
    const b = envBinds[activeOp];
    const ar = Math.round(b.ar.getNormalised() * 31);
    const dr = Math.round(b.dr.getNormalised() * 31);
    const sl = Math.round(b.sl.getNormalised() * 15);
    const sr = Math.round(b.sr.getNormalised() * 31);
    const rr = Math.round(b.rr.getNormalised() * 15);
    envelope.setEnvelope(ar, dr, sl, sr, rr);
  }
  for (let op = 1; op <= 4; ++op) {
    const b = envBinds[op];
    for (const k of ["ar", "dr", "sl", "sr", "rr"]) {
      b[k].onChange(() => { if (op === activeOp) refreshEnvelope(); });
    }
  }
  refreshEnvelope();
  // Defensive re-fire after the synchronous mount path completes. Catches the
  // case where a setStateInformation / preset-load relay value arrived during
  // mount and the synchronous bind.onChange hadn't installed its listener
  // yet — without this, the envelope canvas can sit on default rates until
  // the user toggles an op badge or wiggles a knob.
  queueMicrotask(refreshEnvelope);
  opBadgeCtrls[0].setActive(true);

  return {
    dispose() {
      root.classList.remove("fm-view");
      root.innerHTML = "";
    },
  };
}
