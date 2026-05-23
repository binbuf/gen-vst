/*
 * toggle — square LED-button. A small bevelled cap with a red LED dot inset:
 * the dot is lit when the toggle is on, dim when off. Click flips it.
 * (05-ui-ux.md Component Inventory — toggle / LED button.)
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

export class Toggle {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;
    this.label = options.label ?? "";

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this._unsubscribe = this.binding.onChange(() => this.render());

    canvas.addEventListener("click", e => {
      e.preventDefault();
      this.binding.toggle();
    });

    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const w = this.w, h = this.h;
    const on = this.binding.getValue();

    ctx.clearRect(0, 0, w, h);

    // Body — raised, blue when on, panel-dark when off (matching genny-ui's
    // selected-vs-unselected button feel).
    ctx.fillStyle = on ? pal["knob-body"] : pal["panel"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, true);

    // LED dot — recessed square, lit when on. Sits roughly centred on the
    // button face.
    const dotSize = Math.min(w, h) >= 16 ? 4 : 3;
    const dotX = Math.floor((w - dotSize) / 2);
    const dotY = Math.floor((h - dotSize) / 2);
    ctx.fillStyle = on ? pal["led-on"] : pal["led-base"];
    ctx.fillRect(dotX, dotY, dotSize, dotSize);
  }

  destroy() {
    this._unsubscribe?.();
  }
}
