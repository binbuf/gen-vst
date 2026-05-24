/*
 * notification-toast — the single user-visible error/status channel
 * (08-ui-views.md view 8, 05-ui-ux.md "C++ → JS notifications").
 *
 * Driven by the C++ -> JS `notify` event { level, message } emitted by the
 * processor / editor on patch-load failures, rejected DMP versions, missing
 * custom roots, etc. The toast container sits above the modal root in the
 * DOM so toasts surface above any open modal.
 *
 * Behaviour (08-ui-views.md view 8):
 *   - Position: slides down from the top edge, centered below the header.
 *   - Levels: `info` / `warn` / `error`; each gets a distinct palette
 *     (green-LCD / logo-yellow / LED-red).
 *   - Duration: auto-dismiss after ~4 s; click to dismiss.
 *   - Stacking: at most two visible; further notifications queue.
 */

import { palette } from "./pixel.js";

const MAX_VISIBLE = 2;
const AUTO_DISMISS_MS = 4000;
const SLIDE_DURATION_MS = 220;

export class NotificationToastHost {
  constructor(container) {
    this.container = container;
    this.container.classList.add("toast-host");
    this.visible = [];
    this.queue = [];
  }

  /** Push a notification. `level` ∈ {"info","warn","error"}. */
  push({ level = "info", message = "" }) {
    const lvl = ["info", "warn", "error"].includes(level) ? level : "info";
    const item = { level: lvl, message: String(message ?? "") };
    if (this.visible.length < MAX_VISIBLE)
      this._show(item);
    else
      this.queue.push(item);
  }

  _show(item) {
    const el = document.createElement("div");
    el.className = `toast toast-${item.level}`;
    this._applyPalette(el, item.level);

    const text = document.createElement("span");
    text.className = "toast-message";
    text.textContent = item.message;
    el.appendChild(text);

    el.addEventListener("click", () => this._dismiss(el));
    this.container.appendChild(el);

    const entry = { el, item, timer: null };
    this.visible.push(entry);

    // Slide-down animation: start above the container, animate to 0.
    requestAnimationFrame(() => { el.classList.add("toast-shown"); });

    entry.timer = setTimeout(() => this._dismiss(el), AUTO_DISMISS_MS);
  }

  _dismiss(el) {
    const idx = this.visible.findIndex(e => e.el === el);
    if (idx < 0) return;
    const entry = this.visible[idx];
    clearTimeout(entry.timer);
    this.visible.splice(idx, 1);

    // Slide-up: remove the shown class, drop the element after the CSS
    // transition completes.
    el.classList.remove("toast-shown");
    el.classList.add("toast-hiding");
    setTimeout(() => {
      el.remove();
      // Pull from the queue if there is anything waiting.
      if (this.queue.length > 0 && this.visible.length < MAX_VISIBLE)
        this._show(this.queue.shift());
    }, SLIDE_DURATION_MS);
  }

  _applyPalette(el, level) {
    const pal = palette();
    // Hard 1–2px borders, no radius, per the pixel-art rules. CSS classes
    // also exist for these as a fallback if the palette cache hasn't loaded.
    if (level === "error") {
      el.style.background = pal["led-base"] || "#2a0808";
      el.style.color      = pal["led-on"]   || "#ff2020";
      el.style.borderColor = pal["led-on"]  || "#ff2020";
    } else if (level === "warn") {
      el.style.background = pal["logo-dark"] || "#2a2208";
      el.style.color      = pal["logo"]     || "#f5c842";
      el.style.borderColor = pal["logo"]    || "#f5c842";
    } else {
      el.style.background = pal["lcd-base"] || "#3d5a2e";
      el.style.color      = pal["lcd-pixel"]|| "#a8d878";
      el.style.borderColor = pal["lcd-pixel"]|| "#a8d878";
    }
  }
}
