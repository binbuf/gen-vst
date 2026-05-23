/*
 * folder-icon — small canvas-drawn folder glyph for the PRESETS / IMPORT tab
 * header (08-ui-views.md view 4 *Patch browser*: "The folder icon in the
 * Presets/Import tab header opens the browser modal"). Renders as a pixel-art
 * silhouette like gear-icon — staying inside the project's no-image-assets
 * discipline (05-ui-ux.md).
 */

import { setupPixelCanvas, palette } from "./pixel.js";

export class FolderIcon {
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

    // A two-tone "manila folder" outline: a top tab over a body rectangle.
    // Sized to fill the canvas with 1px chassis-coloured padding so the icon
    // reads as a distinct button against the panel.
    const pad = 1;
    const tabH = Math.max(2, Math.floor(h * 0.22));
    const tabW = Math.max(4, Math.floor(w * 0.45));

    // Tab — top-left protrusion.
    ctx.fillRect(pad, pad, tabW, tabH);

    // Body — main rectangle below the tab.
    ctx.fillRect(pad, pad + tabH, w - pad * 2, h - pad * 2 - tabH);

    // A 1px chassis-coloured inset on the body to suggest the folder lip,
    // staying consistent with the other pixel-art widgets' hard-bevel rule.
    ctx.fillStyle = pal["chassis"];
    ctx.fillRect(pad + 1, pad + tabH + 1, w - pad * 2 - 2, 1);
  }
}
