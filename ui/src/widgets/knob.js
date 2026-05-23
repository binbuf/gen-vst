/*
 * knob — blue skeuomorphic rotary, ~270 deg sweep, rest at 7 o'clock.
 *
 * Vertical click-drag (up = increase), Shift = fine, double-click = reset
 * (genny-ui.md "Interaction Details"). Drawn entirely in Canvas under the
 * pixel-art rules — no gradients, no anti-aliasing, square corners
 * (05-ui-ux.md). The rotating indicator is a dithered pair of dots stepped
 * radially to keep it pixel-snapped at any value.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const START_ANGLE_DEG = 120;     // 7 o'clock
const SWEEP_DEG       = 270;
const DRAG_RANGE_PX   = 200;
const FINE_FACTOR     = 0.2;

function clamp01(x) { return Math.min(1, Math.max(0, x)); }

export class Knob {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;
    this.defaultNormalised = options.defaultNormalised ?? 0;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.dragging = false;
    this.dragStartY = 0;
    this.dragStartValue = 0;

    this._unsubscribe = this.binding.onChange(() => this.render());

    canvas.addEventListener("pointerdown",  e => this._onDown(e));
    canvas.addEventListener("pointermove",  e => this._onMove(e));
    canvas.addEventListener("pointerup",    e => this._onUp(e));
    canvas.addEventListener("pointercancel",e => this._onUp(e));
    canvas.addEventListener("dblclick",     e => this._onDouble(e));

    this.render();
  }

  _value() { return clamp01(this.binding.getNormalised()); }

  _onDown(e) {
    this.dragging = true;
    this.dragStartY = e.clientY;
    this.dragStartValue = this._value();
    this.binding.beginGesture();
    this.canvas.setPointerCapture(e.pointerId);
    e.preventDefault();
  }

  _onMove(e) {
    if (!this.dragging) return;
    const dy = this.dragStartY - e.clientY;
    const scale = e.shiftKey ? FINE_FACTOR : 1;
    const next = this.dragStartValue + (dy / DRAG_RANGE_PX) * scale;
    this.binding.setNormalised(clamp01(next));
  }

  _onUp(e) {
    if (!this.dragging) return;
    this.dragging = false;
    this.binding.endGesture();
    if (this.canvas.hasPointerCapture(e.pointerId))
      this.canvas.releasePointerCapture(e.pointerId);
  }

  _onDouble(e) {
    e.preventDefault();
    this.binding.beginGesture();
    this.binding.setNormalised(clamp01(this.defaultNormalised));
    this.binding.endGesture();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const v = this._value();
    const w = this.w, h = this.h;
    const cx = w / 2, cy = h / 2;
    const outerR = Math.floor(Math.min(cx, cy)) - 1;
    const ringR  = outerR;
    const bodyR  = outerR - 3;

    ctx.clearRect(0, 0, w, h);

    // Dark blue ring (the bezel).
    this._fillDisc(cx, cy, ringR, pal["knob-ring"]);

    // Body.
    this._fillDisc(cx, cy, bodyR, pal["knob-body"]);

    // Hard 1px bevel highlight on the body's top-left quadrant + shadow on the
    // bottom-right — gives the chunky physical-knob feel without a gradient.
    this._discHighlight(cx, cy, bodyR);

    // Indicator: 1-pixel-wide line of "dot" colour rendered as discrete pixels
    // from r * 0.30 out to r * 0.85 along the value angle.
    const angleRad = ((START_ANGLE_DEG + v * SWEEP_DEG) * Math.PI) / 180;
    const dx = Math.cos(angleRad);
    const dy = Math.sin(angleRad);

    const inner = bodyR * 0.30;
    const outer = bodyR * 0.85;

    ctx.fillStyle = pal["knob-dot"];
    const steps = Math.ceil(outer - inner);
    for (let i = 0; i <= steps; ++i) {
      const t = inner + i;
      ctx.fillRect(snap(cx + dx * t), snap(cy + dy * t), 1, 1);
    }

    // Square indicator dot at the tip — readable like the genny-ui reference.
    ctx.fillRect(snap(cx + dx * outer) - 1, snap(cy + dy * outer) - 1, 2, 2);
  }

  // Filled disc using Bresenham-style integer scan — keeps the edge crisp.
  _fillDisc(cx, cy, r, color) {
    const ctx = this.ctx;
    ctx.fillStyle = color;
    const r2 = r * r;
    const x0 = Math.ceil(cx - r);
    const x1 = Math.floor(cx + r);
    for (let x = x0; x <= x1; ++x) {
      const dx = x - cx + 0.5;
      const dy = Math.sqrt(Math.max(0, r2 - dx * dx));
      const y0 = Math.ceil(cy - dy);
      const y1 = Math.floor(cy + dy);
      if (y1 >= y0) ctx.fillRect(x, y0, 1, y1 - y0 + 1);
    }
  }

  _discHighlight(cx, cy, r) {
    const ctx = this.ctx;
    const pal = palette();
    // A 1px arc on the top-left for the lit edge, drawn as fillRect dots
    // along the parametric circle. Confined to the upper-left 90deg wedge.
    const r2 = r * r;
    ctx.fillStyle = pal["bevel-light"];
    for (let a = 180; a <= 270; ++a) {
      const rad = a * Math.PI / 180;
      const x = snap(cx + Math.cos(rad) * (r - 1));
      const y = snap(cy + Math.sin(rad) * (r - 1));
      ctx.fillRect(x, y, 1, 1);
    }
    ctx.fillStyle = pal["bevel-dark"];
    for (let a = 0; a <= 90; ++a) {
      const rad = a * Math.PI / 180;
      const x = snap(cx + Math.cos(rad) * (r - 1));
      const y = snap(cy + Math.sin(rad) * (r - 1));
      ctx.fillRect(x, y, 1, 1);
    }
  }

  destroy() {
    this._unsubscribe?.();
  }
}
