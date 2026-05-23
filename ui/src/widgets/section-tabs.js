/*
 * section-tabs / pill-buttons — a row of segmented selectors. One is selected
 * at a time; the selected segment is drawn with a red highlight per the
 * genny-ui.md "selected item" idiom. The widget binds to a combo-box relay
 * (one parameter, N choices) so changes round-trip through the apvts.
 *
 * Used by both the FM / SQ / D chip-section selector and the PRESETS / IMPORT
 * tabs; the same widget covers both visual styles via the `style` option.
 */

import { setupPixelCanvas, palette, drawBevel, snap, drawLabel } from "./pixel.js";

const PAD_X = 4;
const PAD_Y = 2;
const GAP = 2;

export class SectionTabs {
  constructor(canvas, binding, options = {}) {
    this.canvas = canvas;
    this.binding = binding;
    this.style = options.style ?? "pill";   // "pill" | "tab"
    this.fontSize = options.fontSize ?? 8;
    this.labels = options.labels ?? null;   // override choices

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    canvas.addEventListener("click", e => this._onClick(e));

    this._unsubscribe = this.binding.onChange(() => this.render());
    this._unsubscribeProps = this.binding.onProperties?.(() => this.render()) ?? null;

    this.render();
  }

  _labels() {
    return this.labels ?? this.binding.getChoices();
  }

  _segmentRects() {
    const labels = this._labels();
    if (labels.length === 0) return [];
    // Each segment is sized by its label length; the row is laid out left-to-
    // right with GAP between segments.
    const charW = this.fontSize;     // Press Start 2P is 1:1 cell
    const rects = [];
    let x = 0;
    for (const label of labels) {
      const text = String(label).toUpperCase();
      const w = text.length * charW + PAD_X * 2;
      rects.push({ x, y: 0, w, h: this.h, text });
      x += w + GAP;
    }
    return rects;
  }

  _onClick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const rects = this._segmentRects();
    for (let i = 0; i < rects.length; ++i) {
      const r = rects[i];
      if (x >= r.x && x < r.x + r.w) {
        this.binding.setIndex(i);
        return;
      }
    }
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.clearRect(0, 0, this.w, this.h);

    const rects = this._segmentRects();
    const selected = this.binding.getIndex();

    for (let i = 0; i < rects.length; ++i) {
      const r = rects[i];
      const isSel = i === selected;

      if (this.style === "tab") {
        // Tab style — no chrome on unselected, active gets a red underline
        // (2 px thick) and red text (matching the PRESETS / IMPORT pair).
        ctx.fillStyle = pal["chassis"];
        ctx.fillRect(r.x, r.y, r.w, r.h);
        drawLabel(ctx, r.x + PAD_X, r.y + PAD_Y, r.text,
                  this.fontSize, isSel ? pal["led-on"] : pal["label"]);
        if (isSel) {
          ctx.fillStyle = pal["led-on"];
          ctx.fillRect(r.x + PAD_X, r.y + r.h - 2, r.w - PAD_X * 2, 2);
        }
      } else {
        // Pill style — beveled body, red highlight when selected. The
        // selected pill picks up an additional 1-px red ring outside the
        // bevel so it reads as a stamped switch.
        ctx.fillStyle = isSel ? pal["select"] : pal["panel"];
        ctx.fillRect(r.x, r.y, r.w, r.h);
        drawBevel(ctx, r.x, r.y, r.w, r.h, true);
        if (isSel) {
          // 1-px red highlight ring just outside the bevel — clear "this
          // segment is active" cue, particularly at small fontSize values.
          ctx.fillStyle = pal["led-on"];
          ctx.fillRect(r.x, r.y - 1, r.w, 1);             // top
          ctx.fillRect(r.x, r.y + r.h, r.w, 1);           // bottom
          ctx.fillRect(r.x - 1, r.y, 1, r.h);             // left
          ctx.fillRect(r.x + r.w, r.y, 1, r.h);           // right
        }
        drawLabel(ctx, r.x + PAD_X, r.y + PAD_Y, r.text,
                  this.fontSize, isSel ? pal["label"] : pal["label"]);
      }
    }
  }

  destroy() {
    this._unsubscribe?.();
    this._unsubscribeProps?.();
  }
}
