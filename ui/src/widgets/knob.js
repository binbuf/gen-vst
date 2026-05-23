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
    const bodyR  = outerR - 4;     // leave 4px for a chunkier blue bezel ring

    ctx.clearRect(0, 0, w, h);

    // Dark blue bezel ring.
    this._fillDisc(cx, cy, ringR, pal["knob-ring"]);

    // Inner body.
    this._fillDisc(cx, cy, bodyR, pal["knob-body"]);

    // Hard-edge raised bevel — 2px lit ring on the top-left quadrant + 2px
    // shadow on the bottom-right. Pure pixel art (no gradient blur). Gives
    // the chunky physical-knob feel and reads well at small sizes.
    this._discHighlight(cx, cy, bodyR);

    // Small 2x2 specular highlight near the top-left, just inside the bevel.
    // A real physical surface catches the room light at one spot, and this
    // tiny white square is the pixel-art shorthand for that highlight.
    const specR = bodyR - 2;
    const specAngle = (220 * Math.PI) / 180;  // upper-left of the knob
    const sx = snap(cx + Math.cos(specAngle) * specR * 0.62);
    const sy = snap(cy + Math.sin(specAngle) * specR * 0.62);
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(sx, sy, 2, 2);

    // Indicator: a chunky line of dot-colour rendered as a 2px-thick stripe
    // from r * 0.30 out to r * 0.88 along the value angle, with a 3x3 square
    // tip so the indicator is unambiguous from across the layout.
    const angleRad = ((START_ANGLE_DEG + v * SWEEP_DEG) * Math.PI) / 180;
    const dx = Math.cos(angleRad);
    const dy = Math.sin(angleRad);

    const inner = bodyR * 0.30;
    const outer = bodyR * 0.88;

    ctx.fillStyle = pal["knob-dot"];
    const steps = Math.ceil(outer - inner);
    // Two-pixel-wide stripe: draw the dot pair offset perpendicular to the
    // travel direction so a near-vertical indicator is still 2 px wide.
    const px = -dy, py = dx;   // perpendicular unit
    for (let i = 0; i <= steps; ++i) {
      const t = inner + i;
      const ax = snap(cx + dx * t);
      const ay = snap(cy + dy * t);
      ctx.fillRect(ax, ay, 1, 1);
      ctx.fillRect(snap(ax + px), snap(ay + py), 1, 1);
    }

    // 3x3 indicator tip — really visible at the knob edge.
    ctx.fillRect(snap(cx + dx * outer) - 1, snap(cy + dy * outer) - 1, 3, 3);
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
    // A 2px-thick arc on the top-left for the lit edge and the bottom-right
    // for the shadowed edge. Drawn as fillRect dots along two parametric
    // circles (radius r-1 and r-2) so the highlight is exactly 2 px wide
    // even when the underlying disc is integer-stepped. The lit colour is
    // a lighter-blue tint so it doesn't blend back into the chassis bevel.
    const lit = "#7aa0e0";       // lighter knob-body — only used as a bevel hint
    const dim = pal["knob-ring"]; // already-dark blue, reads as shadow
    for (let ring = 0; ring < 2; ++ring) {
      const rr = r - ring;
      ctx.fillStyle = lit;
      for (let a = 180; a <= 280; ++a) {
        const rad = a * Math.PI / 180;
        const x = snap(cx + Math.cos(rad) * rr);
        const y = snap(cy + Math.sin(rad) * rr);
        ctx.fillRect(x, y, 1, 1);
      }
      ctx.fillStyle = dim;
      for (let a = -10; a <= 90; ++a) {
        const rad = a * Math.PI / 180;
        const x = snap(cx + Math.cos(rad) * rr);
        const y = snap(cy + Math.sin(rad) * rr);
        ctx.fillRect(x, y, 1, 1);
      }
    }
  }

  destroy() {
    this._unsubscribe?.();
  }
}
