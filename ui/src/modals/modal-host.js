// Shared overlay host for Settings / About / confirmation modals
// (08-ui-views.md *Modal behaviour (shared)*).
//
// Contract:
//   - One modal open at a time. openModal() while another is open closes the
//     previous one first (so opening About from Settings replaces Settings —
//     the views-7 ↔ views-6 transition described in the task).
//   - The notification toast (z-index 100) sits above the modal overlay
//     (z-index 80) so warnings can still surface over an open modal.
//   - Esc dismisses; clicks on the dim layer dismiss; clicks on the modal
//     panel are absorbed.
//   - Modals never spawn an OS window — pure in-WebView overlay.
//
// API:
//   openModal({ build, onClose? }) — `build(closeFn)` returns the panel
//     DOM node. The returned `close` function (also called on Esc/dim-click)
//     dismisses the modal and runs onClose.
//   closeModal() — programmatic dismissal.

function ensureStyles() {
  if (document.getElementById("genvst-modal-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-modal-style";
  style.textContent = `
    .modal-host {
      position: fixed;
      inset: 0;
      z-index: 80;
      display: flex;
      align-items: center;
      justify-content: center;
      background: rgba(0, 0, 0, 0.55);
      backdrop-filter: blur(1px);
      cursor: default;
    }
    .modal-host.is-hidden { display: none; }
    .modal-panel {
      min-width: 360px;
      max-width: 760px;
      max-height: 480px;
      overflow: auto;
      padding: 16px 20px 18px;
      background: linear-gradient(180deg,
        var(--chassis-bg-top) 0%,
        var(--chassis-bg-mid) 50%,
        var(--chassis-bg-bottom) 100%);
      border: 1px solid var(--chassis-edge);
      border-radius: 6px;
      box-shadow:
        4px 6px 18px rgba(0, 0, 0, 0.7),
        inset 1px 1px 0 rgba(255, 255, 255, 0.35),
        inset -1px -1px 0 rgba(0, 0, 0, 0.25);
      color: var(--text-on-chassis);
      position: relative;
    }
    .modal-panel .modal-title {
      font: 500 13px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--text-on-chassis);
      margin-bottom: 12px;
      padding-right: 28px;
    }
    .modal-panel .modal-close {
      position: absolute;
      top: 10px;
      right: 10px;
      width: 22px;
      height: 22px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      background: linear-gradient(180deg, var(--btn-bg-top), var(--btn-bg-bottom));
      color: var(--btn-text);
      border: 1px solid var(--knob-rim);
      border-radius: 3px;
      font: 500 12px/1 "IBM Plex Mono", monospace;
      cursor: pointer;
    }
    .modal-panel .modal-body {
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    .modal-panel .modal-row {
      display: grid;
      grid-template-columns: 200px 1fr;
      align-items: center;
      gap: 12px;
      padding: 6px 4px;
      border-bottom: 1px solid rgba(0, 0, 0, 0.10);
    }
    .modal-panel .modal-row:last-child { border-bottom: 0; }
    .modal-panel .modal-row .modal-label {
      font: 500 10px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.16em;
      text-transform: uppercase;
      color: var(--text-on-chassis);
    }
    .modal-panel .modal-row .modal-control {
      display: inline-flex;
      align-items: center;
      gap: 8px;
    }
    .modal-panel .modal-footer {
      margin-top: 14px;
      display: flex;
      justify-content: flex-end;
      gap: 8px;
    }
    .modal-panel .modal-section-title {
      font: 500 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.20em;
      text-transform: uppercase;
      color: var(--label-text-dim);
      margin-top: 10px;
      margin-bottom: 4px;
    }
    .modal-panel .modal-btn-destructive {
      background: linear-gradient(180deg, #c84343 0%, #882020 100%);
      border-color: #2a0606;
      color: #fff;
    }
  `;
  document.head.appendChild(style);
}

// Singleton — one shared host per document. installed lazily on first open.
let hostEl = null;
let panelEl = null;
let currentClose = null;
let escHandler = null;

function ensureHost() {
  if (hostEl) return;
  hostEl = document.createElement("div");
  hostEl.className = "modal-host is-hidden";
  document.body.appendChild(hostEl);

  hostEl.addEventListener("mousedown", (e) => {
    // Click on the dim layer (not the panel) dismisses.
    if (e.target === hostEl) {
      if (currentClose) currentClose();
    }
  });
}

function installEsc() {
  if (escHandler) return;
  escHandler = (e) => {
    if (e.key === "Escape" && currentClose) {
      e.preventDefault();
      currentClose();
    }
  };
  document.addEventListener("keydown", escHandler);
}

export function openModal({ build, onClose, panelClass }) {
  ensureStyles();
  ensureHost();
  installEsc();

  // If another modal is open, close it first — only one modal at a time.
  if (currentClose) currentClose();

  panelEl = document.createElement("div");
  panelEl.className = "modal-panel" + (panelClass ? " " + panelClass : "");
  // Absorb clicks inside the panel so they don't bubble to the dim-layer
  // dismiss handler.
  panelEl.addEventListener("mousedown", (e) => e.stopPropagation());

  const close = () => {
    if (currentClose !== close) return;
    currentClose = null;
    if (hostEl) hostEl.classList.add("is-hidden");
    if (panelEl && panelEl.parentNode === hostEl) hostEl.removeChild(panelEl);
    panelEl = null;
    if (typeof onClose === "function") onClose();
  };
  currentClose = close;

  const content = build(close);
  if (content) panelEl.appendChild(content);

  hostEl.appendChild(panelEl);
  hostEl.classList.remove("is-hidden");

  return { close };
}

export function closeModal() {
  if (currentClose) currentClose();
}

// Confirmation modal — small helper used by Settings → RESET ALL.
export function openConfirm({ title, message, confirmLabel = "OK", destructive = false, onConfirm }) {
  return openModal({
    build: (close) => {
      const wrap = document.createElement("div");
      const titleEl = document.createElement("div");
      titleEl.className = "modal-title";
      titleEl.textContent = title || "Confirm";
      wrap.appendChild(titleEl);

      const closeX = document.createElement("button");
      closeX.type = "button";
      closeX.className = "modal-close";
      closeX.textContent = "X";
      closeX.addEventListener("click", () => close());
      wrap.appendChild(closeX);

      const body = document.createElement("div");
      body.className = "modal-body";
      const msg = document.createElement("div");
      msg.style.font = "400 12px/1.4 \"IBM Plex Mono\", monospace";
      msg.style.color = "var(--text-on-chassis)";
      msg.style.maxWidth = "440px";
      msg.textContent = message || "";
      body.appendChild(msg);
      wrap.appendChild(body);

      const footer = document.createElement("div");
      footer.className = "modal-footer";
      const cancel = document.createElement("button");
      cancel.type = "button";
      cancel.className = "btn";
      cancel.textContent = "CANCEL";
      cancel.addEventListener("click", () => close());
      const confirm = document.createElement("button");
      confirm.type = "button";
      confirm.className = "btn" + (destructive ? " modal-btn-destructive" : "");
      confirm.textContent = confirmLabel;
      confirm.addEventListener("click", () => {
        close();
        if (typeof onConfirm === "function") onConfirm();
      });
      footer.appendChild(cancel);
      footer.appendChild(confirm);
      wrap.appendChild(footer);

      return wrap;
    },
  });
}
