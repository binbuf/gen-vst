/*
 * lcd-list — green-LCD scrollable list with a pixel scrollbar and inverse-video
 * selection (genny-ui.md "Center Column" / "Right Column"). Used by both the
 * Instruments and Presets lists, and later by the patch browser.
 *
 * The list is fed an array of `{ id, label }` items via setItems(); selection
 * binds optionally to a combo-box relay so the choice round-trips through the
 * apvts. (The real patch browser will fill items + selection via native
 * functions in Task 14; in the gallery the list demonstrates the scrolling
 * and selection behaviour against a scratch combo parameter.)
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const ROW_H = 12;
const FONT_PX = 8;
const PAD_X = 6;
const PAD_TOP = 4;
const SCROLLBAR_W = 6;

export class LcdList {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.items = options.items ?? [];
    this.binding = options.binding ?? null;
    this.onSelect = options.onSelect ?? null;

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.scrollY = 0;
    this.selected = options.selected ?? 0;

    canvas.addEventListener("click", e => this._onClick(e));
    canvas.addEventListener("wheel", e => this._onWheel(e), { passive: false });
    canvas.addEventListener("pointerdown", e => this._onScrollDown(e));
    canvas.addEventListener("pointermove", e => this._onScrollMove(e));
    canvas.addEventListener("pointerup",   e => this._onScrollUp(e));

    if (this.binding) {
      this._unsubscribe = this.binding.onChange(() => {
        this.selected = this.binding.getIndex();
        this._ensureSelectedVisible();
        this.render();
      });
      this._unsubscribeProps =
        this.binding.onProperties?.(() => {
          this.items = (this.binding.getChoices() ?? []).map(
            (label, id) => ({ id, label }));
          this.selected = this.binding.getIndex();
          this.render();
        }) ?? null;
      const choices = this.binding.getChoices() ?? [];
      if (this.items.length === 0 && choices.length > 0)
        this.items = choices.map((label, id) => ({ id, label }));
      this.selected = this.binding.getIndex();
    }

    this.render();
  }

  setItems(items, selectedIndex = 0) {
    this.items = items;
    this.selected = selectedIndex;
    this.scrollY = 0;
    this.render();
  }

  _visibleRows() {
    return Math.max(1, Math.floor((this.h - PAD_TOP * 2) / ROW_H));
  }

  _contentH() { return this.items.length * ROW_H; }

  _maxScroll() {
    return Math.max(0, this._contentH() - (this.h - PAD_TOP * 2));
  }

  _ensureSelectedVisible() {
    const rows = this._visibleRows();
    const top = Math.floor(this.scrollY / ROW_H);
    if (this.selected < top) {
      this.scrollY = this.selected * ROW_H;
    } else if (this.selected >= top + rows) {
      this.scrollY = (this.selected - rows + 1) * ROW_H;
    }
    this.scrollY = Math.max(0, Math.min(this._maxScroll(), this.scrollY));
  }

  _onClick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    if (x >= this.w - SCROLLBAR_W) return;   // scrollbar handles its own clicks
    const row = Math.floor((y - PAD_TOP + this.scrollY) / ROW_H);
    if (row >= 0 && row < this.items.length) {
      this.selected = row;
      if (this.binding) this.binding.setIndex(row);
      if (this.onSelect) this.onSelect(this.items[row], row);
      this.render();
    }
  }

  _onWheel(e) {
    e.preventDefault();
    this.scrollY = Math.max(0, Math.min(this._maxScroll(), this.scrollY + e.deltaY));
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
    const dy = e.clientY - this._scrollDrag.startY;
    const trackH = this.h - PAD_TOP * 2;
    if (this._contentH() <= trackH) return;
    const scrollPerPx = this._maxScroll() / Math.max(1, trackH - 16);
    this.scrollY = Math.max(0, Math.min(this._maxScroll(),
      this._scrollDrag.startScroll + dy * scrollPerPx));
    this.render();
  }

  _onScrollUp(e) {
    if (!this._scrollDrag) return;
    this._scrollDrag = null;
    if (this.canvas.hasPointerCapture(e.pointerId))
      this.canvas.releasePointerCapture(e.pointerId);
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    // LCD-green base + the standard inset bevel (recessed screen).
    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    // Clip the visible row area so partial rows at the bottom don't bleed.
    ctx.save();
    ctx.beginPath();
    ctx.rect(1, PAD_TOP, this.w - SCROLLBAR_W - 1, this.h - PAD_TOP * 2);
    ctx.clip();

    ctx.imageSmoothingEnabled = false;
    ctx.font = `${FONT_PX}px "Press Start 2P", monospace`;
    ctx.textBaseline = "top";

    const top = Math.floor(this.scrollY / ROW_H);
    const rowsVisible = this._visibleRows() + 1;
    for (let i = top; i < Math.min(this.items.length, top + rowsVisible); ++i) {
      const item = this.items[i];
      const y = PAD_TOP + i * ROW_H - this.scrollY;
      const isSel = i === this.selected;

      if (isSel) {
        // Inverse-video selection — phosphor-green fill, dark-LCD text.
        ctx.fillStyle = pal["lcd-pixel"];
        ctx.fillRect(1, y, this.w - SCROLLBAR_W - 2, ROW_H);
        ctx.fillStyle = pal["lcd-base"];
      } else {
        ctx.fillStyle = pal["lcd-pixel"];
      }
      ctx.fillText(String(item.label).toUpperCase(),
                   PAD_X, snap(y + (ROW_H - FONT_PX) / 2));
    }

    ctx.restore();

    // Pixel scrollbar on the right edge.
    this._drawScrollbar();
  }

  _drawScrollbar() {
    const ctx = this.ctx;
    const pal = palette();
    const trackX = this.w - SCROLLBAR_W;
    const trackY = PAD_TOP;
    const trackH = this.h - PAD_TOP * 2;

    ctx.fillStyle = pal["lcd-base-hi"];
    ctx.fillRect(trackX, trackY, SCROLLBAR_W, trackH);

    const contentH = this._contentH();
    if (contentH <= trackH) return;

    const thumbH = Math.max(8, Math.floor(trackH * trackH / contentH));
    const range  = trackH - thumbH;
    const thumbY = trackY + Math.floor(range * (this.scrollY / Math.max(1, this._maxScroll())));

    ctx.fillStyle = pal["lcd-pixel"];
    ctx.fillRect(trackX, thumbY, SCROLLBAR_W, thumbH);
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
