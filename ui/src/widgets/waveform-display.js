/*
 * waveform-display — green-LCD inset showing the loaded DAC sample
 * (08-ui-views.md view 3 "Sample strip"). The strip's peak data is pushed in
 * via `setPeaks(peaksArray)`; each entry is a 0..1 peak magnitude for that
 * column. When `peaks` is null/empty the strip renders blank ("— no sample —"
 * is rendered by the surrounding view, not this widget).
 *
 * The shape is the standard recessed LCD inset (bevel-inset) filled with the
 * dim lcd-base, with bright phosphor pixels for the peak columns. Pixel-art
 * rules apply: 1px columns, no smoothing, no gradients.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

export class WaveformDisplay {
  constructor(canvas) {
    this.canvas = canvas;
    this.peaks = null;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  setPeaks(peaks) {
    if (!Array.isArray(peaks) || peaks.length === 0) {
      this.peaks = null;
    } else {
      this.peaks = peaks;
    }
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    if (this.peaks === null) return;

    const innerX = 2, innerY = 2;
    const innerW = this.w - 4, innerH = this.h - 4;
    const midY  = Math.floor(innerY + innerH / 2);

    // Centerline — faint baseline so even silent regions read as "loaded".
    ctx.fillStyle = pal["lcd-base-hi"];
    ctx.fillRect(innerX, midY, innerW, 1);

    // Map each pixel column to the peak bucket nearest to it. The C++ side
    // (DACPlayer::computePeaks) sends `innerW` buckets when it can; this
    // mapping is robust to either matching or mismatched counts.
    const N = this.peaks.length;
    if (N === 0) return;

    ctx.fillStyle = pal["lcd-pixel"];
    for (let x = 0; x < innerW; ++x) {
      const idx = Math.min(N - 1, Math.floor((x * N) / innerW));
      const peak = Math.max(0, Math.min(1, this.peaks[idx]));
      const half = Math.max(1, Math.round(peak * (innerH / 2 - 1)));
      ctx.fillRect(snap(innerX + x), midY - half, 1, half * 2);
    }
  }
}
