/*
 * gear-icon — small canvas-drawn gear glyph for the header top-right
 * (genny-ui.md / 08-ui-views.md view 1 "Header meter bay"). Settings modal
 * wiring lands in Task 13; this widget is just the icon.
 *
 * Drawn as a pixel-art silhouette to stay consistent with the rest of the
 * UI's no-image-assets discipline (05-ui-ux.md): six rectangular teeth
 * radiating from a square body with a hollow center hole. Filled in the
 * label off-white so it sits on the chassis without competing with the
 * gold wordmark or the red LEDs.
 */

import { setupPixelCanvas, palette, snap } from "./pixel.js";

export class GearIcon {
  constructor(canvas) {
    this.canvas = canvas;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const w = this.w, h = this.h;
    ctx.clearRect(0, 0, w, h);

    ctx.fillStyle = pal["label"];

    const cx = Math.floor(w / 2);
    const cy = Math.floor(h / 2);
    const bodyR = Math.floor(Math.min(w, h) * 0.32);  // outer square radius
    const holeR = Math.max(1, Math.floor(bodyR * 0.35));
    const toothLen = Math.max(2, Math.floor(bodyR * 0.6));
    const toothW = Math.max(2, Math.floor(bodyR * 0.55));

    // Body — solid square stamp; pixel-art idiom prefers blocky shapes over
    // approximated circles at this size (16-18px).
    ctx.fillRect(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);

    // 4 cardinal teeth (top, right, bottom, left). Two diagonal teeth would
    // need half-pixel rotation to read clearly; cardinal-only keeps every
    // edge axis-aligned per 05-ui-ux.md "1× pixel grid" rule.
    const halfW = Math.floor(toothW / 2);
    // top
    ctx.fillRect(cx - halfW, cy - bodyR - toothLen, toothW, toothLen);
    // bottom
    ctx.fillRect(cx - halfW, cy + bodyR, toothW, toothLen);
    // left
    ctx.fillRect(cx - bodyR - toothLen, cy - halfW, toothLen, toothW);
    // right
    ctx.fillRect(cx + bodyR, cy - halfW, toothLen, toothW);

    // Center hole — punched out as chassis-black so the gear reads as a ring.
    ctx.fillStyle = pal["chassis"];
    ctx.fillRect(cx - holeR, cy - holeR, holeR * 2, holeR * 2);
  }
}
