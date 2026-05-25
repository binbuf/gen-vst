// SQ mode panel — `08-ui-views.md` view 3.
//
// Assembled from Task 04's widget library. Left-edge GLOBAL IN block carries
// the read-only `PB` midi-wheel, followed by four vertical strips: three tone
// channels + one noise channel.
//
//   Tone strip:  envelope-curve thumbnail
//                ATK / DR1 / SUS / DR2 / RR knobs
//                DETUNE / VOL knobs + PAN slider
//
//   Noise strip: envelope-curve thumbnail
//                ATK / DR1 / SUS / DR2 / RR knobs
//                VOL knob + PAN slider
//                TYPE pill (white / periodic)
//                RATE pill (low / mid / high / ch2)
//
// Every interactive control routes through binding.js relays — apvts param
// IDs match `createParameterLayout()` in src/PluginProcessor.cpp (tone
// suffixes `_ch1` / `_ch2` / `_ch3`, noise suffix `_noise`).
//
// MW is deliberately omitted from GLOBAL IN — v2 SQ wires no mod-wheel
// destination, so a visualiser here would be misleading chrome (see view 3).

import {
  bindSlider,
  bindCombo,
} from "../binding.js";

import { mount as mountKnob }       from "../widgets/knob.js";
import { mount as mountSlider }     from "../widgets/slider.js";
import { mount as mountEnvelope }   from "../widgets/envelope-curve.js";
import { mount as mountMidiWheel }  from "../widgets/midi-wheel.js";

// Channel suffixes — index 0..2 are tone channels, 3 is noise. The apvts
// param IDs encode the suffix directly (no numeric `[ch]` indexing).
const CHANNEL_SUFFIX = ["_ch1", "_ch2", "_ch3", "_noise"];
const CHANNEL_LABEL  = ["Tone 1", "Tone 2", "Tone 3", "Noise"];

// Envelope-knob ranges — kept inline since the widget needs integer values to
// drive its polyline. Matches the apvts range declared in PluginProcessor.cpp
// (psg_atk 0..31, psg_dr1 0..31, psg_sus 0..15, psg_dr2 0..31, psg_rr 0..15).
const ENV_KNOB_MAX = { atk: 31, dr1: 31, sus: 15, dr2: 31, rr: 15 };

