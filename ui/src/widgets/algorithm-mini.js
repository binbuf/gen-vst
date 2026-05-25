// `algorithm-mini` widget — canvas read-only diagram of the currently
// selected YM2612 algorithm's operator topology.
//
// Eight hard-coded routings; the widget renders one based on
// setAlgorithm(idx). No click handler — the selection picker is `algo-grid`.
// Recipe in 09-visual-spec.md *Algorithm topology diagram (canvas)*: 1.4 px
// strokes in --lcd-text-on on --lcd-bg with phosphor bloom; ≈ 112 px tile.

import { applyTooltip } from "./tooltip-content.js";

function cssVar(name, fallback = "#000") {
  const v = getComputedStyle(document.documentElement)
    .getPropertyValue(name).trim();
  return v || fallback;
}

// Each algorithm describes (1) the operator-box positions on a 4x4 grid
// (slot 0..15 -> {col, row}), and (2) the carrier set + the modulator
// arrows. Coordinates are 0..3 in grid space; the widget translates to
// canvas pixels.
//
// YM2612 algorithm topologies, simplified to fit a 112 px tile. Each entry
// is { boxes: [{op,col,row}, ...], lines: [[fromOp, toOp], ...], outputs:
// [op...] }. fromOp = -1 means a feedback loop into the source op.
//
// These are visual sketches — readable at-a-glance representations of the
// algorithm number, not pin-accurate datasheet diagrams. The user sees the
// numeric label `Algo: N` as the authoritative cue.
const ALGOS = [
  // 0: 1->2->3->4
  { boxes:[{op:1,col:0,row:0},{op:2,col:0,row:1},{op:3,col:0,row:2},{op:4,col:0,row:3}],
    lines:[[1,2],[2,3],[3,4]], outputs:[4] },
  // 1: (1+2)->3->4
  { boxes:[{op:1,col:0,row:0},{op:2,col:1,row:0},{op:3,col:0,row:1},{op:4,col:0,row:2}],
    lines:[[1,3],[2,3],[3,4]], outputs:[4] },
  // 2: 1->2; (2+3)->4
  { boxes:[{op:1,col:0,row:0},{op:2,col:0,row:1},{op:3,col:1,row:0},{op:4,col:0,row:2}],
    lines:[[1,2],[2,4],[3,4]], outputs:[4] },
  // 3: 1->2; 3->4 (parallel chains, summed)
  { boxes:[{op:1,col:0,row:0},{op:2,col:0,row:1},{op:3,col:1,row:0},{op:4,col:1,row:1}],
    lines:[[1,2],[3,4]], outputs:[2,4] },
  // 4: 1->2; 3->4; (sum)
  { boxes:[{op:1,col:0,row:0},{op:2,col:0,row:2},{op:3,col:1,row:0},{op:4,col:1,row:2}],
    lines:[[1,2],[3,4]], outputs:[2,4] },
  // 5: 1->(2,3,4)
  { boxes:[{op:1,col:0,row:0},{op:2,col:1,row:1},{op:3,col:0,row:2},{op:4,col:1,row:2}],
    lines:[[1,2],[1,3],[1,4]], outputs:[2,3,4] },
  // 6: 1->2; (2+3+4)
  { boxes:[{op:1,col:0,row:0},{op:2,col:0,row:1},{op:3,col:1,row:0},{op:4,col:1,row:1}],
    lines:[[1,2]], outputs:[2,3,4] },
  // 7: 1+2+3+4 (all carriers)
  { boxes:[{op:1,col:0,row:0},{op:2,col:1,row:0},{op:3,col:0,row:1},{op:4,col:1,row:1}],
    lines:[], outputs:[1,2,3,4] },
];

