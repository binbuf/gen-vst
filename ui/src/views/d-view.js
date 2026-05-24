/*
 * D (DAC) section view — 08-ui-views.md view 3.
 *
 * Task 31 multi-sample upgrade: the 4 x 5 note grid is now a functional kit.
 * Each cell binds to its own loaded WAV; clicking a cell opens a native
 * file chooser that loads into that cell only. Triggering a MIDI note in
 * the grid range (C-3..G-4) plays the matched cell's sample at the cell's
 * stored rate. Empty cells stay silent.
 *
 * The bottom playback strip keeps the HZ + LEV controls. HZ is now the
 * default rate the *next* per-cell load resamples to (per Task 31 spec);
 * LEV is the global DAC gain. The single LOAD WAV / CLEAR / filename
 * triplet from the Task 26 preview is gone — per-cell loading replaces it.
 */

import {
  Knob, Slider, Toggle, LedReadout,
  bindSlider, bindToggle, bindCombo,
  setupPixelCanvas, palette, drawBevel, snap, drawLabel,
} from "../widgets/index.js";
import * as Juce from "../juce/index.js";
import { routingStepField } from "./routing-controls.js";

const loadDacCellWavFn = Juce.getNativeFunction("loadDacCellWav");
const clearDacCellFn   = Juce.getNativeFunction("clearDacCell");
const getDacKitFn      = Juce.getNativeFunction("getDacKit");

const RATE_LABELS = ["8000", "11025", "22050"];

// Note grid — 4 rows x 5 cols, starting at C-3 and walking up chromatically
// (per Genny screenshot 094629). Labels include the sharp marker so cells
// stay visually distinct in a glance. The cellIndex for row r, col c is
// r * 5 + c, mapping to MIDI note 48 + cellIndex.
const NOTE_GRID = [
  ["C-3",  "C#-3", "D-3",  "D#-3", "E-3"],
  ["F-3",  "F#-3", "G-3",  "G#-3", "A-3"],
  ["A#-3", "B-3",  "C-4",  "C#-4", "D-4"],
  ["D#-4", "E-4",  "F-4",  "F#-4", "G-4"],
];

const GRID_ROWS = 4;
const GRID_COLS = 5;
const GRID_CELLS = GRID_ROWS * GRID_COLS;

// Strip a WAV's file extension and uppercase-truncate so a long file name
// fits the small cell label (e.g. "KICK_FAT_01.wav" -> "KICK_FAT").
function shortenSampleName(raw) {
  if (!raw) return "";
  const noExt = raw.replace(/\.[^.]+$/, "");
  return noExt.toUpperCase().slice(0, 8);
}

function pushNotify(level, message) {
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

  /* ---- Body: interactive note grid + bottom HZ/LEV strip -------------- */
  const body = document.createElement("div");
  body.className = "d-body";
  host.appendChild(body);

  const grid = makeNoteGrid();
  body.appendChild(grid.root);

  const strip = makePlaybackStrip();
  body.appendChild(strip.root);

  // Refresh once on mount to pick up any preloaded cells (state-restored
  // projects, dev WAV in cell 12, etc.).
  refreshDacKit(grid);
}

/* -------------------------------------------------------------------- */
/* Note-grid — interactive (Task 31)                                    */
/* -------------------------------------------------------------------- */