// SQ panel CSS scoped to .sq-view. Same self-contained pattern as fm-view —
// widget recipes (.knob, .slider, .midi-wheel, .btn-pill, …) come from
// design-system.css; this sheet only positions them.
function ensureStyles() {
  if (document.getElementById("genvst-sq-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-sq-view-style";
  style.textContent = `
    .sq-view { width: 100%; height: 100%; display: flex; gap: 8px; align-items: stretch; }
    .sq-view .sq-block {
      background: rgba(0,0,0,0.10);
      border: 1px solid rgba(0,0,0,0.30);
      border-radius: 3px;
      padding: 8px 10px;
      display: flex;
      flex-direction: column;
      gap: 6px;
    }
    .sq-view .sq-block-title {
      font: 500 8px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--label-text-dim);
      margin-bottom: 2px;
      text-align: center;
    }
    .sq-view .global-in {
      align-items: center;
      justify-content: flex-start;
      padding: 8px 6px;
    }
    .sq-view .ch-strip {
      flex: 1 1 0;
      min-width: 0;
      align-items: stretch;
    }
    .sq-view .env-knob-row {
      display: flex;
      gap: 4px;
      justify-content: space-between;
    }
    .sq-view .knob-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .sq-view .knob-cell .knob-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .sq-view .control-row {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .sq-view .control-row .knob-cell { flex: 0 0 auto; }
    .sq-view .control-row .pan-cell  { flex: 1 1 0; min-width: 0; }
    .sq-view .pan-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 3px;
    }
    .sq-view .pan-cell .pan-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
    }
    .sq-view .pan-cell .slider { width: 100%; min-width: 60px; }
    .sq-view .pill-row {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .sq-view .pill-cell {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .sq-view .pill-cell .pill-label {
      font: 500 7px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.85;
      width: 32px;
      flex: 0 0 auto;
    }
    .sq-view .pill-cell .btn-pill { flex: 1 1 0; }
    .sq-view .pill-cell .btn-pill .btn {
      flex: 1 1 0;
      padding: 4px 4px;
      font-size: 8px;
      letter-spacing: 0.14em;
    }
    .sq-view .env-curve-host {
      display: flex;
      justify-content: center;
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
  if (label) wrap.appendChild(el("div", { className: "knob-label", text: label }));
  const bind = bindSlider(paramId);
  mountKnob(knobHost, { bind, size: opts.size || 28, tipId: paramId });
  return { host: wrap, bind };
}

function makePanCell(paramId) {
  const wrap = el("div", { className: "pan-cell" });
  const sliderHost = el("div");
  wrap.appendChild(sliderHost);
  wrap.appendChild(el("div", { className: "pan-label", text: "PAN" }));
  mountSlider(sliderHost, {
    bind: bindSlider(paramId),
    tipId: paramId,
    defaultNormalised: 0.5,
  });
  return wrap;
}

// Build a vertical pill of `.btn`s bound to a Choice apvts param. The active
// button carries `.is-active`. Returns the wrapper element.
function makePillCell(label, paramId, choices) {
  const wrap = el("div", { className: "pill-cell" });
  wrap.appendChild(el("div", { className: "pill-label", text: label }));
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
  wrap.appendChild(pill);
  return wrap;
}

// Build one channel strip (tone or noise). `chIndex` selects the apvts suffix
// + the strip's label; the noise strip differs by dropping DETUNE and adding
// TYPE / RATE pills.
function makeChannelStrip(chIndex) {
  const isNoise = (chIndex === 3);
  const sfx     = CHANNEL_SUFFIX[chIndex];
  const label   = CHANNEL_LABEL[chIndex];

  const block = el("div", { className: "sq-block ch-strip" });
  block.appendChild(el("div", { className: "sq-block-title", text: label }));

  // Envelope-curve thumbnail — same widget as FM, sized down for the strip.
  const envHost = el("div", { className: "env-curve-host" });
  const envelope = mountEnvelope(envHost, { width: 150, height: 70 });
  block.appendChild(envHost);

  // Envelope knobs — 5 across.
  const envRow = el("div", { className: "env-knob-row" });
  const atkCell = makeKnobCell("ATK", `psg_atk${sfx}`);
  const dr1Cell = makeKnobCell("DR1", `psg_dr1${sfx}`);
  const susCell = makeKnobCell("SUS", `psg_sus${sfx}`);
  const dr2Cell = makeKnobCell("DR2", `psg_dr2${sfx}`);
  const rrCell  = makeKnobCell("RR",  `psg_rr${sfx}`);
  envRow.appendChild(atkCell.host);
  envRow.appendChild(dr1Cell.host);
  envRow.appendChild(susCell.host);
  envRow.appendChild(dr2Cell.host);
  envRow.appendChild(rrCell.host);
  block.appendChild(envRow);

  // Live-recompute the envelope thumbnail from the 5 knob values. The
  // envelope-curve API matches FM's (ar/dr/sl/sr/rr) — SQ's ATK/DR1/SUS/
  // DR2/RR map 1:1 onto those positional args.
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

  // DETUNE / VOL / PAN row. Noise has no pitch, so it drops DETUNE.
  const ctrlRow = el("div", { className: "control-row" });
  if (!isNoise) {
    ctrlRow.appendChild(makeKnobCell("DETUNE", `psg_detune${sfx}`).host);
  }
  ctrlRow.appendChild(makeKnobCell("VOL", `psg_vol${sfx}`).host);
  const panCell = makePanCell(`psg_pan${sfx}`);
  panCell.classList.add("pan-cell");
  ctrlRow.appendChild(panCell);
  block.appendChild(ctrlRow);

  // Noise-only TYPE + RATE pills.
  if (isNoise) {
    const pillRow = el("div", { className: "pill-row" });
    pillRow.appendChild(makePillCell("TYPE", "psg_noise_type",
                                     ["WHITE", "PERIODIC"]));
    pillRow.appendChild(makePillCell("RATE", "psg_noise_rate",
                                     ["LOW", "MID", "HIGH", "CH2"]));
    block.appendChild(pillRow);
  }

  return block;
}

// Build & mount the SQ panel into `root`. Returns a disposer that strips the
// view class and clears the host; main.js calls it on mode change.
export function mount(root) {
  ensureStyles();
  root.classList.add("sq-view");
  root.innerHTML = "";

  // --- GLOBAL IN block (left edge) --------------------------------------
  const inBlock = el("div", { className: "sq-block global-in" });
  inBlock.appendChild(el("div", { className: "sq-block-title", text: "Global In" }));
  const pbCell  = el("div", { className: "midi-wheel-cell" });
  const pbHost  = el("span");
  pbCell.appendChild(pbHost);
  pbCell.appendChild(el("span", { className: "wheel-label", text: "PB" }));
  // Pitch-bend is read-only on the SQ panel; PluginProcessor mirrors live
  // MIDI pitch-bend into `pitch_bend_value` (Task 05 § *MIDI state mirror*).
  // Range -1..+1 — JUCE normalises to 0..1; the widget centres at 0.5.
  mountMidiWheel(pbHost, {
    bind: bindSlider("pitch_bend_value"),
    variant: "pb",
    tipId: "pitch_bend_value",
  });
  inBlock.appendChild(pbCell);
  root.appendChild(inBlock);

  // --- Four channel strips (3 tone + 1 noise) ---------------------------
  for (let ch = 0; ch < 4; ++ch) {
    root.appendChild(makeChannelStrip(ch));
  }

  return {
    dispose() {
      root.classList.remove("sq-view");
      root.innerHTML = "";
    },
  };
}
