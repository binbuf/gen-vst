/*
 * algo-diagram — the 8 hard-coded YM2612 algorithm routings, drawn as a mini
 * wiring diagram inside a green-LCD inset (genny-ui.md "Left Column").
 *
 * Each algorithm shows the four operators S1..S4 as labeled boxes connected by
 * lines indicating which feed which. Carriers are coloured with the bright LCD
 * pixel; modulators use the dimmer base-hi. The diagram redraws whenever the
 * bound `alg` parameter changes (and therefore also on any FM-channel page
 * switch, since the attachment rebind fires valueChanged).
 *
 * The 8 topologies follow docs/design/02-fm-synthesis.md "FM Algorithms".
 */

import { setupPixelCanvas, palette, drawBevel, snap, drawLabel } from "./pixel.js";

// Per-algorithm carrier mask: bit i set => OP(i+1) is a carrier. Matches
// kCarrierMaskByAlg in src/FmRegisterMap.h — keep these two in lockstep.
//   alg 0..3: OP4 only
//   alg 4:    OP2 + OP4
//   alg 5:    OP2 + OP3 + OP4
//   alg 6:    OP2 + OP3 + OP4
//   alg 7:    all four carriers
const CARRIER_MASKS = [
  0b1000, 0b1000, 0b1000, 0b1000,
  0b1010, 0b1110, 0b1110, 0b1111,
];

// Operator names as drawn on the diagram (S1..S4). The Patch operator index 0
// is OP1/S1, etc. — see 02-fm-synthesis.md.
const OP_LABELS = ["S1", "S2", "S3", "S4"];

// Per-algorithm placement of each operator box, in cell coordinates of a 4x3
// grid. Choosing fixed cells keeps the layout pixel-snapped regardless of
// canvas size. Each entry: [col, row] for ops 1..4 in order.
//
// The layouts try to read left-to-right as series chains and stacked for
// parallel sums. They are derived from the FM-Algorithms table in
// docs/design/02-fm-synthesis.md.
const LAYOUTS = [
  // alg 0: S1->S2->S3->S4 (series chain)
  [[0, 1], [1, 1], [2, 1], [3, 1]],
  // alg 1: (S1+S2)->S3->S4
  [[0, 0], [0, 2], [1, 1], [2, 1]],
  // alg 2: S1 + (S2->S3) -> S4
  [[1, 0], [0, 2], [1, 2], [2, 1]],
  // alg 3: S1 -> (S2+S3) -> S4
  [[0, 1], [1, 0], [1, 2], [2, 1]],
  // alg 4: (S1->S2) + (S3->S4) -> out
  [[0, 0], [1, 0], [0, 2], [1, 2]],
  // alg 5: S1 -> (S2, S3, S4) -> out (three carriers)
  [[0, 1], [1, 0], [1, 1], [1, 2]],
  // alg 6: (S1->S2) + S3 + S4 -> out
  [[0, 0], [1, 0], [1, 1], [1, 2]],
  // alg 7: S1 + S2 + S3 + S4 -> out (additive)
  [[0, 0], [0, 1], [0, 2], [0, 3]],
];

// Per-algorithm modulation edges (modulator op -> target op or "+", where "+"
// means "sums into the output bus"). Each edge: [from, to]. `to` of -1 means
// the carrier feeds the mix bus directly (drawn as a horizontal line to the
// right edge).
const EDGES = [
  // alg 0: 1->2, 2->3, 3->4, 4->out
  [[0, 1], [1, 2], [2, 3], [3, -1]],
  // alg 1: 1->3, 2->3, 3->4, 4->out
  [[0, 2], [1, 2], [2, 3], [3, -1]],
  // alg 2: 1->4, 2->3, 3->4, 4->out
  [[0, 3], [1, 2], [2, 3], [3, -1]],
  // alg 3: 1->2, 1->3, 2->4, 3->4, 4->out
  [[0, 1], [0, 2], [1, 3], [2, 3], [3, -1]],
  // alg 4: 1->2, 3->4, 2->out, 4->out
  [[0, 1], [2, 3], [1, -1], [3, -1]],
  // alg 5: 1->2, 1->3, 1->4, 2->out, 3->out, 4->out
  [[0, 1], [0, 2], [0, 3], [1, -1], [2, -1], [3, -1]],
  // alg 6: 1->2, 2->out, 3->out, 4->out
  [[0, 1], [1, -1], [2, -1], [3, -1]],
  // alg 7: all four to out
  [[0, -1], [1, -1], [2, -1], [3, -1]],
];

