/*
 * D (DAC) section view — 08-ui-views.md view 3, Genny-restyled in Task 26.
 *
 * The panel still drives the single-sample DAC engine (one WAV, three rates,
 * one-shot / loop, level), but the LAYOUT is restyled to look like Genny's
 * multi-sample DAC: a 4x5 read-only note-grid backdrop dominates the body
 * with a "CLICK NOTE TO LOAD SAMPLE" prompt overlay, and the engine
 * controls collapse to a compact bottom strip with a HZ knob, LEV slider,
 * LOAD WAV / CLEAR buttons + the active filename. Clicking any note cell
 * emits a "multi-sample DAC coming soon" toast — the deferral is tracked in
 * docs/tasks/31-dac-multisample.md.
 *
 * Native functions (`loadWavDialog`, `clearDac`, `getDacInfo`) and apvts
 * params (`dac_enable`, `dac_rate`, `dac_mode`, `dac_level`) are unchanged.
 * The HZ knob wraps the existing `dac_rate` AudioParameterChoice in a
 * three-step adapter so the knob snaps to 8000 / 11025 / 22050.
 */

import {
  Knob, Slider, Toggle, LedReadout,
  bindSlider, bindToggle, bindCombo,
  setupPixelCanvas, palette, drawBevel, snap, drawLabel,
} from "../widgets/index.js";
import * as Juce from "../juce/index.js";
import { routingStepField } from "./routing-controls.js";

const loadWavDialogFn = Juce.getNativeFunction("loadWavDialog");
const clearDacFn      = Juce.getNativeFunction("clearDac");
const getDacInfoFn    = Juce.getNativeFunction("getDacInfo");

const RATE_LABELS = ["8000", "11025", "22050"];

// Note grid — 4 rows x 5 cols, starting at C-3 and walking up chromatically
// (per Genny screenshot 094629). Labels include the sharp marker so cells
// stay visually distinct in a glance.
const NOTE_GRID = [
  ["C-3",  "C#-3", "D-3",  "D#-3", "E-3"],
  ["F-3",  "F#-3", "G-3",  "G#-3", "A-3"],
  ["A#-3", "B-3",  "C-4",  "C#-4", "D-4"],
  ["D#-4", "E-4",  "F-4",  "F#-4", "G-4"],
];

// The deferral message + level used for every note-grid click, kept as a
// const so the toast wording stays consistent if the message changes.
const DEFERRED_MSG = "MULTI-SAMPLE DAC COMING SOON — SEE TASK 31";

function pushNotify(level, message) {
  // Same path the rest of the UI uses (fm-view.js showToast). Defer to the
  // standard notify event so the toast pipeline is the single source of truth.
  try {
    window.__JUCE__.backend.emitEvent("notify", { level, message });
  } catch { /* dev harness */ }
}

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

  // "DAC" branding label — printed on the section header strip per
  // genny-ui.md (Genny's DAC panel has a chunky branded label flanking the
  // header controls).
  const brand = document.createElement("span");
  brand.className = "label d-brand";
  brand.textContent = "DAC";
  header.appendChild(brand);

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

  /* ---- Body: note-grid backdrop + bottom playback strip --------------- */
  const body = document.createElement("div");
  body.className = "d-body";
  host.appendChild(body);

  const grid = makeNoteGrid();
  body.appendChild(grid.root);

  const strip = makePlaybackStrip();
  body.appendChild(strip.root);

  // Refresh once on mount to pick up any preloaded WAV (dev builds).
  refreshDacInfo(strip);
  strip.refresh = () => refreshDacInfo(strip);
}

/* -------------------------------------------------------------------- */
/* Note-grid backdrop                                                    */
/* -------------------------------------------------------------------- */

