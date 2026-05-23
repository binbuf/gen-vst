/*
 * slider — horizontal groove + chunky blue rectangular cap, with an attached
 * led-readout to the right (05-ui-ux.md Component Inventory; genny-ui.md
 * bottom-row operator panel sliders).
 *
 * Click-drag horizontally (right = increase), Shift = fine, double-click =
 * reset. Same gesture protocol as the knob to keep the interaction model
 * consistent across widgets.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const DRAG_RANGE_PX_DEFAULT = 200;
const FINE_FACTOR = 0.2;

function clamp01(x) { return Math.min(1, Math.max(0, x)); }

export class Slider {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;
    this.defaultNormalised = options.defaultNormalised ?? 0;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.capW = options.capWidth ?? 8;
    this.dragRange = options.dragRangePx ?? Math.max(80, this.w - this.capW);

    this.dragging = false;
    this.dragStartX = 0;
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
    this.dragStartX = e.clientX;
    this.dragStartValue = this._value();
    this.binding.beginGesture();
    this.canvas.setPointerCapture(e.pointerId);
    e.preventDefault();
  }

  _onMove(e) {
    if (!this.dragging) return;
    const dx = e.clientX - this.dragStartX;
    const scale = e.shiftKey ? FINE_FACTOR : 1;
    const next = this.dragStartValue + (dx / this.dragRange) * scale;
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
    const w = this.w, h = this.h;
    const v = this._value();

    ctx.clearRect(0, 0, w, h);

    // Background — pure chassis black so the slider sits flush in a panel.
    ctx.fillStyle = pal["chassis"];
    ctx.fillRect(0, 0, w, h);

    // Groove: 4px tall recessed inset across the full width.
    const grooveH = 4;
    const grooveY = Math.floor((h - grooveH) / 2);
    ctx.fillStyle = pal["panel"];
    ctx.fillRect(0, grooveY, w, grooveH);
    drawBevel(ctx, 0, grooveY, w, grooveH, false);

    // Cap: chunky blue rectangle with the same bevel idiom as a raised panel.
    const capX = snap(v * (w - this.capW));
    const capY = 1;
    const capH = h - 2;
    ctx.fillStyle = pal["knob-body"];
    ctx.fillRect(capX, capY, this.capW, capH);
    drawBevel(ctx, capX, capY, this.capW, capH, true);
  }

  destroy() {
    this._unsubscribe?.();
  }
}