export class AlgoDiagram {
  constructor(canvas, binding) {
    this.canvas = canvas;
    this.binding = binding;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this._unsubscribe = this.binding.onChange(() => this.render());
    this._unsubscribeProps = this.binding.onProperties?.(() => this.render()) ?? null;

    this.render();
  }

  _currentAlg() {
    return Math.max(0, Math.min(7, Math.round(this.binding.getScaled())));
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const alg = this._currentAlg();
    const layout = LAYOUTS[alg];
    const edges = EDGES[alg];
    const carriers = CARRIER_MASKS[alg];

    // LCD-green base + recessed inset, same vocabulary as lcd-list.
    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    // 4-column x 4-row cell grid laid out within the bezel insets. Boxes are
    // ~18x12; lines connect their centres on integer pixels.
    const padX = 6, padY = 6;
    const cellW = Math.floor((this.w - padX * 2 - 12) / 4);   // last col is "out"
    const cellH = Math.floor((this.h - padY * 2) / 4);
    const boxW = Math.min(cellW - 4, 18);
    const boxH = Math.min(cellH - 2, 12);

    const cellCenter = (col, row) => ({
      x: padX + col * cellW + Math.floor(cellW / 2),
      y: padY + row * cellH + Math.floor(cellH / 2),
    });

    // Draw modulation edges first so they sit under the boxes.
    ctx.fillStyle = pal["lcd-pixel"];
    for (const [from, to] of edges) {
      const a = cellCenter(layout[from][0], layout[from][1]);
      if (to === -1) {
        // Carrier -> out: horizontal line to the right edge.
        const outX = this.w - padX - 2;
        this._hLine(a.x, outX, a.y);
      } else {
        const b = cellCenter(layout[to][0], layout[to][1]);
        // L-shaped polyline: horizontal then vertical (or vice versa).
        this._hLine(a.x, b.x, a.y);
        this._vLine(b.x, a.y, b.y);
      }
    }

    // Draw each operator box. Carrier vs modulator colouring per genny-ui.md:
    // carriers stand out brighter than modulators.
    for (let op = 0; op < 4; ++op) {
      const [col, row] = layout[op];
      const cc = cellCenter(col, row);
      const isCarrier = ((carriers >> op) & 1) === 1;
      const fill = isCarrier ? pal["lcd-pixel-hi"] : pal["lcd-base-hi"];
      const text = isCarrier ? pal["lcd-base"] : pal["lcd-pixel"];

      const bx = cc.x - Math.floor(boxW / 2);
      const by = cc.y - Math.floor(boxH / 2);
      ctx.fillStyle = fill;
      ctx.fillRect(bx, by, boxW, boxH);
      drawBevel(ctx, bx, by, boxW, boxH, true);

      // Label centered in the box. We pick 8px font, ~12px tall — close enough.
      const label = OP_LABELS[op];
      const tx = bx + Math.floor((boxW - label.length * 8) / 2);
      const ty = by + Math.floor((boxH - 8) / 2);
      drawLabel(ctx, tx, ty, label, 8, text);
    }
  }

  _hLine(x1, x2, y) {
    const ctx = this.ctx;
    const xa = Math.min(x1, x2);
    const xb = Math.max(x1, x2);
    ctx.fillRect(snap(xa), snap(y), xb - xa + 1, 1);
  }

  _vLine(x, y1, y2) {
    const ctx = this.ctx;
    const ya = Math.min(y1, y2);
    const yb = Math.max(y1, y2);
    ctx.fillRect(snap(x), snap(ya), 1, yb - ya + 1);
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
