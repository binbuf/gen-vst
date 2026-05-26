// SQ mode panel — `08-ui-views.md` view 3.
//
// Layout + CSS ported verbatim from the mvp2/01 mockup (deleted in
// task 04 per its own disposal note): see `ui/mockup-sq.html` and
// `ui/mockup/mockup-sq.css` at commit 2120d65 for the throwaway
// source-of-truth.
//
// Five columns: left GLOBAL IN strip (PB wheel only — no MW per view 3),
// then four equal channel strips. Each tone strip carries an ADSR LCD
// thumbnail, ATK / DR1 / SUS / DR2 / RR envelope knobs, then a bottom
// stack of Detune / Vol / Pan. The noise strip swaps Detune for a
// VOL position in the 2nd env row and adds Type (W/P) + Rate
// (L/M/H/CH2) pills below the Pan slider.
//
// Every interactive control routes through binding.js relays — apvts
// param IDs match `createParameterLayout()` (tone suffixes `_ch1` /
// `_ch2` / `_ch3`, noise suffix `_noise`).

import {
  bindSlider,
  bindCombo,
} from "../binding.js";

import { mount as mountKnob }       from "../widgets/knob.js";
import { mount as mountSlider }     from "../widgets/slider.js";
import { mount as mountEnvelope }   from "../widgets/envelope-curve.js";
import { mount as mountMidiWheel }  from "../widgets/midi-wheel.js";

const CHANNEL_SUFFIX = ["_ch1", "_ch2", "_ch3", "_noise"];
const CHANNEL_LABEL  = ["Tone 1", "Tone 2", "Tone 3", "Noise"];

const ENV_KNOB_MAX = { atk: 31, dr1: 31, sus: 15, dr2: 31, rr: 15 };

