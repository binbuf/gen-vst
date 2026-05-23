/*
 * D (DAC) section view — 08-ui-views.md view 3.
 *
 * Section header band carries the `ENABLE` toggle, the DAC `MIDI` step-field
 * (routes to the dedicated DAC destination via MidiRouter), plus the section
 * title. Body has two groups:
 *   - SAMPLE strip: `LOAD WAV…` button → native juce::FileChooser via the
 *     `loadWavDialog` native function; filename + waveform display + length
 *     + bit-depth; `CLEAR` button → `clearDac` native function.
 *   - PLAYBACK group: `RATE` (8000/11025/22050), `MODE` (one-shot/loop),
 *     `LEVEL` knob.
 *
 * Empty state: before a WAV is loaded the sample strip shows `— no sample —`,
 * the waveform display is blank, and `CLEAR` is disabled.
 */

import {
  Knob, Toggle, SectionTabs, WaveformDisplay, LedReadout,
  bindSlider, bindToggle, bindCombo,
} from "../widgets/index.js";
import * as Juce from "../juce/index.js";
import { routingStepField } from "./routing-controls.js";

const loadWavDialogFn = Juce.getNativeFunction("loadWavDialog");
const clearDacFn      = Juce.getNativeFunction("clearDac");
const getDacInfoFn    = Juce.getNativeFunction("getDacInfo");

const NUM_PEAK_BUCKETS = 220;   // matches the strip width; the C++ side
                                // computes peaks at this resolution.

export function mountDView(host) {
  host.innerHTML = "";
  host.className = "d-section";

  /* ---- Section header band -------------------------------------------- */
  const header = document.createElement("div");
  header.className = "section-header";

  const title = document.createElement("span");
  title.className = "label section-title";
  title.textContent = "DAC · PCM SAMPLE CHANNEL";
  header.appendChild(title);

  const enableCell = document.createElement("div");
  enableCell.className = "header-cell";
  const enableLbl = document.createElement("span");
  enableLbl.className = "label";
  enableLbl.textContent = "ENABLE";
  const enableCanvas = document.createElement("canvas");
  enableCanvas.width = 22; enableCanvas.height = 14;
  enableCell.appendChild(enableLbl);
  enableCell.appendChild(enableCanvas);
  header.appendChild(enableCell);

  new Toggle(enableCanvas, bindToggle("dac_enable"));

  const midiCell = document.createElement("div");
  midiCell.className = "header-cell";
  const midiLbl = document.createElement("span");
  midiLbl.className = "label";
  midiLbl.textContent = "MIDI";
  midiCell.appendChild(midiLbl);
  const stepHost = document.createElement("div");
  midiCell.appendChild(stepHost);
  routingStepField(stepHost, { kind: "dac", index: 0 });
  header.appendChild(midiCell);

  host.appendChild(header);

  /* ---- Body: SAMPLE strip + PLAYBACK group ---------------------------- */
  const body = document.createElement("div");
  body.className = "d-body";
  host.appendChild(body);

  const sampleGroup = makeSampleGroup();
  body.appendChild(sampleGroup.root);

  const playback = makePlaybackGroup();
  body.appendChild(playback.root);

  // Refresh the strip once the page is up — covers the case where the
  // processor started with a dev WAV preloaded.
  refreshDacInfo(sampleGroup);
  sampleGroup.refresh = () => refreshDacInfo(sampleGroup);
}

function makeSampleGroup() {
  const root = document.createElement("div");
  root.className = "d-panel bevel-raised";

  const head = document.createElement("div");
  head.className = "d-panel-head";
  const headLbl = document.createElement("span");
  headLbl.className = "label";
  headLbl.textContent = "SAMPLE";
  head.appendChild(headLbl);
  root.appendChild(head);

  const body = document.createElement("div");
  body.className = "d-panel-body sample-body";
  root.appendChild(body);

  // Row 1: LOAD WAV button + filename
  const row1 = document.createElement("div");
  row1.className = "d-row";
  const loadBtn = document.createElement("button");
  loadBtn.type = "button";
  loadBtn.className = "d-button bevel-raised label";
  loadBtn.textContent = "LOAD WAV…";
  row1.appendChild(loadBtn);
  const fname = document.createElement("span");
  fname.className = "label sample-name";
  fname.textContent = "— no sample —";
  row1.appendChild(fname);
  body.appendChild(row1);

  // Row 2: waveform display
  const wfCanvas = document.createElement("canvas");
  wfCanvas.width = NUM_PEAK_BUCKETS;
  wfCanvas.height = 50;
  wfCanvas.style.width = NUM_PEAK_BUCKETS + "px";
  wfCanvas.style.height = "50px";
  wfCanvas.className = "bevel-inset waveform-display";
  body.appendChild(wfCanvas);
  const waveform = new WaveformDisplay(wfCanvas);

  // Row 3: length + bit-depth
  const meta = document.createElement("span");
  meta.className = "label sample-meta";
  meta.textContent = "";
  body.appendChild(meta);

  // Row 4: CLEAR button
  const row4 = document.createElement("div");
  row4.className = "d-row";
  const clearBtn = document.createElement("button");
  clearBtn.type = "button";
  clearBtn.className = "d-button bevel-raised label";
  clearBtn.textContent = "CLEAR";
  clearBtn.disabled = true;
  row4.appendChild(clearBtn);
  body.appendChild(row4);

  const group = {
    root, loadBtn, fname, waveform, meta, clearBtn, refresh: null,
  };

  loadBtn.addEventListener("click", async () => {
    const r = await loadWavDialogFn();
    if (r && r.ok) {
      applyDacInfo(group, r.info);
    }
    // Failures from cancelled dialog: silent. Real load errors come through
    // the notify toast (the native function emits a `notify` event before
    // resolving).
  });

  clearBtn.addEventListener("click", async () => {
    await clearDacFn();
    applyDacInfo(group, null);
  });

  return group;
}

