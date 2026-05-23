/*
 * vu-meter — small green "TRUE STEREO" VU meter in the header
 * (08-ui-views.md view 1; genny-ui.md "Top header bar").
 *
 * Renders two stacked LCD-style segment bars (L over R). Levels come from
 * the editor's ~30 Hz meterData push; the C++ side already runs a peak
 * envelope follower so values arrive as smoothed 0..1 floats — this widget
 * just maps to discrete segment cells, which gives the snappy chunked look
 * a real LED bargraph has rather than a smooth analog needle.
 */

import { setupPixelCanvas, palette, drawBevel } from "./pixel.js";

export class VuMeter {
  constructor(canvas) {
    this.canvas = canvas;
    this.left = 0;
    this.right = 0;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  setLevels(l, r) {
    const nl = Math.max(0, Math.min(1, l ?? 0));
    const nr = Math.max(0, Math.min(1, r ?? 0));
    // Skip the repaint if the visible segment count won't change. Both bars
    // quantise to the same segment count, so this is a cheap pre-check that
    // avoids ~30 redundant canvas fills/sec when the meter is at rest.
    if (nl === this.left && nr === this.right) return;
    this.left = nl;
    this.right = nr;
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    // Two horizontal bars. The inner area is (w-4) x (h-6), with a 1px gap
    // between the two channels.
    const barH = Math.floor((this.h - 6) / 2);
    const innerW = this.w - 6;

    const drawBar = (y, fraction) => {
      // Background segment cells (dim) + lit cells (bright).
      const segs = Math.max(8, Math.floor(innerW / 3));
      const litCount = Math.round(segs * fraction);
      const segW = Math.floor(innerW / segs);
      for (let i = 0; i < segs; ++i) {
        ctx.fillStyle = i < litCount ? pal["lcd-pixel-hi"] : pal["lcd-base-hi"];
        ctx.fillRect(3 + i * segW, y, segW - 1, barH);
      }
    };

    drawBar(3, this.left);
    drawBar(3 + barH + 1, this.right);
  }
}
