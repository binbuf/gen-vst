/*
 * true-stereo-toggle — header cell for the TRUE STEREO global toggle
 * (08-ui-views.md view 1 / genny-ui.md "Top header bar"). Sits between the
 * wordmark and the meter bay.
 *
 * Visual idiom (Task 25): stacked "TRUE / STEREO" label in the dark-chassis
 * style with a small status dot + a compact toggle glyph on the right. When
 * the toggle is on the glyph is an "X" (stereo-cross) and the dot is lit;
 * when off the glyph collapses to a horizontal bar (mono) and the dot
 * darkens. Click anywhere on the cell flips the apvts param via the toggle
 * binding.
 */

import { setupPixelCanvas, palette, drawBevel, drawLabel, snap } from "./pixel.js";

const CELL_W = 76;
const CELL_H = 36;

// Glyph cell occupies the right slot of the cell; the dot sits just to its left.
const GLYPH_W = 10;
const GLYPH_H = 10;
const DOT_W = 4;

export class TrueStereoToggle {
  constructor(canvas, binding) {
    this.canvas = canvas;
    this.binding = binding;

    canvas.width = CELL_W;
    canvas.height = CELL_H;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this._unsubscribe = binding.onChange(() => this.render());
    canvas.style.cursor = "pointer";
    canvas.addEventListener("click", (e) => {
      e.preventDefault();
      binding.toggle();
    });

    this.render();
    if (document.fonts?.ready) document.fonts.ready.then(() => this.render());
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const w = this.w, h = this.h;
    const on = this.binding.getValue();

    ctx.clearRect(0, 0, w, h);

    // Body — sits on the dark chassis like the wordmark next to it (no green
    // LCD inset; the cell reads as a chassis-mounted switch rather than a
    // meter-bay readout).
    ctx.fillStyle = pal["panel"];
    ctx.fillRect(0, 0, w, h);
    drawBevel(ctx, 0, 0, w, h, true);

    // Stacked "TRUE / STEREO" label, 8 px Press Start 2P. STEREO is the wider
    // word so its width drives the label column; TRUE sits center-aligned
    // above it.
    const fontPx = 8;
    const labelLeft = 4;
    const stereoW = "STEREO".length * fontPx;
    const trueW   = "TRUE".length   * fontPx;
    const lineGap = 2;
    const labelBlockH = fontPx * 2 + lineGap;
    const labelTop = Math.floor((h - labelBlockH) / 2);
    drawLabel(ctx, labelLeft + Math.floor((stereoW - trueW) / 2), labelTop,
              "TRUE", fontPx, pal["label"]);
    drawLabel(ctx, labelLeft, labelTop + fontPx + lineGap,
              "STEREO", fontPx, pal["label"]);

    // Right-side cluster: status dot + X / dash glyph. Aligned to the right
    // edge with a small inset so the bevel stays clean.
    const rightInset = 4;
    const glyphX = w - rightInset - GLYPH_W;
    const glyphY = Math.floor((h - GLYPH_H) / 2);
    const dotX = glyphX - 2 - DOT_W;
    const dotY = Math.floor((h - DOT_W) / 2);

    // Status dot — lit red when on, dim when off. Recessed bevel so it reads
    // as a tiny inset LED, matching the operator-panel status dot idiom.
    ctx.fillStyle = on ? pal["led-on"] : pal["led-base"];
    ctx.fillRect(dotX, dotY, DOT_W, DOT_W);

    // Glyph: pixel-art X (stereo cross) when on, single horizontal bar (mono
    // sum) when off. Drawn lit / dim in the LED palette so the visual cue
    // and the status dot read together.
    const glyphColor = on ? pal["led-on"] : pal["led-dim"];
    ctx.fillStyle = glyphColor;

    if (on) {
      // Diagonal strokes — 2 px thick, drawn cell-by-cell to stay pixel-snapped.
      for (let i = 0; i < GLYPH_W; ++i) {
        // top-left → bottom-right
        ctx.fillRect(snap(glyphX + i),     snap(glyphY + i),     1, 1);
        ctx.fillRect(snap(glyphX + i + 1), snap(glyphY + i),     1, 1);
        // top-right → bottom-left
        const j = GLYPH_W - 1 - i;
        ctx.fillRect(snap(glyphX + j),     snap(glyphY + i),     1, 1);
        ctx.fillRect(snap(glyphX + j - 1), snap(glyphY + i),     1, 1);
      }
    } else {
      // Single horizontal bar in the middle: signals the L+R sum / mono mode.
      const barY = glyphY + Math.floor(GLYPH_H / 2) - 1;
      ctx.fillRect(glyphX, barY, GLYPH_W, 2);
    }
  }

  destroy() {
    this._unsubscribe?.();
  }
}
