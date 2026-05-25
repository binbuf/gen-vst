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

// FM panel CSS scoped to .fm-view. Kept inline so the view ships self-
// contained without polluting design-system.css with view-specific layout.
// Recipes for individual widgets (.knob, .toggle, .algo-grid, etc.) come
// from design-system.css; this stylesheet only positions them.
function ensureStyles() {
  if (document.getElementById("genvst-fm-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-fm-view-style";
  style.textContent = `
    .fm-view { width: 100%; height: 100%; display: flex; flex-direction: column; gap: 6px; }
    .fm-view .fm-row { display: flex; gap: 8px; align-items: stretch; }
    .fm-view .fm-block {
      background: rgba(0,0,0,0.10);
      border: 1px solid rgba(0,0,0,0.30);
      border-radius: 3px;
      padding: 6px 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .fm-view .fm-block-title {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.16em;
      text-transform: uppercase;
      color: var(--label-text-dim);
      margin-bottom: 2px;
    }
    .fm-view .knob-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .fm-view .knob-cell .knob-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .fm-view .lfo-row { display: flex; gap: 10px; align-items: flex-end; }
    .fm-view .stepper-row { display: flex; gap: 8px; align-items: center; }
    .fm-view .stepper-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
    }
    .fm-view .stepper-cell .stepper-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .fm-view .op-grid {
      display: grid;
      grid-template-columns:
        [tl] 48px
        [badge] 28px
        [am] 28px
        [ar] 36px [dr] 36px [sl] 36px [sr] 36px [rr] 36px
        [rs] 36px [ssg] 56px [mul] 36px [freq] 44px
        [fixed] 28px [dt] 36px;
      gap: 4px 6px;
      align-items: center;
    }
    .fm-view .op-grid .col-header {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text-dim);
      text-align: center;
      padding-bottom: 2px;
    }
    .fm-view .op-grid .knob { margin: 0 auto; }
    .fm-view .op-grid .freq-lcd { display: flex; justify-content: center; }
    .fm-view .op-grid .fixed-cell { display: flex; justify-content: center; }
    .fm-view .op-grid.is-int-mul .fixed-cell { opacity: 0.35; pointer-events: none; }
    .fm-view .global-in {
      display: flex;
      flex-direction: row;
      gap: 6px;
      align-items: stretch;
    }
    .fm-view .vel-col {
      display: flex;
      flex-direction: column;
      gap: 6px;
      align-items: center;
    }
    .fm-view .ch-vol-row {
      display: flex;
      align-items: center;
      gap: 4px;
    }
    .fm-view .midi-wheel-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 2px;
    }
    .fm-view .midi-wheel-cell .wheel-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
    }
    .fm-view .retrig-rate.is-hidden { visibility: hidden; }
    .fm-view .freq-mode-pill {
      display: flex;
      flex-direction: column;
      gap: 2px;
    }
    .fm-view .freq-mode-pill .btn { padding: 4px 8px; }
    .fm-view .op-row {
      display: contents;   /* grid items continue from parent */
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
export function mount(root) {
  ensureStyles();
  root.classList.add("fm-view");
  root.innerHTML = "";

  // Top row: LFO/global + envelope + freq ctrl + misc + algo + topology.
  const topRow = el("div", { className: "fm-row" });

  // --- LFO + global block ---------------------------------------------------
  const lfoBlock = el("div", { className: "fm-block" });
  lfoBlock.appendChild(el("div", { className: "fm-block-title", text: "LFO / Global" }));
  const lfoRow = el("div", { className: "lfo-row" });
  // LFO enable lives behind the LFO RATE knob conceptually; the knob's value =
  // 0 + lfo_enable=false reads as "off". For MVP we bind the rate, then a small
  // toggle to the right enables it.
  const lfoRate = makeKnobCell("RATE", "lfo_rate");
  const lfoEnable = (() => {
    const wrap = el("div", { className: "knob-cell" });
    const toggle = el("div");
    wrap.appendChild(toggle);
    wrap.appendChild(el("div", { className: "knob-label", text: "LFO" }));
    mountToggle(toggle, { bind: bindToggle("lfo_enable"), tipId: "lfo_enable" });
    return wrap;
  })();
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
  const noteModeWrap = el("div", { className: "knob-cell" });
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
  const envBlock = el("div", { className: "fm-block" });
  envBlock.appendChild(el("div", { className: "fm-block-title", text: "Envelope · Op 1" }));
  const envHost = el("div");
  const envelope = mountEnvelope(envHost, { width: 240, height: 110 });
  envBlock.appendChild(envHost);
  topRow.appendChild(envBlock);

  // Active operator tracked by envelope-curve — local UI state, not apvts.
  let activeOp = 1;
  const envBinds = {};   // op (1..4) -> { ar, dr, sl, sr, rr } bindings

  // --- FREQ CTRL block (3-button pill) -------------------------------------
  const freqBlock = el("div", { className: "fm-block" });
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
  const miscBlock = el("div", { className: "fm-block" });
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
  miscBlock.appendChild(op1FbCell.host);
  topRow.appendChild(miscBlock);

  // --- Algorithm picker + topology tile -------------------------------------
  const algoBlock = el("div", { className: "fm-block" });
  algoBlock.appendChild(el("div", { className: "fm-block-title", text: "Algorithm" }));
  const algoGridHost = el("div");
  const algoMiniHost = el("div");
  const algoMini = mountAlgoMini(algoMiniHost, { size: 96 });
  mountAlgoGrid(algoGridHost, {
    bind: bindSlider("alg"),
    onSelect: (idx) => algoMini.setAlgorithm(idx),
    tipId: "alg",
  });
  const algoRow = el("div", { className: "fm-row" });
  algoRow.appendChild(algoGridHost);
  algoRow.appendChild(algoMiniHost);
  algoBlock.appendChild(algoRow);
  topRow.appendChild(algoBlock);

  root.appendChild(topRow);

  // --- Mid row: GLOBAL IN block | operator grid | VEL column ---------------
  const midRow = el("div", { className: "fm-row" });

  // GLOBAL IN — PB + MW midi-wheels.
  const inBlock = el("div", { className: "fm-block global-in" });
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
  inBlock.appendChild(pbCell);
  inBlock.appendChild(mwCell);
  midRow.appendChild(inBlock);

  // --- Operator grid (4 rows + CH VOL master + header row) -----------------
  const opGridBlock = el("div", { className: "fm-block" });
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
  for (let op = 1; op <= 4; ++op) {
    // TL — leftmost anchor knob.
    const tlCell = makeKnobCell(null, `tl_op${op}`, { size: 36 }).host;
    opGrid.appendChild(tlCell);

    // op-badge: click to swap which operator the envelope-curve tracks.
    const badgeHost = el("div");
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
    const amHost = el("div");
    mountToggle(amHost, { bind: bindToggle(`amon_op${op}`), tipId: `amon_op${op}` });
    opGrid.appendChild(amHost);

    // AR / DR / SL / SR / RR knobs — bound, plus envelope wiring.
    const arBind = bindSlider(`ar_op${op}`);
    const drBind = bindSlider(`dr_op${op}`);
    const slBind = bindSlider(`sl_op${op}`);
    const srBind = bindSlider(`sr_op${op}`);
    const rrBind = bindSlider(`rr_op${op}`);

    for (const [bind, key, max] of [
      [arBind, "ar", 31], [drBind, "dr", 31], [slBind, "sl", 15],
      [srBind, "sr", 31], [rrBind, "rr", 15],
    ]) {
      const host = el("div");
      mountKnob(host, { bind, size: 32, tipId: `${key}_op${op}` });
      opGrid.appendChild(host);
    }
    envBinds[op] = { ar: arBind, dr: drBind, sl: slBind, sr: srBind, rr: rrBind };

    // RS / SSG-EG / MUL knobs.
    const rsHost = el("div");
    mountKnob(rsHost, { bind: bindSlider(`ks_op${op}`), size: 32, tipId: `ks_op${op}` });
    opGrid.appendChild(rsHost);

    const ssgHost = el("div");
    mountKnob(ssgHost, { bind: bindSlider(`ssg_op${op}`), size: 32, tipId: `ssg_op${op}` });
    opGrid.appendChild(ssgHost);

    const mulHost = el("div");
    mountKnob(mulHost, { bind: bindSlider(`mul_op${op}`), size: 32, tipId: `mul_op${op}` });
    opGrid.appendChild(mulHost);

    // FREQ — state-dependent LCD readout. Display updates on every relevant
    // change.
    const freqCell = el("div", { className: "freq-lcd" });
    const freqLcd = mountLcd(freqCell, { width: 40, height: 16, fontPx: 9 });
    opGrid.appendChild(freqCell);

    // FIXED toggle (greyed in INT_MUL).
    const fixedCell = el("div", { className: "fixed-cell" });
    const fixedHost = el("div");
    fixedCell.appendChild(fixedHost);
    mountToggle(fixedHost, {
      bind: bindToggle(`fixed_op${op}`),
      tipId: `fixed_op${op}`,
    });
    opGrid.appendChild(fixedCell);

    // DT knob.
    const dtHost = el("div");
    mountKnob(dtHost, { bind: bindSlider(`dt_op${op}`), size: 32, tipId: `dt_op${op}` });
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

  // FREQ_CTRL_MODE → operator-grid styling (greys out FIXED in INT_MUL).
  const updateGridForMode = (mode) => {
    opGrid.classList.toggle("is-int-mul", mode === FREQ_CTRL_MODE_INT);
    // Pill highlight.
    freqBtns.forEach((b, i) => b.classList.toggle("is-active", i === mode));
    // RETRIG RATE visible only in AUTO_RETRIG.
    retrigRateCell.classList.toggle("is-hidden",
                                    mode !== FREQ_CTRL_MODE_RETRIG);
  };
  freqCtrlCombo.onChange(updateGridForMode);
  updateGridForMode(freqCtrlCombo.getIndex());

  midRow.appendChild(opGridBlock);

  // --- VEL column (right margin, single column 4-deep) ---------------------
  const velBlock = el("div", { className: "fm-block vel-col" });
  velBlock.appendChild(el("div", { className: "fm-block-title", text: "Vel" }));
  for (let op = 1; op <= 4; ++op) {
    const cell = makeKnobCell(null, `vel_op${op}`, { size: 24 }).host;
    velBlock.appendChild(cell);
  }
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
  opBadgeCtrls[0].setActive(true);

  return {
    dispose() {
      root.classList.remove("fm-view");
      root.innerHTML = "";
    },
  };
}
