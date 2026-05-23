/*
 * seg-display — wide red 7-segment patch-name display in the header
 * (genny-ui.md "Top header bar"). The display shows the currently selected
 * part's patch name; bracket glyphs flank the text.
 *
 * Rendered as text in the bundled Segment7 font over a dark-red base inside a
 * recessed bevel. The text is set via setText() — the FM view orchestrator
 * pushes the active patch name in whenever a load completes or the selected
 * channel changes.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const FONT_PX_DEFAULT = 28;
const PAD_X = 8;
const PAD_Y = 6;

export class SegDisplay {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.fontPx = options.fontPx ?? FONT_PX_DEFAULT;
    this.text = options.text ?? "";

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

    // Faint "unlit-segment" ghost — a dimmer fill to suggest the inactive
    // segment grid. A real chassis would show every dimly-lit segment behind
    // the lit characters; the closest pixel-art analogue is to leave the
    // base colour visible and avoid drawing a ghost overlay (the Segment7
    // font already shows only lit segments). The lit text below uses --led-on.
    ctx.save();
    ctx.imageSmoothingEnabled = false;
    ctx.font = `${this.fontPx}px "Segment7", monospace`;
    ctx.textBaseline = "middle";
    ctx.textAlign = "center";
    ctx.fillStyle = pal["led-on"];

    // Bracket glyphs flank the patch name as in genny-ui.md. The text is
    // centered between them; if the name overflows we clip it via the canvas
    // bounds rather than scaling the font (no fractional sizes).
    const t = (this.text || "—").toUpperCase();
    const display = `[ ${t} ]`;
    ctx.fillText(display, this.w / 2, this.h / 2 + 1);
    ctx.restore();
  }
}
