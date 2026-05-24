/*
 * instrument-rack — Task 22 / 08-ui-views.md view 1 (revised center column).
 *
 * Renders a green-LCD list of rack rows, one per active rack slot, plus a
 * trailing "+" cell. Each row shows:
 *
 *   ◇  type-icon  patch-name           C  P  :::  −
 *
 * - type-icon: sine glyph (FM), square (SQ), tiny drum-kit pixel (D)
 * - patch-name: from getRackState() — for FM that's the patch file stem, for
 *   SQ a literal "PSG n / Noise" label, for D the loaded WAV name (or
 *   "— no sample —" if none)
 * - "C": copy the row's full state into the editor-session clipboard (Task 33).
 * - "P": paste the clipboard into this row — only shown when the clipboard is
 *   non-empty AND its captured type matches this row's type.
 * - "+" / "-": add a row (opens the type popover) / remove the selected row
 * - drag-handle (:::): grab + drag to reorder rows (Task 27).
 *
 * Selection: clicking a row calls `onSelect({ type, slotIndex, paramSuffix })`
 * — the parent FM view rebinds the per-instrument routing strip + paging path.
 *
 * Drag-drop reorder (Task 27): pointerdown on the ::: column starts a drag.
 * pointermove updates a virtual insertion index and renders a translucent
 * ghost row at the cursor plus a 1-px insertion-line cue. pointerup commits
 * the new order via `onReorder(fromIndex, toIndex)`. Auto-scroll fires when
 * the cursor enters an 8-px zone at the top or bottom of the canvas. The
 * trailing "+ ADD INSTRUMENT" row is rejected as a drop target.
 *
 * The widget is fully canvas-driven for visual parity with the existing
 * Instruments LCD list it replaces.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

const ROW_H        = 14;
const ROW_PAD_TOP  = 4;
const ICON_X       = 6;
const ICON_W       = 10;
const FONT_PX      = 8;
// Task 34 — per-row activity LED strip. 10 dots (FM 1-6 + PSG 1-3 + NOISE)
// sit between the type icon and the patch name, vertically centered. The
// strip width is constant regardless of clipboard state, so the visible name
// space stays stable when the paste glyph appears.
const LED_SIZE     = 2;
const LED_GAP      = 1;
const LED_COUNT    = 10;
const LED_STRIP_W  = LED_COUNT * LED_SIZE + (LED_COUNT - 1) * LED_GAP;
const LED_STRIP_X  = ICON_X + ICON_W + 2;
const NAME_X       = LED_STRIP_X + LED_STRIP_W + 4;
const REMOVE_W     = 14;        // width of the "-" cell at the right edge
const SCROLLBAR_W  = 6;
const ADD_ROW_LABEL = "+ ADD INSTRUMENT";
// Drag-handle column (Task 27). Width matches the inline "::: " glyph and the
// gap that follows it before the remove cell.
const DRAG_HANDLE_W   = 14;
// Copy + paste cells (Task 33). Reserve space for both regardless of clipboard
// state so toggling the clipboard never reflows the row layout — the paste
// glyph itself is drawn conditionally.
const COPY_W  = 11;
const PASTE_W = 11;
// Pointer-movement threshold (px²) before a press becomes a real drag. Below
// this, the press falls through to the normal click handler so a fingertap
// on the handle still selects the row.
const DRAG_THRESHOLD_SQ = 9;
// Auto-scroll trigger zone (px) at the top + bottom of the canvas while drag
// is active, and how often the rack steps by one row.
const AUTOSCROLL_EDGE_PX = 8;
const AUTOSCROLL_INTERVAL_MS = 200;

export class InstrumentRack {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.rows   = options.rows ?? [];      // [{ type, slotIndex, patchName, ... }]
    this.selected = options.selected ?? 0; // row index of the highlighted row
    this.onSelect = options.onSelect ?? null;
    this.onAdd    = options.onAdd    ?? null;   // (clientX, clientY) -> opens popover
    this.onRemove = options.onRemove ?? null;   // (row) -> remove from rack
    this.onReorder = options.onReorder ?? null; // (fromIndex, toIndex) -> reorder
    this.onCopy   = options.onCopy   ?? null;   // (row) -> Task 33 copy
    this.onPaste  = options.onPaste  ?? null;   // (row) -> Task 33 paste

    // Task 33 — Editor-session clipboard type ("fm" | "sq" | "d" | null). The
    // FM view sets this via setClipboardType after a copy succeeds; null hides
    // every paste glyph. The rack does NOT own the payload — only the type,
    // which is enough to decide which rows show the paste affordance.
    this.clipboardType = null;

    // Task 34 — per-row activity bitmasks (10 bits each: FM 1-6 + PSG 1-3 +
    // NOISE). Stored parallel to this.rows; setActiveMasks updates entries
    // without rebuilding the row list. Empty / undefined entries render as
    // all-off, so a missing notify field cannot light spurious LEDs.
    this.activeMasks = [];

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.scrollY = 0;

    // Task 27 drag state — populated on pointerdown over the ::: column, kept
    // alive across pointermoves, cleared on pointerup. While null, the widget
    // behaves identically to its pre-Task-27 self.
    this._rowDrag = null;
    this._autoScrollTimer = null;
    this._autoScrollDir = 0;
    this._suppressNextClick = false;

    canvas.addEventListener("click", e => this._onClick(e));
    canvas.addEventListener("wheel", e => this._onWheel(e), { passive: false });
    canvas.addEventListener("pointerdown", e => this._onPointerDown(e));
    canvas.addEventListener("pointermove", e => this._onPointerMove(e));
    canvas.addEventListener("pointerup",   e => this._onPointerUp(e));
    canvas.addEventListener("pointercancel", e => this._onPointerUp(e));

    this.render();
  }

  setRows(rows, selectedIndex = 0) {
    this.rows = rows;
    if (selectedIndex < -1) selectedIndex = -1;
    if (selectedIndex >= rows.length) selectedIndex = Math.max(-1, rows.length - 1);
    this.selected = selectedIndex;
    this.scrollY = 0;
    // Drop stale activity masks past the new row count; the next notify tick
    // will repopulate. Leaving them in place would leak a previous row's lit
    // LEDs onto a freshly-added row at the same index.
    if (this.activeMasks.length > rows.length)
      this.activeMasks.length = rows.length;
    this.render();
  }

  // Task 34 — Update per-row activity LEDs. Called from the meterData event
  // handler at ~30 Hz. Skips the repaint when nothing actually changed so a
  // long-held note doesn't burn a canvas fill every tick.
  setActiveMasks(masks) {
    if (!Array.isArray(masks)) return;
    const n = this.rows.length;
    let changed = false;
    for (let i = 0; i < n; ++i) {
      const m = (masks[i] | 0) & 0x3ff;
      if ((this.activeMasks[i] | 0) !== m) {
        this.activeMasks[i] = m;
        changed = true;
      }
    }
    if (this.activeMasks.length > n) {
      this.activeMasks.length = n;
      changed = true;
    }
    if (changed) this.render();
  }

  setSelected(index) {
    this.selected = typeof index === "number" ? index : -1;
    this.render();
  }

  // Task 33 — Show/hide the paste glyph on rows whose type matches `type`.
  // Pass `null` to hide every paste glyph (clipboard empty).
  setClipboardType(type) {
    this.clipboardType = type ?? null;
    this.render();
  }

  // Right-edge cell X coordinates for one data row. Centralises the layout so
  // _onClick hit-testing and _renderDataRow stay in sync.
  _rowCells() {
    const removeX = this.w - SCROLLBAR_W - REMOVE_W;
    const dragX   = removeX - DRAG_HANDLE_W;
    const pasteX  = dragX   - PASTE_W;
    const copyX   = pasteX  - COPY_W;
    return { copyX, pasteX, dragX, removeX };
  }

  // Find the row index from a Y pixel coordinate, accounting for scroll. The
  // "+" cell sits one row past the last data row. Returns -1 if outside.
  _hitRowIndex(y) {
    const row = Math.floor((y - ROW_PAD_TOP + this.scrollY) / ROW_H);
    return row;
  }

  _onClick(e) {
    // Drag commits clear this flag on the synthetic click that browsers emit
    // after pointerup — without the guard, a drag-drop would also fire row
    // selection or row removal.
    if (this._suppressNextClick) {
      this._suppressNextClick = false;
      return;
    }

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
    const { copyX, pasteX, removeX } = this._rowCells();
    if (x >= removeX) {
      this.onRemove?.(row);
      return;
    }
    if (x >= pasteX && x < pasteX + PASTE_W) {
      // Paste hit is silently ignored on rows whose type doesn't match the
      // clipboard — the glyph isn't drawn in that case, so clicking the
      // empty space behaves like clicking the row body (select it).
      if (this.clipboardType && this.clipboardType === row.type) {
        this.onPaste?.(row);
        return;
      }
    }
    if (x >= copyX && x < copyX + COPY_W) {
      this.onCopy?.(row);
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

  // Returns the X range of the drag-handle column (the ::: glyph). Used by
  // _onPointerDown to decide between starting a row-drag and falling through
  // to the normal scrollbar / row-select / remove behaviour.
  _dragHandleRange() {
    const { dragX } = this._rowCells();
    return { lo: dragX, hi: dragX + DRAG_HANDLE_W };
  }

  _isOnDragHandle(x) {
    const { lo, hi } = this._dragHandleRange();
    return x >= lo && x < hi;
  }

  _onPointerDown(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    // Scrollbar column takes precedence over everything else.
    if (x >= this.w - SCROLLBAR_W) {
      this._scrollDrag = { startY: e.clientY, startScroll: this.scrollY };
      this.canvas.setPointerCapture(e.pointerId);
      return;
    }

    // Drag-handle column starts a row drag — but only for real data rows. The
    // trailing "+ ADD INSTRUMENT" cell stays sticky.
    if (!this._isOnDragHandle(x)) return;
    const idx = this._hitRowIndex(y);
    if (idx < 0 || idx >= this.rows.length) return;

    this._rowDrag = {
      pointerId: e.pointerId,
      fromIndex: idx,
      startX: x,
      startY: y,
      currentY: y,
      gapIdx: idx,
      moved: false,
    };
    this.canvas.setPointerCapture(e.pointerId);
  }

  _onPointerMove(e) {
    if (this._scrollDrag) {
      const trackH = this.h - ROW_PAD_TOP * 2;
      const max = this._maxScroll();
      if (max <= 0) return;
      const dy = e.clientY - this._scrollDrag.startY;
      const scrollPerPx = max / Math.max(1, trackH - 16);
      this.scrollY = Math.max(0, Math.min(max,
        this._scrollDrag.startScroll + dy * scrollPerPx));
      this.render();
      return;
    }

    if (!this._rowDrag) return;

    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    const dx = x - this._rowDrag.startX;
    const dy = y - this._rowDrag.startY;
    if (!this._rowDrag.moved && (dx * dx + dy * dy) >= DRAG_THRESHOLD_SQ)
      this._rowDrag.moved = true;

    this._rowDrag.currentY = y;
    this._rowDrag.gapIdx   = this._computeGapIndex(y);

    this._updateAutoScroll(y);
    this.render();
  }

  _onPointerUp(e) {
    if (this._scrollDrag) {
      this._scrollDrag = null;
      if (this.canvas.hasPointerCapture(e.pointerId))
        this.canvas.releasePointerCapture(e.pointerId);
      return;
    }

    if (!this._rowDrag) return;
    const drag = this._rowDrag;
    this._rowDrag = null;
    this._stopAutoScroll();
    if (this.canvas.hasPointerCapture(drag.pointerId))
      this.canvas.releasePointerCapture(drag.pointerId);

    if (drag.moved) {
      // A real drag — eat the synthetic click that follows pointerup so the
      // row's normal click-to-select path doesn't fire on top of the reorder.
      this._suppressNextClick = true;

      const fromIdx = drag.fromIndex;
      const gapIdx  = drag.gapIdx;
      const toIdx   = gapIdx > fromIdx ? gapIdx - 1 : gapIdx;

      if (toIdx !== fromIdx) {
        // Selection follows the dragged row so the FM paging / routing-strip
        // binding keeps focusing the same instrument after the move.
        if (this.selected === fromIdx) {
          this.selected = toIdx;
        } else if (this.selected > fromIdx && this.selected <= toIdx) {
          this.selected -= 1;
        } else if (this.selected < fromIdx && this.selected >= toIdx) {
          this.selected += 1;
        }
        // Optimistic local reorder so the rack repaints cleanly while the
        // native fn round-trips. The parent re-fetches state via
        // getRackState() afterwards and will overwrite this if the C++ side
        // disagrees.
        const moved = this.rows.splice(fromIdx, 1)[0];
        this.rows.splice(toIdx, 0, moved);
        this.onReorder?.(fromIdx, toIdx);
      }
    }

    this.render();
  }

  // Compute the row insertion index ("gap") for a given Y. 0 means "before
  // the first row", rows.length means "after the last data row". The "+ ADD
  // INSTRUMENT" cell is rejected as a drop target by clamping at rows.length.
  _computeGapIndex(y) {
    const rawGap = Math.round((y + this.scrollY - ROW_PAD_TOP) / ROW_H);
    return Math.max(0, Math.min(this.rows.length, rawGap));
  }

  _updateAutoScroll(y) {
    let dir = 0;
    if (y < ROW_PAD_TOP + AUTOSCROLL_EDGE_PX) dir = -1;
    else if (y > this.h - ROW_PAD_TOP - AUTOSCROLL_EDGE_PX) dir = +1;

    if (dir === 0) {
      this._stopAutoScroll();
      return;
    }

    if (this._autoScrollDir === dir && this._autoScrollTimer !== null) return;

    this._stopAutoScroll();
    this._autoScrollDir = dir;
    this._autoScrollTimer = setInterval(() => {
      const max = this._maxScroll();
      const next = Math.max(0, Math.min(max, this.scrollY + dir * ROW_H));
      if (next === this.scrollY) {
        this._stopAutoScroll();
        return;
      }
      this.scrollY = next;
      if (this._rowDrag)
        this._rowDrag.gapIdx = this._computeGapIndex(this._rowDrag.currentY);
      this.render();
    }, AUTOSCROLL_INTERVAL_MS);
  }

  _stopAutoScroll() {
    if (this._autoScrollTimer !== null) {
      clearInterval(this._autoScrollTimer);
      this._autoScrollTimer = null;
    }
    this._autoScrollDir = 0;
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

    // Task 27 — ghost row + insertion-line cue overlayed on top of the
    // already-drawn rows while a drag is in progress. Drawn last so they
    // cover the source row's static position.
    if (this._rowDrag && this._rowDrag.moved) {
      this._renderDragOverlay();
    }

    ctx.restore();

    this._drawScrollbar();
  }

  _renderDataRow(i) {
    const y = ROW_PAD_TOP + i * ROW_H - this.scrollY;
    this._renderRowAt(this.rows[i], y, i === this.selected,
                      this.activeMasks[i] | 0);
  }

  _renderRowAt(row, y, isSel, activeMask = 0) {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w;

    if (isSel) {
      ctx.fillStyle = pal["lcd-pixel"];
      ctx.fillRect(1, y, W - SCROLLBAR_W - 2, ROW_H);
      ctx.fillStyle = pal["lcd-base"];
    } else {
      ctx.fillStyle = pal["lcd-pixel"];
    }

    // Type icon.
    this._drawTypeIcon(ICON_X, y + 2, row.type, isSel);

    // Task 34 — per-channel activity LEDs between the type icon and the
    // patch name. Drawn unconditionally (unlit dots stay visible) so the
    // row's horizontal rhythm matches across every row regardless of state.
    this._drawActivityStrip(LED_STRIP_X, y + Math.floor((ROW_H - LED_SIZE) / 2),
                             activeMask, isSel);

    // Patch name — uppercase, trimmed to fit before the copy cell. The right
    // edge reserves COPY_W + PASTE_W + DRAG_HANDLE_W + REMOVE_W + SCROLLBAR_W
    // regardless of clipboard state so toggling the clipboard never reflows
    // the visible name.
    const { copyX, pasteX, dragX, removeX } = this._rowCells();
    const nameMaxPx = copyX - NAME_X - 2;
    const name = (row.patchName ?? "").toString().toUpperCase();
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    this._drawClippedText(name, NAME_X, snap(y + (ROW_H - FONT_PX) / 2), nameMaxPx);

    // Task 33 — Copy glyph on every row. Paste glyph only when the clipboard
    // is non-empty AND its captured type matches this row's type.
    this._drawCopyCell(copyX, y, isSel);
    if (this.clipboardType && this.clipboardType === row.type)
      this._drawPasteCell(pasteX, y, isSel);

    // Drag-handle glyph (live target for Task 27 drag).
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-text-dark"] || pal["lcd-pixel"];
    this._drawDragHandle(dragX, y + 3);

    // "-" remove button.
    this._drawRemoveCell(removeX, y, isSel);
  }

  _renderDragOverlay() {
    const ctx = this.ctx;
    const pal = palette();
    const W = this.w;
    const drag = this._rowDrag;

    // 1-px insertion cue at the candidate gap, clamped inside the visible
    // strip so it doesn't get lost in the top/bottom padding.
    const gapY = ROW_PAD_TOP + drag.gapIdx * ROW_H - this.scrollY;
    ctx.fillStyle = pal["lcd-pixel"];
    ctx.fillRect(1, gapY, W - SCROLLBAR_W - 2, 1);

    // Translucent ghost row centered on the cursor's Y. Re-rendering the row
    // gives a faithful drag preview without re-implementing the glyph layout.
    const ghostY = snap(drag.currentY - ROW_H / 2);
    const row = this.rows[drag.fromIndex];
    if (row) {
      ctx.save();
      ctx.globalAlpha = 0.65;
      this._renderRowAt(row, ghostY, true,
                        this.activeMasks[drag.fromIndex] | 0);
      ctx.restore();
    }
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

  // Task 34 — Draw the 10-LED per-channel activity strip. bit 0 is leftmost
  // (FM 1), bit 9 is rightmost (PSG noise). Lit dots follow the same
  // selection-aware colour rule as the other glyphs (lcd-pixel on an unsel
  // row, lcd-base on the lcd-pixel-bg of a selected row); unlit dots stay at
  // lcd-base-hi everywhere so the strip reads as a panel of indicators
  // rather than disappearing on the selected row.
  _drawActivityStrip(x, y, mask, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    const lit = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    const dim = pal["lcd-base-hi"];
    for (let i = 0; i < LED_COUNT; ++i) {
      ctx.fillStyle = ((mask >> i) & 1) ? lit : dim;
      ctx.fillRect(snap(x + i * (LED_SIZE + LED_GAP)), snap(y),
                   LED_SIZE, LED_SIZE);
    }
  }

  _drawRemoveCell(x, y, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    // A thin "-" glyph centered in the cell. Color follows selection.
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    ctx.fillRect(x + 3, y + Math.floor(ROW_H / 2), REMOVE_W - 6, 1);
  }

  // Task 33 — letterform glyphs for the copy + paste cells. Drawn as solid
  // 5x7-ish pixel letters in the canvas's standard pixel font so they read at
  // the same scale as the surrounding "+" / "-" cells. Same selection-aware
  // colour rule as the other cells.
  _drawCopyCell(x, y, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    ctx.fillText("C", snap(x + (COPY_W - FONT_PX) / 2),
                       snap(y + (ROW_H - FONT_PX) / 2));
  }

  _drawPasteCell(x, y, isSel) {
    const ctx = this.ctx;
    const pal = palette();
    ctx.fillStyle = isSel ? pal["lcd-base"] : pal["lcd-pixel"];
    ctx.fillText("P", snap(x + (PASTE_W - FONT_PX) / 2),
                       snap(y + (ROW_H - FONT_PX) / 2));
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
