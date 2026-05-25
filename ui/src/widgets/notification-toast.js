// `notification-toast` widget — transient banners for errors / warnings /
// info messages pushed from the C++ side via the `notify` event
// (05-ui-ux.md *C++ -> JS notifications*).
//
// Mount once at the top of the page; the widget subscribes to backend
// `notify` events automatically and renders into a fixed-position stack of
// up to 2 visible toasts at a time. Anything beyond that is queued and
// dispatched as toasts auto-dismiss after ~4 s. Click a toast to dismiss
// immediately.

import { onBackendEvent } from "../binding.js";

const VISIBLE_CAP   = 2;
const AUTO_DISMISS  = 4000;

function ensureStyles() {
  if (document.getElementById("genvst-toast-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-toast-style";
  style.textContent = `
    .toast-host {
      position: fixed;
      bottom: 14px;
      right: 14px;
      display: flex;
      flex-direction: column;
      gap: 8px;
      z-index: 100;
      pointer-events: none;
    }
    .toast-host .toast {
      pointer-events: auto;
      min-width: 220px;
      max-width: 360px;
      padding: 10px 14px;
      border-radius: 4px;
      font: 500 11px/1.3 "IBM Plex Mono", monospace;
      letter-spacing: 0.04em;
      color: #fff;
      cursor: pointer;
      box-shadow:
        0 6px 14px rgba(0,0,0,0.55),
        inset 1px 1px 0 rgba(255,255,255,0.10);
      border: 1px solid rgba(0,0,0,0.6);
      background: var(--accent-info);
      transform: translateX(8px);
      opacity: 0;
      transition: transform 180ms ease-out, opacity 180ms ease-out;
    }
    .toast-host .toast.is-visible {
      transform: translateX(0);
      opacity: 1;
    }
    .toast-host .toast.is-warning { background: var(--accent-warning); }
    .toast-host .toast.is-error   { background: var(--accent-error); }
  `;
  document.head.appendChild(style);
}

export function mount(host, opts = {}) {
  const { autoSubscribe = true } = opts;

  ensureStyles();
  host.classList.add("toast-host");

  const queue = [];        // pending payloads
  const active = [];       // { el, dismissTimer }

  const dismiss = (rec) => {
    const idx = active.indexOf(rec);
    if (idx >= 0) active.splice(idx, 1);
    if (rec.dismissTimer) clearTimeout(rec.dismissTimer);
    rec.el.classList.remove("is-visible");
    setTimeout(() => {
      if (rec.el.parentNode === host) host.removeChild(rec.el);
      drain();
    }, 200);
  };

  const renderOne = (payload) => {
    const el = document.createElement("div");
    el.className = "toast";
    const level = (payload && payload.level) || "info";
    if (level === "warning") el.classList.add("is-warning");
    if (level === "error")   el.classList.add("is-error");
    el.textContent = (payload && payload.message) || "";
    host.appendChild(el);
    // Force a layout pass before applying the visible class so the
    // transform animates in.
    void el.offsetHeight;
    el.classList.add("is-visible");

    const rec = { el, dismissTimer: null };
    rec.dismissTimer = window.setTimeout(() => dismiss(rec), AUTO_DISMISS);
    el.addEventListener("click", () => dismiss(rec));
    active.push(rec);
  };

  const drain = () => {
    while (active.length < VISIBLE_CAP && queue.length > 0) {
      renderOne(queue.shift());
    }
  };

  const push = (payload) => {
    queue.push(payload || {});
    drain();
  };

  let unsubNotify = null;
  if (autoSubscribe) {
    unsubNotify = onBackendEvent("notify", (payload) => push(payload));
  }

  return {
    push,
    dispose() {
      if (unsubNotify) unsubNotify();
      for (const rec of active) {
        if (rec.dismissTimer) clearTimeout(rec.dismissTimer);
        if (rec.el.parentNode === host) host.removeChild(rec.el);
      }
      active.length = 0;
      queue.length = 0;
    },
  };
}
