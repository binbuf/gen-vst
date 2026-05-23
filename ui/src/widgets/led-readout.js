/*
 * led-readout — 5x7 red dot-matrix value display.
 *
 * Not a font. The full glyph table, geometry constants, and render algorithm
 * are in widgets/pixel.js, per 05-ui-ux.md "5x7 Dot-Matrix Readouts". This
 * widget is the wrapper that binds the readout to a parameter relay and
 * formats the numeric value it shows.
 *
 * Supported display values: digits 0-9, the letters "O" / "F" (so "OFF" works),
 * "-" for negative numbers, and blank padding. The caller picks how the bound
 * value formats into one of those — see `format` below.
 */

import {
  setupPixelCanvas, palette, drawLedReadout,
  GLYPH_W, GLYPH_H, GLYPH_GAP, readoutWidth, drawBevel,
} from "./pixel.js";

const PAD_X = 3;   // inset between bevel and the dot grid (logical px)
const PAD_Y = 3;

/**
 * Build a led-readout widget.
 * @param {HTMLCanvasElement} canvas
 * @param {Object} options
 *   - `widthChars`  number of glyph cells (default 4)
 *   - `binding`     optional Binding from binding.js — listens for changes
 *   - `format`      (scaledValue) => displayString; default = integer formatting
 *                   that maps the special value 0 (or properties.start) to "OFF"
 *                   only if `offWhenZero` is true.
 *   - `offWhenZero` when true, a scaled value of 0 displays as "OFF"
 *   - `signed`      when true, negative scaled values include a leading "-"
 *   - `text`        for unbound usage: an explicit initial string to display
 */
export class LedReadout {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.widthChars = options.widthChars ?? 4;
    this.binding = options.binding ?? null;
    this.offWhenZero = !!options.offWhenZero;
    this.signed = !!options.signed;
    this.format = options.format ?? null;
    this.text = options.text ?? "";

    this._setupSize();

    if (this.binding) {
      this._unsubscribe = this.binding.onChange(() => this.render());
      this._unsubscribeProps =
        this.binding.onProperties?.(() => this.render()) ?? null;
    }
    this.render();
  }

  _setupSize() {
    // The canvas's logical size in CSS pixels must be large enough to fit
    // the dot grid + 1px bevel + padding. Override style here so the gallery
    // can drop a canvas with no explicit width/height and still get a sized
    // readout.
    const innerW = readoutWidth(this.widthChars);
    const innerH = GLYPH_H;
    const totalW = innerW + PAD_X * 2 + 2;   // +2 for the 1px bevel ring
    const totalH = innerH + PAD_Y * 2 + 2;
    this.canvas.style.width  = totalW + "px";
    this.canvas.style.height = totalH + "px";
    const setup = setupPixelCanvas(this.canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;
  }

  setText(s) {
    this.text = s;
    this.binding = null;   // explicit-text usage overrides any binding
    this.render();
  }

  _currentText() {
    if (this.binding) {
      const scaled = this.binding.getScaled();
      if (this.format) return this.format(scaled);
      if (this.offWhenZero && Math.abs(scaled) < 1e-9) return "OFF";
      const rounded = Math.round(scaled);
      if (this.signed) {
        if (rounded < 0) return "-" + Math.abs(rounded).toString();
        return rounded.toString();
      }
      return Math.abs(rounded).toString();
    }
    return this.text;
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    ctx.fillStyle = pal["led-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    const text = this._currentText();
    drawLedReadout(
      ctx,
      1 + PAD_X,
      1 + PAD_Y,
      text,
      this.widthChars,
    );
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