// SQ panel CSS — ported verbatim from mvp2/01 mockup-sq.css. Widget
// recipes (.knob, .midi-wheel, .btn-pill, …) come from design-system.css;
// this sheet only positions them.
function ensureStyles() {
  if (document.getElementById("genvst-sq-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-sq-view-style";
  style.textContent = `
    .sq-panel {
      width: 100%;
      height: 100%;
      display: grid;
      /* Leftmost narrow column for the GLOBAL IN block (PB wheel only —
       * MW omitted in SQ, see 08-ui-views.md view 3), then four equal
       * columns for the three tone strips + noise strip. Single row at
       * 1fr so each strip cell inherits the panel's full height instead
       * of auto-collapsing to its natural content height. */
      grid-template-columns: 64px 1fr 1fr 1fr 1fr;
      grid-template-rows: 1fr;
      align-items: stretch;
      gap: 12px;
      padding: 22px 14px 14px;
      box-sizing: border-box;
    }

    .sq-panel .sq-globalin {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 8px;
      padding: 22px 4px 8px;
      background: rgba(0, 0, 0, 0.18);
      border: 1px solid rgba(0, 0, 0, 0.4);
      border-radius: 4px;
      position: relative;
    }
    .sq-panel .sq-globalin > .strip-title {
      position: absolute;
      top: 4px;
      left: 6px;
      right: 6px;
      text-align: center;
      font: 500 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text-dim);
    }
    .sq-panel .midi-wheel-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
    }
    .sq-panel .midi-wheel-cell .wheel-label {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
    }

    .sq-panel .sq-strip {
      display: flex;
      flex-direction: column;
      align-items: stretch;
      justify-content: space-between;
      gap: 14px;
      min-height: 0;
      padding: 22px 12px 16px;
      background: rgba(0, 0, 0, 0.18);
      border: 1px solid rgba(0, 0, 0, 0.4);
      border-radius: 4px;
      position: relative;
    }
    .sq-panel .sq-strip > .strip-title {
      position: absolute;
      top: 4px;
      left: 10px;
      font: 500 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text-dim);
    }

    .sq-panel .sq-env-lcd {
      height: 64px;
      background: var(--lcd-bg, #0d1424);
      border: 1px solid var(--inset-edge-dark);
      border-radius: 2px;
      box-shadow:
        inset 2px 2px 5px rgba(0, 0, 0, 0.7),
        0 0 0 1px var(--lcd-bg-edge, #050810);
      position: relative;
      overflow: hidden;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .sq-panel .sq-env-lcd::before {
      content: "";
      position: absolute;
      inset: 0;
      background: linear-gradient(180deg,
        rgba(0, 0, 0, 0.45) 0%, transparent 15%, transparent 85%,
        rgba(78, 160, 255, 0.04) 100%);
      pointer-events: none;
    }

    .sq-panel .sq-env-row {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 8px 4px;
      justify-items: center;
    }
    .sq-panel .sq-env-row.row2 {
      grid-template-columns: repeat(2, 1fr);
    }
    .sq-panel .sq-noise-env-row {
      grid-template-columns: repeat(3, 1fr);
    }
    .sq-panel .sq-noise-env-row-b {
      grid-template-columns: repeat(3, 1fr);
    }

    .sq-panel .knob-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .sq-panel .knob-cell .knob-cell-label {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }

    .sq-panel .sq-bottom {
      display: flex;
      flex-direction: column;
      gap: 8px;
    }
    .sq-panel .sq-bottom-row {
      display: grid;
      grid-template-columns: 40px 1fr;
      align-items: center;
      gap: 4px 8px;
    }
    .sq-panel .sq-bottom-row .label-col {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text);
      text-align: left;
    }
    .sq-panel .sq-bottom-row .value-col {
      display: flex;
      align-items: center;
      justify-content: flex-end;
    }
    .sq-panel .sq-bottom-row .value-col .slider {
      flex: 1 1 auto;
      min-width: 0;
      width: 100%;
    }

    .sq-panel .sq-noise-extras {
      display: flex;
      flex-direction: column;
      gap: 4px;
      border-top: 1px solid rgba(255, 255, 255, 0.05);
      padding-top: 6px;
    }
    .sq-panel .sq-noise-extras .btn-pill {
      display: flex;
      flex: 1 1 auto;
    }
    .sq-panel .sq-noise-extras .btn-pill .btn {
      flex: 1 1 0;
      padding: 4px 4px;
      font-size: 8px;
      letter-spacing: 0.14em;
    }
  `;
  document.head.appendChild(style);
}

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
  if (label) wrap.appendChild(el("div", { className: "knob-cell-label", text: label }));
  const bind = bindSlider(paramId);
  mountKnob(knobHost, { bind, size: opts.size || 28, tipId: paramId });
  return { host: wrap, bind };
}

function makePillCell(paramId, choices) {
  const pill = el("div", { className: "btn-pill" });
  const combo = bindCombo(paramId);
  const btns = choices.map((choice, idx) => {
    const btn = el("button", { className: "btn" });
    btn.type = "button";
    btn.textContent = choice;
    btn.addEventListener("click", () => combo.setIndex(idx));
    pill.appendChild(btn);
    return btn;
  });
  combo.onChange((idx) => {
    btns.forEach((b, i) => b.classList.toggle("is-active", i === idx));
  });
  return pill;
}

// Build one channel strip. Tone strips have envelope knob-grid +
// Detune/Vol/Pan bottom; the noise strip swaps Detune for VOL in the
// 2nd env row and replaces the bottom with Pan + Type/Rate pills.
function makeChannelStrip(chIndex) {
  const isNoise = (chIndex === 3);
  const sfx     = CHANNEL_SUFFIX[chIndex];
  const label   = CHANNEL_LABEL[chIndex];

  const strip = el("div", { className: "sq-strip" });
  strip.appendChild(el("span", { className: "strip-title", text: label }));

  // ADSR envelope LCD canvas — same widget as FM, sized down.
  const envHost = el("div", { className: "sq-env-lcd" });
  const envelope = mountEnvelope(envHost, {
    width: 140, height: 56, tipId: "envelope_curve",
  });
  strip.appendChild(envHost);

  // Env knobs split across two rows per the mockup:
  //   Row 1: ATK · DR1 · SUS   (.sq-env-row)
  //   Row 2: DR2 · RR          (.sq-env-row.row2)
  //   Noise row 2: DR2 · RR · VOL  (.sq-noise-env-row-b)
  const row1 = el("div", { className: "sq-env-row" + (isNoise ? " sq-noise-env-row" : "") });
  const atkCell = makeKnobCell("ATK", `psg_atk${sfx}`);
  const dr1Cell = makeKnobCell("DR1", `psg_dr1${sfx}`);
  const susCell = makeKnobCell("SUS", `psg_sus${sfx}`);
  row1.appendChild(atkCell.host);
  row1.appendChild(dr1Cell.host);
  row1.appendChild(susCell.host);
  strip.appendChild(row1);

  const row2 = el("div", { className: "sq-env-row " + (isNoise ? "sq-noise-env-row-b" : "row2") });
  const dr2Cell = makeKnobCell("DR2", `psg_dr2${sfx}`);
  const rrCell  = makeKnobCell("RR",  `psg_rr${sfx}`);
  row2.appendChild(dr2Cell.host);
  row2.appendChild(rrCell.host);
  if (isNoise) {
    const volCell = makeKnobCell("VOL", `psg_vol${sfx}`);
    row2.appendChild(volCell.host);
  }
  strip.appendChild(row2);

  // Live-recompute the envelope thumbnail. Mapping ATK→AR, DR1→DR,
  // SUS→SL, DR2→SR, RR→RR.
  const envBinds = {
    ar: atkCell.bind, dr: dr1Cell.bind, sl: susCell.bind,
    sr: dr2Cell.bind, rr:  rrCell.bind,
  };
  const refreshEnvelope = () => {
    envelope.setEnvelope(
      Math.round(envBinds.ar.getNormalised() * ENV_KNOB_MAX.atk),
      Math.round(envBinds.dr.getNormalised() * ENV_KNOB_MAX.dr1),
      Math.round(envBinds.sl.getNormalised() * ENV_KNOB_MAX.sus),
      Math.round(envBinds.sr.getNormalised() * ENV_KNOB_MAX.dr2),
      Math.round(envBinds.rr.getNormalised() * ENV_KNOB_MAX.rr),
    );
  };
  for (const b of Object.values(envBinds)) b.onChange(refreshEnvelope);
  refreshEnvelope();

  // Bottom stack — different for tone vs noise.
  const bottom = el("div", { className: "sq-bottom" });

  if (!isNoise) {
    // Tone: Detune / Vol / Pan stacked rows.
    const detuneRow = el("div", { className: "sq-bottom-row" });
    detuneRow.appendChild(el("span", { className: "label-col", text: "Detune" }));
    const detuneVal = el("span", { className: "value-col" });
    const detuneKnob = el("div");
    detuneVal.appendChild(detuneKnob);
    mountKnob(detuneKnob, {
      bind: bindSlider(`psg_detune${sfx}`),
      size: 26,
      tipId: `psg_detune${sfx}`,
    });
    detuneRow.appendChild(detuneVal);
    bottom.appendChild(detuneRow);

    const volRow = el("div", { className: "sq-bottom-row" });
    volRow.appendChild(el("span", { className: "label-col", text: "Vol" }));
    const volVal = el("span", { className: "value-col" });
    const volKnob = el("div");
    volVal.appendChild(volKnob);
    mountKnob(volKnob, {
      bind: bindSlider(`psg_vol${sfx}`),
      size: 26,
      tipId: `psg_vol${sfx}`,
    });
    volRow.appendChild(volVal);
    bottom.appendChild(volRow);

    const panRow = el("div", { className: "sq-bottom-row" });
    panRow.appendChild(el("span", { className: "label-col", text: "Pan" }));
    const panVal = el("span", { className: "value-col" });
    const panHost = el("div");
    panVal.appendChild(panHost);
    mountSlider(panHost, {
      bind: bindSlider(`psg_pan${sfx}`),
      tipId: `psg_pan${sfx}`,
      defaultNormalised: 0.5,
    });
    panRow.appendChild(panVal);
    bottom.appendChild(panRow);
  } else {
    // Noise: Pan row, then Type / Rate pills inside .sq-noise-extras.
    const panRow = el("div", { className: "sq-bottom-row" });
    panRow.appendChild(el("span", { className: "label-col", text: "Pan" }));
    const panVal = el("span", { className: "value-col" });
    const panHost = el("div");
    panVal.appendChild(panHost);
    mountSlider(panHost, {
      bind: bindSlider(`psg_pan${sfx}`),
      tipId: `psg_pan${sfx}`,
      defaultNormalised: 0.5,
    });
    panRow.appendChild(panVal);
    bottom.appendChild(panRow);

    const extras = el("div", { className: "sq-noise-extras" });
    const typeRow = el("div", { className: "sq-bottom-row" });
    typeRow.appendChild(el("span", { className: "label-col", text: "Type" }));
    const typeVal = el("span", { className: "value-col" });
    typeVal.appendChild(makePillCell("psg_noise_type", ["W", "P"]));
    typeRow.appendChild(typeVal);
    extras.appendChild(typeRow);

    const rateRow = el("div", { className: "sq-bottom-row" });
    rateRow.appendChild(el("span", { className: "label-col", text: "Rate" }));
    const rateVal = el("span", { className: "value-col" });
    rateVal.appendChild(makePillCell("psg_noise_rate", ["L", "M", "H", "CH2"]));
    rateRow.appendChild(rateVal);
    extras.appendChild(rateRow);

    bottom.appendChild(extras);
  }

  strip.appendChild(bottom);
  return strip;
}

// Build & mount the SQ panel into `root`. Returns a disposer.
export function mount(root) {
  ensureStyles();
  root.classList.add("sq-panel");
  root.innerHTML = "";

  // --- GLOBAL IN block (left column) --------------------------------------
  const inBlock = el("div", { className: "sq-globalin" });
  inBlock.appendChild(el("span", { className: "strip-title", text: "Global In" }));
  const pbCell  = el("div", { className: "midi-wheel-cell" });
  const pbHost  = el("span");
  pbCell.appendChild(pbHost);
  pbCell.appendChild(el("span", { className: "wheel-label", text: "PB" }));
  mountMidiWheel(pbHost, {
    bind: bindSlider("pitch_bend_value"),
    variant: "pb",
    tipId: "pitch_bend_value",
  });
  inBlock.appendChild(pbCell);
  root.appendChild(inBlock);

  // --- Four channel strips (3 tone + 1 noise) -----------------------------
  for (let ch = 0; ch < 4; ++ch) {
    root.appendChild(makeChannelStrip(ch));
  }

  return {
    dispose() {
      root.classList.remove("sq-panel");
      root.innerHTML = "";
    },
  };
}
