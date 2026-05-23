/*
 * Shared modal framework — 08-ui-views.md "Modal behaviour (shared)".
 *
 * One OS window: every "modal" in Gen VST is an in-WebView overlay layer (a
 * DOM layer drawn on the same canvas), not a separate OS window. The host
 * provides the single overlay+panel scaffolding so each modal (Settings, MIDI
 * routing, About, Patch browser in Task 14) just renders its body into a
 * pre-styled panel.
 *
 * Behaviour:
 *   - Only one modal open at a time; opening a second one replaces the first.
 *     Views 5 and 7 are explicitly opened *from* view 6 and replace it.
 *   - Click on the dimmed overlay outside the panel does not reach the main
 *     UI but does NOT dismiss the modal — only Close / [X] / Esc dismiss.
 *   - Esc dismisses the topmost modal.
 *   - The notification toast (view 8) may still appear above the modal — the
 *     toast container sits above the modal root in the DOM so its z-order is
 *     naturally higher; see chassis.css.
 */

const STATE = {
  current: null,   // { root, close }
  keyListener: null,
};

function ensureRoot() {
  let root = document.getElementById("modal-root");
  if (!root) {
    root = document.createElement("div");
    root.id = "modal-root";
    document.body.appendChild(root);
  }
  return root;
}

function installKeyListenerIfNeeded() {
  if (STATE.keyListener) return;
  STATE.keyListener = (e) => {
    if (e.key === "Escape" && STATE.current) {
      e.preventDefault();
      STATE.current.close();
    }
  };
  window.addEventListener("keydown", STATE.keyListener);
}

/**
 * Open a modal. Builds the overlay + panel DOM, calls `build(panel, ctx)` so
 * the caller fills the panel, and returns a close function. Any existing
 * modal is closed first (one at a time).
 *
 * @param {Object} options
 *   - title       (string)   Title shown in the panel header.
 *   - width       (number)   Panel width in CSS px (defaults 640).
 *   - height      (number?)  Optional panel height; otherwise auto.
 *   - build       (panel, ctx) => void   Populates the panel body.
 *   - onClose     () => void  Optional callback after the modal is dismissed.
 *
 * The build callback's `ctx.close()` dismisses the modal — used by inner
 * `Close` buttons.
 */
export function openModal(options) {
  const { title = "", width = 640, height = null, build, onClose } = options;

  // Replace any existing modal — view 5/7 open from view 6 (Settings), and
  // this is how that replacement works.
  if (STATE.current) {
    STATE.current.close({ silent: true });
  }

  const root = ensureRoot();
  installKeyListenerIfNeeded();

  // Overlay catches click events so the main UI behind cannot receive them.
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  // Clicks on the overlay (outside the panel) are swallowed but do NOT
  // dismiss the modal — per 08-ui-views.md, only Close / [X] / Esc dismiss.
  overlay.addEventListener("pointerdown", (e) => {
    if (e.target === overlay) e.stopPropagation();
  });

  const panel = document.createElement("div");
  panel.className = "modal-panel bevel-raised";
  panel.style.width = `${width}px`;
  if (height) panel.style.height = `${height}px`;

  const header = document.createElement("div");
  header.className = "modal-header";
  const titleEl = document.createElement("span");
  titleEl.className = "modal-title label";
  titleEl.textContent = title;
  header.appendChild(titleEl);

  const closeBtn = document.createElement("button");
  closeBtn.type = "button";
  closeBtn.className = "modal-close bevel-raised label";
  closeBtn.textContent = "X";
  closeBtn.addEventListener("click", () => close());
  header.appendChild(closeBtn);

  const body = document.createElement("div");
  body.className = "modal-body";

  panel.appendChild(header);
  panel.appendChild(body);
  overlay.appendChild(panel);
  root.appendChild(overlay);

  document.body.dataset.modalOpen = "true";

  let closed = false;
  function close(args = {}) {
    if (closed) return;
    closed = true;
    overlay.remove();
    if (STATE.current === handle) STATE.current = null;
    if (!document.querySelector(".modal-overlay"))
      delete document.body.dataset.modalOpen;
    if (!args.silent && onClose) {
      try { onClose(); } catch { /* ignore */ }
    }
  }

  const ctx = { close, panel, body, header };
  const handle = { root: overlay, close };
  STATE.current = handle;

  try {
    build(body, ctx);
  } catch (err) {
    console.error("modal build failed:", err);
    close();
  }

  return close;
}

/** Close any currently-open modal. Used by callers that want to dismiss
 *  programmatically (e.g. after a successful action). */
export function closeAnyModal() {
  if (STATE.current) STATE.current.close();
}

/** True if any modal is open right now. */
export function isModalOpen() {
  return STATE.current !== null;
}

/**
 * Tiny confirm-modal helper for destructive actions (RESET PART, RESET ALL).
 * Opens a small modal asking the user to confirm, calls `onConfirm` if they
 * say yes. Cancel / Esc / [X] dismiss without firing the callback.
 */
export function confirmModal({ title, message, confirmLabel = "OK", onConfirm }) {
  openModal({
    title,
    width: 420,
    build: (body, ctx) => {
      const msg = document.createElement("p");
      msg.className = "label confirm-message";
      msg.textContent = message;
      body.appendChild(msg);

      const row = document.createElement("div");
      row.className = "modal-footer confirm-buttons";

      const cancel = document.createElement("button");
      cancel.type = "button";
      cancel.className = "settings-button bevel-raised label";
      cancel.textContent = "CANCEL";
      cancel.addEventListener("click", () => ctx.close());
      row.appendChild(cancel);

      const ok = document.createElement("button");
      ok.type = "button";
      ok.className = "settings-button bevel-raised label confirm-destructive";
      ok.textContent = confirmLabel;
      ok.addEventListener("click", () => {
        ctx.close();
        try { onConfirm?.(); } catch (e) { console.error(e); }
      });
      row.appendChild(ok);

      body.appendChild(row);
    },
  });
}