export function mount(host, opts = {}) {
  const {
    size = 112,
    tipId = null,
  } = opts;

  host.style.display = "inline-block";
  host.style.width  = `${size}px`;
  host.style.height = `${size}px`;
  if (tipId) applyTooltip(host, tipId);

  const canvas = document.createElement("canvas");
  canvas.style.width  = `${size}px`;
  canvas.style.height = `${size}px`;
  canvas.style.display = "block";
  host.appendChild(canvas);

  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  canvas.width  = Math.round(size * dpr);
  canvas.height = Math.round(size * dpr);
  ctx.scale(dpr, dpr);
  ctx.imageSmoothingEnabled = true;

  let algoIdx = 0;

  const draw = () => {
    const w = size, h = size;
    ctx.clearRect(0, 0, w, h);

    // Background
    ctx.fillStyle = cssVar("--lcd-bg", "#0d1424");
    ctx.fillRect(0, 0, w, h);
    ctx.strokeStyle = "rgba(0,0,0,0.7)";
    ctx.lineWidth = 1;
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);

    const algo = ALGOS[Math.max(0, Math.min(ALGOS.length - 1, algoIdx))];
    const pad = 14;
    const usableW = w - pad * 2;
    const usableH = h - pad * 2 - 12;   // leave room for the algo number label

    const cols = 2, rows = 4;
    const cellW = usableW / cols;
    const cellH = usableH / rows;
    const boxSize = Math.min(cellW, cellH) * 0.6;

    // Compute each operator's centre and box rect by index.
    const opCentre = new Map();
    const opRect   = new Map();
    for (const b of algo.boxes) {
      const cx = pad + b.col * cellW + cellW / 2;
      const cy = pad + b.row * cellH + cellH / 2;
      opCentre.set(b.op, { x: cx, y: cy });
      opRect.set(b.op, {
        x: cx - boxSize / 2,
        y: cy - boxSize / 2,
        w: boxSize, h: boxSize,
      });
    }

    const stroke = cssVar("--lcd-text-on", "#4ea0ff");
    ctx.strokeStyle = stroke;
    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur  = 3;
    ctx.lineWidth   = 1.4;
    ctx.lineCap     = "round";

    // 1. Modulator lines (between operator centres).
    for (const [from, to] of algo.lines) {
      const a = opCentre.get(from);
      const b = opCentre.get(to);
      if (!a || !b) continue;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }
    ctx.shadowBlur = 0;

    // 2. Operator boxes -- drawn after lines so they sit on top.
    ctx.lineWidth = 1.2;
    for (const b of algo.boxes) {
      const r = opRect.get(b.op);
      // dark fill so the box reads as solid
      ctx.fillStyle = cssVar("--lcd-bg-edge", "#06080f");
      ctx.fillRect(r.x, r.y, r.w, r.h);
      ctx.strokeStyle = stroke;
      ctx.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);

      // Operator number label centred inside the box.
      ctx.fillStyle = stroke;
      ctx.font = `500 ${Math.round(boxSize * 0.55)}px "IBM Plex Mono", monospace`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
      ctx.shadowBlur = 3;
      ctx.fillText(String(b.op), r.x + r.w / 2, r.y + r.h / 2 + 1);
      ctx.shadowBlur = 0;
    }

    // 3. Output indicator: little down-arrow under each carrier box.
    ctx.fillStyle = stroke;
    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur = 3;
    for (const op of algo.outputs) {
      const r = opRect.get(op);
      if (!r) continue;
      const ax = r.x + r.w / 2;
      const ay = r.y + r.h + 2;
      ctx.beginPath();
      ctx.moveTo(ax - 4, ay);
      ctx.lineTo(ax + 4, ay);
      ctx.lineTo(ax, ay + 5);
      ctx.closePath();
      ctx.fill();
    }
    ctx.shadowBlur = 0;

    // 4. Algorithm number caption at the bottom.
    ctx.fillStyle = stroke;
    ctx.font = "500 9px 'IBM Plex Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "alphabetic";
    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur = 3;
    ctx.fillText(`ALGO ${algoIdx + 1}`, w / 2, h - 4);
    ctx.shadowBlur = 0;
  };

  draw();

  return {
    setAlgorithm(idx) {
      algoIdx = Math.max(0, Math.min(ALGOS.length - 1, idx));
      draw();
    },
    getAlgorithm() { return algoIdx; },
    dispose() {
      if (canvas.parentNode === host) host.removeChild(canvas);
    },
  };
}
