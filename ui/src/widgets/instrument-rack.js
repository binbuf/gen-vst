/*
 * instrument-rack — Task 22 / 08-ui-views.md view 1 (revised center column).
 *
 * Renders a green-LCD list of rack rows, one per active rack slot, plus a
 * trailing "+" cell. Each row shows:
 *
 *   ◇  type-icon  patch-name              :::  −
 *
 * - type-icon: sine glyph (FM), square (SQ), tiny drum-kit pixel (D)
 * - patch-name: from getRackState() — for FM that's the patch file stem, for
 *   SQ a literal "PSG n / Noise" label, for D the loaded WAV name (or
 *   "— no sample —" if none)
 * - "+" / "-": add a row (opens the type popover) / remove the selected row
 * - drag-handle (:::): render-only this pass (post-MVP reorder per task)
 *
 * Selection: clicking a row calls `onSelect({ type, slotIndex, paramSuffix })`
 * — the parent FM view rebinds the per-instrument routing strip + paging path.
 *
 * The widget is fully canvas-driven for visual parity with the existing
 * Instruments LCD list it replaces.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const ROW_H        = 14;
const ROW_PAD_TOP  = 4;
const ICON_X       = 6;
const ICON_W       = 10;
const NAME_X       = 22;
const FONT_PX      = 8;
const REMOVE_W     = 14;        // width of the "-" cell at the right edge
const SCROLLBAR_W  = 6;
const ADD_ROW_LABEL = "+ ADD INSTRUMENT";

export class InstrumentRack {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.rows   = options.rows ?? [];      // [{ type, slotIndex, patchName, ... }]
    this.selected = options.selected ?? 0; // row index of the highlighted row
    this.onSelect = options.onSelect ?? null;
    this.onAdd    = options.onAdd    ?? null;   // (clientX, clientY) -> opens popover
    this.onRemove = options.onRemove ?? null;   // (row) -> remove from rack

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.scrollY = 0;

    canvas.addEventListener("click", e => this._onClick(e));
    canvas.addEventListener("wheel", e => this._onWheel(e), { passive: false });
    canvas.addEventListener("pointerdown", e => this._onScrollDown(e));
    canvas.addEventListener("pointermove", e => this._onScrollMove(e));
    canvas.addEventListener("pointerup",   e => this._onScrollUp(e));

    this.render();
  }

  setRows(rows, selectedIndex = 0) {
    this.rows = rows;
    if (selectedIndex < -1) selectedIndex = -1;
    if (selectedIndex >= rows.length) selectedIndex = Math.max(-1, rows.length - 1);
    this.selected = selectedIndex;
    this.scrollY = 0;
    this.render();
  }

  setSelected(index) {
    this.selected = typeof index === "number" ? index : -1;
    this.render();
  }

  // Find the row index from a Y pixel coordinate, accounting for scroll. The
  // "+" cell sits one row past the last data row. Returns -1 if outside.
  _hitRowIndex(y) {
    const row = Math.floor((y - ROW_PAD_TOP + this.scrollY) / ROW_H);
    return row;
  }

  _onClick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    if (x >= this.w - SCROLLBAR_W) return;

    const idx = this._hitRowIndex(y);
    const isAddRow = idx === this.rows.length;

    if (isAddRow) {
      this.onAdd?.(e.clientX, e.clientY);
      return;
    }

    if (idx < 0 || idx >= this.rows.length) return;

    const row = this.rows[idx];
    const removeX = this.w - SCROLLBAR_W - REMOVE_W;
    if (x >= removeX) {
      this.onRemove?.(row);
      return;
    }

    this.selected = idx;
    this.onSelect?.(row);
    this.render();
  }

  _onWheel(e) {
    e.preventDefault();
    const max = this._maxScroll();
    this.scrollY = Math.max(0, Math.min(max, this.scrollY + e.deltaY));
    this.render();
  }

  _onScrollDown(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    if (x < this.w - SCROLLBAR_W) return;
    this._scrollDrag = { startY: e.clientY, startScroll: this.scrollY };
    this.canvas.setPointerCapture(e.pointerId);
  }

  _onScrollMove(e) {
    if (!this._scrollDrag) return;
    const trackH = this.h - ROW_PAD_TOP * 2;
    const max = this._maxScroll();
    if (max <= 0) return;
    const dy = e.clientY - this._scrollDrag.startY;
    const scrollPerPx = max / Math.max(1, trackH - 16);
    this.scrollY = Math.max(0, Math.min(max,
      this._scrollDrag.startScroll + dy * scrollPerPx));
    this.render();
  }

  _onScrollUp(e) {
    if (!this._scrollDrag) return;
    this._scrollDrag = null;
    if (this.canvas.hasPointerCapture(e.pointerId))
      this.canvas.releasePointerCapture(e.pointerId);
  }

  _maxScroll() {
    const totalH = (this.rows.length + 1) * ROW_H;
    const visible = this.h - ROW_PAD_TOP * 2;
    return Math.max(0, totalH - visible);
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w, H = this.h;

    // LCD-green base + inset bevel.
    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, W, H);
    drawBevel(ctx, 0, 0, W, H, false);

    ctx.save();
    ctx.beginPath();
    ctx.rect(1, ROW_PAD_TOP, W - SCROLLBAR_W - 1, H - ROW_PAD_TOP * 2);
    ctx.clip();

    ctx.imageSmoothingEnabled = false;
    ctx.font = `${FONT_PX}px "Press Start 2P", monospace`;
    ctx.textBaseline = "top";

    for (let i = 0; i < this.rows.length; ++i)
      this._renderDataRow(i);

    // "+ Add instrument" trailing row — drawn as a faint cue. The full add
    // popover is opened on click.
    this._renderAddRow(this.rows.length);

    ctx.restore();

    this._drawScrollbar();
  }

  _renderDataRow(i) {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w;
    const y = ROW_PAD_TOP + i * ROW_H - this.scrollY;
    const row = this.rows[i];
    const isSel = i === this.selected;

    if (isSel) {
      ctx.fillStyle = pal["lcd-pixel"];
      ctx.fillRect(1, y, W - SCROLLBAR_W - 2, ROW_H);
      ctx.fillStyle = pal["lcd-base"];
    } else {
      ctx.fillStyle = pal["lcd-pixel"];
    }

    // Type icon.
    this._drawTypeIcon(ICON_X, y + 2, row.type, isSel);

    // Patch name — uppercase, trimmed to fit before the drag handle.
    const removeX = W - SCROLLBAR_W - REMOVE_W;
    const dragX   = removeX - 14;
    const nameMaxPx = dragX - NAME_X - 2;
    const name = (row.patchName ?? "").toString().toUpperCase();
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    this._drawClippedText(name, NAME_X, snap(y + (ROW_H - FONT_PX) / 2), nameMaxPx);

    // Drag-handle glyph (render-only — reorder is post-MVP).
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-text-dark"] || pal["lcd-pixel"];
    this._drawDragHandle(dragX, y + 3);

    // "-" remove button.
    this._drawRemoveCell(removeX, y, isSel);
  }

  _renderAddRow(i) {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w;
    const y = ROW_PAD_TOP + i * ROW_H - this.scrollY;
    // Background: same green base; a thin dashed divider above hints "new".
    ctx.fillStyle = pal["lcd-pixel"];
    for (let x = 1; x < W - SCROLLBAR_W - 2; x += 2)
      ctx.fillRect(x, y, 1, 1);

    ctx.fillStyle = pal["lcd-pixel"];
    ctx.fillText(ADD_ROW_LABEL,
                 ICON_X, snap(y + (ROW_H - FONT_PX) / 2 + 1));
  }

  _drawClippedText(text, x, y, maxPx) {
    const ctx = this.ctx;
    // Simple greedy fit — drop chars from the end until it measures within.
    let s = text;
    let width = ctx.measureText(s).width;
    if (width <= maxPx) {
      ctx.fillText(s, x, y);
      return;
    }
    while (s.length > 1 && ctx.measureText(s + "…").width > maxPx) s = s.slice(0, -1);
    ctx.fillText(s + "…", x, y);
  }

  _drawTypeIcon(x, y, type, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    const fg = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    ctx.fillStyle = fg;
    const W = ICON_W;
    if (type === "fm") {
      // Sine wave glyph — 10x9 pixels, two-bump curve.
      const xs = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
      const ys = [4, 3, 2, 2, 3, 4, 5, 6, 6, 5];
      for (let i = 0; i < xs.length; ++i)
        ctx.fillRect(x + xs[i], y + ys[i], 1, 1);
    } else if (type === "sq") {
      // Square wave glyph.
      const pts = [
        [0, 5], [1, 5], [2, 5],
        [2, 4], [2, 3], [2, 2],
        [3, 2], [4, 2], [5, 2],
        [5, 3], [5, 4], [5, 5], [5, 6], [5, 7],
        [6, 7], [7, 7], [8, 7],
        [8, 6], [8, 5],
        [9, 5],
      ];
      for (const [px, py] of pts) ctx.fillRect(x + px, y + py, 1, 1);
    } else if (type === "d") {
      // Drum-kit pixel: small box + a "stick" descending.
      ctx.fillRect(x + 1, y + 2, 8, 1);
      ctx.fillRect(x + 1, y + 6, 8, 1);
      ctx.fillRect(x + 1, y + 2, 1, 5);
      ctx.fillRect(x + 8, y + 2, 1, 5);
      // Stand.
      ctx.fillRect(x + 4, y + 7, 2, 2);
    } else {
      ctx.fillRect(x, y + 4, W, 1);
    }
  }

  _drawDragHandle(x, y) {
    const ctx = this.ctx;
    // ::: glyph — three vertical pairs of dots.
    for (let r = 0; r < 3; ++r)
      for (let c = 0; c < 2; ++c)
        ctx.fillRect(x + c * 3, y + r * 3, 1, 1);
  }

  _drawRemoveCell(x, y, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    // A thin "-" glyph centered in the cell. Color follows selection.
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    ctx.fillRect(x + 3, y + Math.floor(ROW_H / 2), REMOVE_W - 6, 1);
  }

  _drawScrollbar() {
    const ctx = this.ctx;
    const pal = palette();
    const trackX = this.w - SCROLLBAR_W;
    const trackY = ROW_PAD_TOP;
    const trackH = this.h - ROW_PAD_TOP * 2;
    ctx.fillStyle = pal["lcd-base-hi"];
    ctx.fillRect(trackX, trackY, SCROLLBAR_W, trackH);

    const totalH = (this.rows.length + 1) * ROW_H;
    if (totalH <= trackH) return;
    const thumbH = Math.max(8, Math.floor(trackH * trackH / totalH));
    const range = trackH - thumbH;
    const max = this._maxScroll();
    const thumbY = trackY + Math.floor(range * (this.scrollY / Math.max(1, max)));
    ctx.fillStyle = pal["lcd-pixel"];
    ctx.fillRect(trackX, thumbY, SCROLLBAR_W, thumbH);
  }
}

export { ROW_H as INSTRUMENT_RACK_ROW_H };