function makeNoteGrid() {
  const root = document.createElement("div");
  root.className = "d-note-grid bevel-inset";

  const canvas = document.createElement("canvas");
  canvas.width = 540;
  canvas.height = 150;
  canvas.className = "d-note-grid-canvas";
  canvas.dataset.tip = "MULTI-SAMPLE DAC GRID (PREVIEW — TASK 31)";
  root.appendChild(canvas);

  const setup = setupPixelCanvas(canvas);
  const ctx = setup.ctx;
  const w = setup.width;
  const h = setup.height;

  function render() {
    const pal = palette();

    // LCD-green base + recessed inset, same vocabulary as the Instruments
    // list so the grid reads as the same family of surfaces.
    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, false);

    // Grid cells. 5 cols x 4 rows, all the same size, divided by thin
    // dark-olive separators (one pixel of --lcd-text-dark so the cells
    // visually subdivide without overwhelming the prompt overlay).
    const cols = 5, rows = 4;
    const cellW = Math.floor((w - 2) / cols);
    const cellH = Math.floor((h - 2) / rows);
    ctx.fillStyle = pal["lcd-text-dark"];
    for (let c = 1; c < cols; ++c) ctx.fillRect(snap(1 + c * cellW), 1, 1, h - 2);
    for (let r = 1; r < rows; ++r) ctx.fillRect(1, snap(1 + r * cellH), w - 2, 1);

    // Note labels — dark olive ink in the top-left of each cell so the
    // grid reads like a populated key map even though the panel is
    // currently a backdrop.
    for (let r = 0; r < rows; ++r) {
      for (let c = 0; c < cols; ++c) {
        const label = NOTE_GRID[r][c];
        const tx = 1 + c * cellW + 4;
        const ty = 1 + r * cellH + 4;
        drawLabel(ctx, tx, ty, label, 8, pal["lcd-text-dark"]);
      }
    }

    // Prompt overlay — centered "CLICK NOTE TO LOAD SAMPLE" in bright
    // phosphor green so it floats over the grid labels.
    const prompt = "CLICK NOTE TO LOAD SAMPLE";
    const pxPerChar = 8;
    const promptW = prompt.length * pxPerChar;
    const promptX = Math.floor((w - promptW) / 2);
    const promptY = Math.floor((h - 8) / 2);
    drawLabel(ctx, promptX, promptY, prompt, 8, pal["lcd-pixel-hi"]);
  }

  // Every cell click emits the deferral toast — the grid is read-only in
  // this revision. The toast pipeline routes through the standard notify
  // event so the user sees the same toast styling as any other status
  // message.
  canvas.addEventListener("click", () => {
    pushNotify("info", DEFERRED_MSG);
  });
  canvas.style.cursor = "pointer";

  render();

  return { root, canvas };
}

/* -------------------------------------------------------------------- */
/* Playback strip — HZ knob + LEV slider + LOAD WAV / CLEAR + filename   */
/* -------------------------------------------------------------------- */

