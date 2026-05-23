/*
 * step-field — small numeric value with up/down arrows. Used by the channel
 * routing column for MIDI channel and Transpose (semitones / octaves)
 * (genny-ui.md "Center Column — Instruments & Channel Routing").
 *
 * Binds to a slider relay because the underlying parameter is a discrete
 * integer (AudioParameterInt) — the relay exposes its [start, end, interval]
 * so we step in legal units. The numeric value is drawn in 5x7 dot-matrix to
 * match the rest of the readouts.
 */

import {
  setupPixelCanvas, palette, drawBevel, snap, drawLedReadout,
  GLYPH_H, readoutWidth,
} from "./pixel.js";

const ARROW_W = 9;
const ARROW_H = 7;
const PAD = 3;

export class StepField {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;
    this.widthChars = options.widthChars ?? 3;
    this.signed = !!options.signed;
    this.minScaled = options.min ?? null;
    this.maxScaled = options.max ?? null;
    this.step      = options.step ?? 1;

    // Compute total size: bevel + arrow buttons stacked on the right + dot
    // grid on the left.
    const grid = readoutWidth(this.widthChars);
    const innerW = grid + PAD * 2 + ARROW_W + PAD;
    const innerH = Math.max(GLYPH_H + PAD * 2, ARROW_H * 2 + 1);
    this.canvas.style.width  = innerW + 2 + "px";
    this.canvas.style.height = innerH + 2 + "px";

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    // Cache arrow hit rectangles (inside-bevel coordinates).
    const ax = 1 + grid + PAD * 2;
    const upY = 1 + Math.floor((innerH - ARROW_H * 2 - 1) / 2);
    const dnY = upY + ARROW_H + 1;
    this._up = { x: ax, y: upY, w: ARROW_W, h: ARROW_H };
    this._dn = { x: ax, y: dnY, w: ARROW_W, h: ARROW_H };

    canvas.addEventListener("pointerdown", e => this._onClick(e));

    this._unsubscribe = this.binding.onChange(() => this.render());
    this._unsubscribeProps =
      this.binding.onProperties?.(() => this.render()) ?? null;
    this.render();
  }

  _bounds() {
    const props = this.binding.properties ?? {};
    const lo = this.minScaled ?? props.start ?? 0;
    const hi = this.maxScaled ?? props.end   ?? 1;
    const step = this.step || props.interval || 1;
    return { lo, hi, step };
  }

  _clamp(value) {
    const { lo, hi } = this._bounds();
    return Math.max(lo, Math.min(hi, value));
  }

  _step(direction) {
    const { step } = this._bounds();
    const next = this._clamp(this.binding.getScaled() + direction * step);
    const props = this.binding.properties ?? {};
    const start = props.start ?? 0;
    const end   = props.end ?? 1;
    const norm  = end === start ? 0 : (next - start) / (end - start);
    this.binding.beginGesture();
    this.binding.setNormalised(Math.max(0, Math.min(1, norm)));
    this.binding.endGesture();
  }

  _onClick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    if (x >= this._up.x && x < this._up.x + this._up.w &&
        y >= this._up.y && y < this._up.y + this._up.h) {
      this._step(+1);
    } else if (x >= this._dn.x && x < this._dn.x + this._dn.w &&
               y >= this._dn.y && y < this._dn.y + this._dn.h) {
      this._step(-1);
    }
  }

  _text() {
    const scaled = this.binding.getScaled();
    const n = Math.round(scaled);
    if (this.signed && n < 0) return "-" + Math.abs(n).toString();
    return Math.abs(n).toString();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.fillStyle = pal["led-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    // LED digits on the left.
    drawLedReadout(ctx, 1 + PAD, 1 + Math.floor((this.h - 2 - GLYPH_H) / 2),
                   this._text(), this.widthChars);

    // Arrow buttons on the right (raised panel + a chevron).
    this._drawArrow(this._up, true);
    this._drawArrow(this._dn, false);
  }

  _drawArrow(rect, up) {
    const ctx = this.ctx;
    const pal = palette();
    ctx.fillStyle = pal["panel"];
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    drawBevel(ctx, rect.x, rect.y, rect.w, rect.h, true);

    ctx.fillStyle = pal["label"];
    const cx = rect.x + Math.floor(rect.w / 2);
    if (up) {
      ctx.fillRect(cx,     rect.y + 2, 1, 1);
      ctx.fillRect(cx - 1, rect.y + 3, 3, 1);
      ctx.fillRect(cx - 2, rect.y + 4, 5, 1);
    } else {
      ctx.fillRect(cx - 2, rect.y + 2, 5, 1);
      ctx.fillRect(cx - 1, rect.y + 3, 3, 1);
      ctx.fillRect(cx,     rect.y + 4, 1, 1);
    }
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