function makePlaybackGroup() {
  const root = document.createElement("div");
  root.className = "d-panel bevel-raised";

  const head = document.createElement("div");
  head.className = "d-panel-head";
  const headLbl = document.createElement("span");
  headLbl.className = "label";
  headLbl.textContent = "PLAYBACK";
  head.appendChild(headLbl);
  root.appendChild(head);

  const body = document.createElement("div");
  body.className = "d-panel-body";
  root.appendChild(body);

  // RATE
  const rateRow = document.createElement("div");
  rateRow.className = "d-row";
  const rateLbl = document.createElement("span");
  rateLbl.className = "label";
  rateLbl.textContent = "RATE";
  rateRow.appendChild(rateLbl);
  const rateCanvas = document.createElement("canvas");
  rateCanvas.width = 130; rateCanvas.height = 14;
  rateRow.appendChild(rateCanvas);
  body.appendChild(rateRow);
  new SectionTabs(rateCanvas, bindCombo("dac_rate"),
                  { style: "pill", fontSize: 8,
                    labels: ["8000", "11025", "22050"] });

  // MODE
  const modeRow = document.createElement("div");
  modeRow.className = "d-row";
  const modeLbl = document.createElement("span");
  modeLbl.className = "label";
  modeLbl.textContent = "MODE";
  modeRow.appendChild(modeLbl);
  const modeCanvas = document.createElement("canvas");
  modeCanvas.width = 130; modeCanvas.height = 14;
  modeRow.appendChild(modeCanvas);
  body.appendChild(modeRow);
  new SectionTabs(modeCanvas, bindCombo("dac_mode"),
                  { style: "pill", fontSize: 8,
                    labels: ["ONE-SHOT", "LOOP"] });

  // LEVEL knob + LED readout
  const levelRow = document.createElement("div");
  levelRow.className = "d-row d-level-row";
  const levelLbl = document.createElement("span");
  levelLbl.className = "label";
  levelLbl.textContent = "LEVEL";
  levelRow.appendChild(levelLbl);
  const knobCanvas = document.createElement("canvas");
  knobCanvas.width = 36; knobCanvas.height = 36;
  levelRow.appendChild(knobCanvas);
  const readout = document.createElement("canvas");
  levelRow.appendChild(readout);
  body.appendChild(levelRow);
  const levelBinding = bindSlider("dac_level");
  new Knob(knobCanvas, levelBinding, { defaultNormalised: 1.0 });
  new LedReadout(readout, {
    binding: levelBinding, widthChars: 3,
    format: (s) => Math.round(s * 100).toString(),
  });

  return { root };
}

function applyDacInfo(group, info) {
  if (!info || info.empty) {
    group.fname.textContent = "— no sample —";
    group.meta.textContent = "";
    group.waveform.setPeaks(null);
    group.clearBtn.disabled = true;
  } else {
    group.fname.textContent = info.name || "(unnamed)";
    const len = formatLength(info.lengthSec ?? 0);
    const bits = (info.bitDepth ?? 8) + "-bit";
    group.meta.textContent = `${len} · ${bits}`;
    group.waveform.setPeaks(info.peaks || []);
    group.clearBtn.disabled = false;
  }
}

function formatLength(sec) {
  if (sec < 1) return Math.round(sec * 1000) + " ms";
  return sec.toFixed(2) + " s";
}

export async function refreshDacInfo(group) {
  try {
    const info = await getDacInfoFn(NUM_PEAK_BUCKETS);
    applyDacInfo(group, info);
  } catch {
    applyDacInfo(group, null);
  }
}