function makePlaybackStrip() {
  const root = document.createElement("div");
  root.className = "d-playback-strip bevel-raised";

  // HZ knob — wraps the dac_rate AudioParameterChoice in a 3-step adapter
  // so the knob snaps to one of three rate indices. The readout shows the
  // selected rate label so the user always knows what the snap landed on.
  const hzCell = document.createElement("div");
  hzCell.className = "d-strip-cell d-hz-cell";
  const hzLbl = document.createElement("span");
  hzLbl.className = "label";
  hzLbl.textContent = "HZ";
  hzCell.appendChild(hzLbl);
  const hzKnob = document.createElement("canvas");
  hzKnob.width = 32; hzKnob.height = 32;
  hzKnob.className = "knob";
  hzCell.appendChild(hzKnob);
  const hzReadout = document.createElement("canvas");
  hzCell.appendChild(hzReadout);
  root.appendChild(hzCell);

  const rateCombo = bindCombo("dac_rate");
  const hzAdapter = comboAsKnobBinding(rateCombo);
  new Knob(hzKnob, hzAdapter, { defaultNormalised: 1.0 });
  new LedReadout(hzReadout, {
    binding: hzAdapter,
    widthChars: 5,
    format: () => RATE_LABELS[rateCombo.getIndex()] || "----",
  });

  // LEV slider — replaces the prior LEVEL knob with a horizontal slider so
  // the layout matches Genny's "LEV" horizontal control. Bound to the
  // existing dac_level apvts param without engine change.
  const levCell = document.createElement("div");
  levCell.className = "d-strip-cell d-lev-cell";
  const levLbl = document.createElement("span");
  levLbl.className = "label";
  levLbl.textContent = "LEV";
  levCell.appendChild(levLbl);
  const levCanvas = document.createElement("canvas");
  levCanvas.width = 80; levCanvas.height = 14;
  levCell.appendChild(levCanvas);
  const levReadout = document.createElement("canvas");
  levCell.appendChild(levReadout);
  root.appendChild(levCell);

  const levelBinding = bindSlider("dac_level");
  new Slider(levCanvas, levelBinding, { defaultNormalised: 1.0 });
  new LedReadout(levReadout, {
    binding: levelBinding,
    widthChars: 3,
    format: (s) => Math.round(s * 100).toString(),
  });

  // Compact LOAD WAV / CLEAR / filename strip. These stay functional so the
  // single-sample DAC engine remains usable while the multi-sample grid is
  // a preview backdrop.
  const wavCell = document.createElement("div");
  wavCell.className = "d-strip-cell d-wav-cell";
  const loadBtn = document.createElement("button");
  loadBtn.type = "button";
  loadBtn.className = "d-button d-strip-button bevel-raised label";
  loadBtn.textContent = "LOAD WAV…";
  wavCell.appendChild(loadBtn);
  const clearBtn = document.createElement("button");
  clearBtn.type = "button";
  clearBtn.className = "d-button d-strip-button bevel-raised label";
  clearBtn.textContent = "CLEAR";
  clearBtn.disabled = true;
  wavCell.appendChild(clearBtn);
  const fname = document.createElement("span");
  fname.className = "label sample-name d-strip-name";
  fname.textContent = "— no sample —";
  wavCell.appendChild(fname);
  root.appendChild(wavCell);

  const group = { root, loadBtn, clearBtn, fname, refresh: null };

  loadBtn.addEventListener("click", async () => {
    const r = await loadWavDialogFn();
    if (r && r.ok) applyDacInfo(group, r.info);
  });
  clearBtn.addEventListener("click", async () => {
    await clearDacFn();
    applyDacInfo(group, null);
  });

  return group;
}

/* -------------------------------------------------------------------- */
/* dac_rate adapter — exposes a ComboBinding as a knob/slider binding    */
/* -------------------------------------------------------------------- */

function comboAsKnobBinding(combo) {
  return {
    getNormalised() {
      const n = combo.getChoices().length;
      return n > 1 ? combo.getIndex() / (n - 1) : 0;
    },
    setNormalised(v) {
      const n = combo.getChoices().length;
      if (n === 0) return;
      const idx = Math.round(Math.max(0, Math.min(1, v)) * (n - 1));
      combo.setIndex(idx);
    },
    getScaled() {
      return combo.getIndex();
    },
    // The Knob widget calls these around drag gestures; the combo backend
    // has no drag-gesture concept, so they are intentional no-ops. A combo
    // index change is a discrete write per snap.
    beginGesture() {},
    endGesture() {},
    onChange(cb)     { return combo.onChange(cb); },
    onProperties(cb) { return combo.onProperties?.(cb); },
  };
}

/* -------------------------------------------------------------------- */
/* DAC-info refresh                                                      */
/* -------------------------------------------------------------------- */

function applyDacInfo(group, info) {
  if (!info || info.empty) {
    group.fname.textContent = "— no sample —";
    group.clearBtn.disabled = true;
  } else {
    group.fname.textContent = info.name || "(unnamed)";
    group.clearBtn.disabled = false;
  }
}

export async function refreshDacInfo(group) {
  try {
    const info = await getDacInfoFn(0);
    applyDacInfo(group, info);
  } catch {
    applyDacInfo(group, null);
  }
}
