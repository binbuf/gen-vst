/*
 * algo-buttons — eight numbered ALGORITHM buttons (1..8). The currently
 * selected button is wrapped in a red stamped ring (genny-ui.md "Left Column").
 *
 * Binds to a slider relay holding the `alg` parameter (AudioParameterInt 0..7);
 * clicking a button writes the new value. The widget redraws on the relay's
 * value-changed event, so paging to a different FM channel (which rebinds the
 * attachment) flashes the new selection in one batch — same path as every
 * other FM widget.
 */

import { setupPixelCanvas, palette, drawBevel, snap, drawLabel } from "./pixel.js";

const NUM_BUTTONS = 8;
const BUTTON_W = 16;
const BUTTON_H = 16;
const GAP = 2;

export class AlgoButtons {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;

    // Size canvas to fit the full row plus a 2px ring border on each side.
    const totalW = NUM_BUTTONS * BUTTON_W + (NUM_BUTTONS - 1) * GAP + 4;
    const totalH = BUTTON_H + 4;
    canvas.style.width = totalW + "px";
    canvas.style.height = totalH + "px";

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    canvas.addEventListener("click", e => this._onClick(e));

    this._unsubscribe = this.binding.onChange(() => this.render());
    this._unsubscribeProps = this.binding.onProperties?.(() => this.render()) ?? null;

    this.render();
  }

  _selectedIndex() {
    return Math.max(0, Math.min(NUM_BUTTONS - 1, Math.round(this.binding.getScaled())));
  }

  _rectFor(i) {
    return {
      x: 2 + i * (BUTTON_W + GAP),
      y: 2,
      w: BUTTON_W,
      h: BUTTON_H,
    };
  }

  _onClick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    for (let i = 0; i < NUM_BUTTONS; ++i) {
      const r = this._rectFor(i);
      if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
        // alg is a 0..7 int. Normalise via the relay's properties so the
        // value lands on a legal step.
        const props = this.binding.properties ?? {};
        const start = props.start ?? 0;
        const end = props.end ?? 7;
        const norm = end === start ? 0 : (i - start) / (end - start);
        this.binding.beginGesture();
        this.binding.setNormalised(Math.max(0, Math.min(1, norm)));
        this.binding.endGesture();
        return;
      }
    }
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.clearRect(0, 0, this.w, this.h);
    ctx.fillStyle = pal["chassis"];
    ctx.fillRect(0, 0, this.w, this.h);

    const sel = this._selectedIndex();

    for (let i = 0; i < NUM_BUTTONS; ++i) {
      const r = this._rectFor(i);

      // Button face — raised panel.
      ctx.fillStyle = pal["panel"];
      ctx.fillRect(r.x, r.y, r.w, r.h);
      drawBevel(ctx, r.x, r.y, r.w, r.h, true);

      // Number label centered.
      const text = String(i + 1);
      const labelX = r.x + Math.floor((r.w - text.length * 8) / 2);
      const labelY = r.y + Math.floor((r.h - 8) / 2);
      drawLabel(ctx, labelX, labelY, text, 8, pal["label"]);

      // Selected: stamped red ring around the button (1px outline 1px outside
      // the button rect). Drawn as 4 fills so it stays a hard square.
      if (i === sel) {
        ctx.fillStyle = pal["select"];
        ctx.fillRect(r.x - 1, r.y - 1, r.w + 2, 1);              // top
        ctx.fillRect(r.x - 1, r.y + r.h, r.w + 2, 1);            // bottom
        ctx.fillRect(r.x - 1, r.y - 1, 1, r.h + 2);              // left
        ctx.fillRect(r.x + r.w, r.y - 1, 1, r.h + 2);            // right
      }
    }
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
