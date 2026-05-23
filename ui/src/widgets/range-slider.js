/*
 * range-slider — two-thumb horizontal range slider for the rack RNG control
 * (Task 22, 08-ui-views.md view 1 revised).
 *
 * Backed by two integer apvts params (note_lo / note_hi). The widget pulls
 * both bindings and renders a thin LED-base groove with two chunky blue caps
 * — the same visual idiom as the existing Slider widget (PAN / DEL).
 *
 * The lo cap clamps to <= hi at drag-time; the hi cap clamps to >= lo. A
 * compact read-out below the groove shows "lo–hi" as MIDI note numbers
 * (e.g. "0–127"), drawn with the same dot-matrix renderer used by the rest
 * of the LED readouts.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const CAP_W = 6;
const CAP_H_PAD = 2;

export class RangeSlider {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.loBinding = options.loBinding;
    this.hiBinding = options.hiBinding;
    if (!this.loBinding || !this.hiBinding)
      throw new Error("RangeSlider needs both loBinding and hiBinding");

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    // The full MIDI note range; pulled from binding metadata when available so
    // an automation-driven non-default range still maps correctly.
    this.lo = 0;
    this.hi = 127;

    this._drag = null;          // 'lo' | 'hi' | null

    canvas.addEventListener("pointerdown", e => this._onDown(e));
    canvas.addEventListener("pointermove", e => this._onMove(e));
    canvas.addEventListener("pointerup",   e => this._onUp(e));

    this._unsubLo = this.loBinding.onChange(() => this.render());
    this._unsubHi = this.hiBinding.onChange(() => this.render());

    this.render();
  }

  _bounds() {
    const lp = this.loBinding.properties ?? {};
    const hp = this.hiBinding.properties ?? {};
    return {
      lo: lp.start ?? 0,
      hi: hp.end   ?? 127,
    };
  }

  _normalisedFromScaled(value, side /* 'lo'|'hi' */) {
    const b = this._bounds();
    const span = (b.hi - b.lo) || 1;
    const v = Math.max(b.lo, Math.min(b.hi, value));
    void side;
    return (v - b.lo) / span;
  }

  _capX(value) {
    return Math.round(this._normalisedFromScaled(value) * (this.w - CAP_W));
  }

  _xToValue(x) {
    const b = this._bounds();
    const norm = Math.max(0, Math.min(1, x / Math.max(1, this.w - CAP_W)));
    return Math.round(b.lo + norm * (b.hi - b.lo));
  }

  _curLo() { return Math.round(this.loBinding.getScaled?.() ?? 0); }
  _curHi() { return Math.round(this.hiBinding.getScaled?.() ?? 127); }

  _setBindingScaled(binding, value) {
    const props = binding.properties ?? {};
    const lo = props.start ?? 0, hi = props.end ?? 127;
    const span = (hi - lo) || 1;
    const norm = Math.max(0, Math.min(1, (value - lo) / span));
    binding.beginGesture?.();
    binding.setNormalised(norm);
    binding.endGesture?.();
  }

  _onDown(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const loX = this._capX(this._curLo());
    const hiX = this._capX(this._curHi());
    // Pick the closer cap; ties go to the one nearer the click side.
    const dLo = Math.abs(x - (loX + CAP_W / 2));
    const dHi = Math.abs(x - (hiX + CAP_W / 2));
    this._drag = dLo <= dHi ? "lo" : "hi";
    this.canvas.setPointerCapture(e.pointerId);
    this._applyDrag(x);
  }

  _onMove(e) {
    if (!this._drag) return;
    const rect = this.canvas.getBoundingClientRect();
    this._applyDrag(e.clientX - rect.left);
  }

  _onUp(e) {
    if (!this._drag) return;
    this._drag = null;
    if (this.canvas.hasPointerCapture(e.pointerId))
      this.canvas.releasePointerCapture(e.pointerId);
  }

  _applyDrag(x) {
    let value = this._xToValue(x - CAP_W / 2);
    if (this._drag === "lo") {
      value = Math.min(value, this._curHi());
      if (value !== this._curLo()) this._setBindingScaled(this.loBinding, value);
    } else {
      value = Math.max(value, this._curLo());
      if (value !== this._curHi()) this._setBindingScaled(this.hiBinding, value);
    }
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w, H = this.h;

    // Groove: thin dark inset, same as PAN/DEL slider grooves.
    ctx.fillStyle = pal["lcd-base-hi"];
    ctx.fillRect(0, 0, W, H);
    drawBevel(ctx, 0, 0, W, H, false);

    // Highlight the active band between the two caps.
    const loX = this._capX(this._curLo());
    const hiX = this._capX(this._curHi());
    if (hiX > loX) {
      ctx.fillStyle = pal["lcd-pixel"];
      ctx.fillRect(loX + CAP_W, snap(H * 0.5 - 1), Math.max(1, hiX - loX - CAP_W), 2);
    }

    // Draw the two caps (chunky blue, like the PAN slider).
    const capY = CAP_H_PAD;
    const capH = H - CAP_H_PAD * 2;
    const drawCap = (x) => {
      ctx.fillStyle = pal["knob-body"];
      ctx.fillRect(x, capY, CAP_W, capH);
      drawBevel(ctx, x, capY, CAP_W, capH, true);
    };
    drawCap(loX);
    drawCap(hiX);
  }

  destroy() {
    this._unsubLo?.();
    this._unsubHi?.();
  }
}