function makeNoteGrid() {
  const root = document.createElement("div");
  root.className = "d-note-grid bevel-inset";

  const canvas = document.createElement("canvas");
  canvas.width = 540;
  canvas.height = 150;
  canvas.className = "d-note-grid-canvas";
  canvas.dataset.tip = "CLICK A NOTE CELL TO LOAD A WAV";
  root.appendChild(canvas);

  const setup = setupPixelCanvas(canvas);
  const ctx = setup.ctx;
  const w = setup.width;
  const h = setup.height;

  // Per-cell loaded label cache. Index 0..19; null = empty cell.
  const cellLabels = new Array(GRID_CELLS).fill(null);

  function cellMetrics() {
    return {
      cellW: Math.floor((w - 2) / GRID_COLS),
      cellH: Math.floor((h - 2) / GRID_ROWS),
    };
  }

  function render() {
    const pal = palette();

    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, false);

    const { cellW, cellH } = cellMetrics();
    ctx.fillStyle = pal["lcd-text-dark"];
    for (let c = 1; c < GRID_COLS; ++c) ctx.fillRect(snap(1 + c * cellW), 1, 1, h - 2);
    for (let r = 1; r < GRID_ROWS; ++r) ctx.fillRect(1, snap(1 + r * cellH), w - 2, 1);

    // Each cell has two lines: the note name (top-left, dim olive) and, when
    // a sample is loaded, the short-form sample name (centre, bright pixel).
    // Empty cells show only the note name so the grid still reads as a
    // populated keymap.
    for (let r = 0; r < GRID_ROWS; ++r) {
      for (let c = 0; c < GRID_COLS; ++c) {
        const idx = r * GRID_COLS + c;
        const noteLabel = NOTE_GRID[r][c];
        const cx = 1 + c * cellW;
        const cy = 1 + r * cellH;

        drawLabel(ctx, cx + 4, cy + 4, noteLabel, 8, pal["lcd-text-dark"]);

        const sample = cellLabels[idx];
        if (sample) {
          const pxPerChar = 8;
          const labelW = sample.length * pxPerChar;
          const lx = cx + Math.floor((cellW - labelW) / 2);
          const ly = cy + Math.floor((cellH - 8) / 2);
          drawLabel(ctx, lx, ly, sample, 8, pal["lcd-pixel-hi"]);
        }
      }
    }
  }

  function applyKit(kit) {
    if (!kit || !Array.isArray(kit.cells)) return;
    for (const cell of kit.cells) {
      if (typeof cell?.index !== "number") continue;
      if (cell.empty) {
        cellLabels[cell.index] = null;
      } else {
        cellLabels[cell.index] = shortenSampleName(cell.name) || "LOADED";
      }
    }
    render();
  }

  // Hit-test the click against the cell grid and pass the cell index to
  // the native chooser. The grid is 5 cols x 4 rows of equal-sized cells
  // inset by 1px (the bevel border).
  canvas.addEventListener("click", async (ev) => {
    const rect = canvas.getBoundingClientRect();
    const x = (ev.clientX - rect.left) * (canvas.width  / rect.width);
    const y = (ev.clientY - rect.top)  * (canvas.height / rect.height);

    const { cellW, cellH } = cellMetrics();
    const col = Math.floor((x - 1) / cellW);
    const row = Math.floor((y - 1) / cellH);
    if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return;
    const cellIdx = row * GRID_COLS + col;

    // Shift-click clears the cell instead of loading; the toast acknowledges
    // the action so the user has feedback even when the cell was empty.
    if (ev.shiftKey) {
      await clearDacCellFn(cellIdx);
      cellLabels[cellIdx] = null;
      render();
      pushNotify("info", `Cleared ${NOTE_GRID[row][col]}`);
      return;
    }

    try {
      const r = await loadDacCellWavFn(cellIdx);
      if (r && r.ok) applyKit(r.kit);
    } catch { /* dev harness */ }
  });
  canvas.style.cursor = "pointer";

  render();

  return { root, canvas, render, applyKit };
}

/* -------------------------------------------------------------------- */
/* Playback strip — HZ knob + LEV slider                                 */
/* -------------------------------------------------------------------- */

function makePlaybackStrip() {
  const root = document.createElement("div");
  root.className = "d-playback-strip bevel-raised";

  // HZ knob — Task 31 repurpose: this is the *default rate the next per-cell
  // load resamples to*, not the global playback rate. Per-cell rates are
  // stored alongside each cell's PCM and survive state save/restore.
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

  // LEV slider — bound to the existing dac_level apvts param without engine
  // change. Drives the kit-wide playback level (matches Genny).
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

  // Hint text on the right edge of the strip — reminds the user that the
  // grid is interactive. Fills the remaining flex space so the strip layout
  // matches Genny's bezel-strip wording column.
  const hint = document.createElement("span");
  hint.className = "label d-strip-hint";
  hint.textContent = "CLICK CELL: LOAD • SHIFT-CLICK: CLEAR";
  root.appendChild(hint);

  return { root };
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
    beginGesture() {},
    endGesture() {},
    onChange(cb)     { return combo.onChange(cb); },
    onProperties(cb) { return combo.onProperties?.(cb); },
  };
}

/* -------------------------------------------------------------------- */
/* DAC-kit refresh                                                       */
/* -------------------------------------------------------------------- */

export async function refreshDacKit(grid) {
  try {
    const kit = await getDacKitFn();
    grid.applyKit(kit);
  } catch {
    /* dev harness — no native fn registered */
  }
}
