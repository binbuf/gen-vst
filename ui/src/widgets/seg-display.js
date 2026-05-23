/*
 * seg-display — wide red LED-style patch-name display in the header
 * (genny-ui.md "Top header bar"). The display shows the currently selected
 * part's patch name; bracket glyphs flank the text.
 *
 * Rendered as a scaled 5x7 dot-matrix grid (same system as led-readout —
 * 05-ui-ux.md "5x7 Dot-Matrix Readouts" — at larger dot pitch so the letters
 * fill the header readout). The dot-matrix renderer is letter-unambiguous
 * (T is clearly T, B is clearly B), unlike a 7-segment font where the
 * alphabet collapses onto digit shapes.
 *
 * Text is set via setText() — the FM view orchestrator pushes the active
 * patch name in whenever a load completes or the selected channel changes.
 */

import {
  setupPixelCanvas, palette, drawBevel,
  drawDotMatrixText, dotMatrixTextWidth, dotMatrixTextHeight,
} from "./pixel.js";

const DOT_SIZE  = 2;
const DOT_PITCH = 4;

export class SegDisplay {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.text = options.text ?? "";
    this.dotSize  = options.dotSize  ?? DOT_SIZE;
    this.dotPitch = options.dotPitch ?? DOT_PITCH;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  setText(t) {
    this.text = String(t ?? "");
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    // Dark red-black base + recessed inset.
    ctx.fillStyle = pal["led-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    // Glyph geometry. Pick dot pitch by trimming if the bracketed name would
    // overflow the inner width; fall back to smaller dots before clipping.
    let opts = { dotSize: this.dotSize, dotPitch: this.dotPitch };
    const innerW = this.w - 8;
    const innerH = this.h - 6;
    const display = `[ ${(this.text || "—").toUpperCase()} ]`;

    let needed = dotMatrixTextWidth(display.length, opts);
    if (needed > innerW || dotMatrixTextHeight(opts) > innerH) {
      // Shrink dot pitch step-by-step until the text fits or we hit the
      // minimum 1px dot grid (which still beats a 7-segment font for letters).
      while (opts.dotPitch > 2 &&
             (dotMatrixTextWidth(display.length, opts) > innerW
              || dotMatrixTextHeight(opts) > innerH)) {
        opts = { dotSize: Math.max(1, opts.dotSize - 1),
                 dotPitch: opts.dotPitch - 1 };
      }
      needed = dotMatrixTextWidth(display.length, opts);
    }

    // Horizontally center the text; vertically center within the bevel.
    const textH = dotMatrixTextHeight(opts);
    const startX = Math.floor((this.w - needed) / 2);
    const startY = Math.floor((this.h - textH) / 2);

    drawDotMatrixText(ctx, startX, startY, display, opts);
  }
}
