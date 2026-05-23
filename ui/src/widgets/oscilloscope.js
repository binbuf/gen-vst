/*
 * oscilloscope — green-LCD waveform display in the header (08-ui-views.md view 1).
 *
 * Plots the post-master-gain post-soft-clip mono mix as a 1-px-thick green
 * trace on the LCD-base background. Samples are fed in from the editor's
 * ~30 Hz meterData push (05-ui-ux.md "C++ → JS telemetry push"); the C++
 * side already downsamples to ~768 points so this widget never has to
 * decimate further — it just maps points to columns and dots.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

export class Oscilloscope {
  constructor(canvas) {
    this.canvas = canvas;
    this.samples = null;   // Float32Array-like, set by setSamples()

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  setSamples(arr) {
    this.samples = arr;
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    const midY = Math.floor(this.h / 2);
    const innerW = this.w - 4;
    const halfH = this.h / 2 - 2;

    if (this.samples && this.samples.length > 0) {
      ctx.fillStyle = pal["lcd-pixel"];

      // Map each output column to the nearest source sample. The samples are
      // already downsampled C++ side; pick rather than re-average to keep
      // sharp transients visible.
      const step = this.samples.length / innerW;
      let prevY = midY - Math.round(halfH * clamp(this.samples[0]));
      for (let x = 0; x < innerW; ++x) {
        const s = this.samples[Math.floor(x * step)] ?? 0;
        const y = midY - Math.round(halfH * clamp(s));

        // Connect adjacent samples with a vertical column of pixels so a
        // high-amplitude step still reads as a continuous waveform rather
        // than a row of disconnected dots.
        const y0 = Math.min(prevY, y);
        const y1 = Math.max(prevY, y);
        ctx.fillRect(2 + x, snap(y0), 1, y1 - y0 + 1);
        prevY = y;
      }
    } else {
      // Cold-start placeholder: a faint centre line so the inset isn't visually
      // empty before the first telemetry tick lands.
      ctx.fillStyle = pal["lcd-pixel"];
      ctx.fillRect(2, midY, innerW, 1);
    }
  }
}

function clamp(v) {
  if (v > 1) return 1;
  if (v < -1) return -1;
  return v;
}
