/*
 * wordmark — canvas-drawn "GEN VST" beveled-gold logotype with a red
 * underline (genny-ui.md "Color Palette" + "Typography"; 05-ui-ux.md
 * "Fonts" — the wordmark is canvas-drawn, not a bitmap image).
 *
 * The label font (Press Start 2P) is rendered at 16px with a hard 1px
 * gold-shadow offset to fake the beveled look without any blur. A solid red
 * underline runs beneath the text, matching the "GEN VST" wordmark across
 * the Genny / Gen VST visual identity.
 */

import { setupPixelCanvas, palette, snap } from "./pixel.js";

const FONT_PX = 16;
const TEXT = "GEN VST";

export class Wordmark {
  constructor(canvas) {
    this.canvas = canvas;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    // Wordmark is the first widget mounted (fm-view.js mountHeader), which
    // historically meant it could render before Press Start 2P had loaded
    // and before getComputedStyle had a chance to resolve the palette CSS
    // vars. document.fonts.ready resolves after both font loading and the
    // initial style recalc, so this single deferred render guarantees a
    // correct first paint.
    this.render();
    if (document.fonts?.ready) document.fonts.ready.then(() => this.render());
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.clearRect(0, 0, this.w, this.h);

    ctx.save();
    ctx.imageSmoothingEnabled = false;
    ctx.font = `${FONT_PX}px "Press Start 2P", monospace`;
    ctx.textBaseline = "top";

    // Hard 1px shadow under-right, then the gold face. No blur, no gradient —
    // a single offset is enough to read as beveled in pixel-art idiom.
    const x = 2;
    const y = Math.max(2, Math.floor((this.h - FONT_PX - 4) / 2));

    ctx.fillStyle = pal["logo-shadow"];
    ctx.fillText(TEXT, snap(x + 1), snap(y + 1));
    ctx.fillStyle = pal["logo"];
    ctx.fillText(TEXT, snap(x), snap(y));

    // Red underline, 2px thick, spanning the visible text width. Press Start
    // 2P is monospaced 1-em-cell, so 7 chars * FONT_PX is exact.
    const underlineY = y + FONT_PX + 2;
    ctx.fillStyle = pal["logo-underline"];
    ctx.fillRect(x, underlineY, TEXT.length * FONT_PX, 2);
    ctx.restore();
  }
}
