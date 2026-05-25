// `tooltip` widget — global hover descriptor system. Single shared
// .tooltip DOM node + one delegated handler installed on a root element
// (typically the chassis frame).
//
// Reads `data-tip-name` and `data-tip-desc` from the closest ancestor of the
// hovered element that carries both attributes. ~400 ms enter delay (so a
// passing cursor doesn't dump a tooltip on every pixel), no exit delay.
//
// Gated by the `tooltips_enabled` apvts bool (header TIPS toggle + Settings
// TOOLTIPS row). When off the handler early-returns and hides any visible
// tooltip.
//
// Position: near the cursor, clamped inside a 1200x560 viewport so the
// tooltip never clips off the chassis edge (ADR-0023 fixed window).

import { bindToggle } from "../binding.js";

const ENTER_DELAY_MS = 400;
const CURSOR_OFFSET_X = 12;
const CURSOR_OFFSET_Y = 16;
const VIEWPORT_W = 1200;
const VIEWPORT_H = 560;
const EDGE_PAD = 6;

export function installTooltips(root) {
  // The shared tooltip node — one per document; safe to call multiple times
  // (we key on a data-attribute to avoid duplicates).
  let tipEl = document.querySelector(".tooltip[data-genvst-tooltip]");
  if (!tipEl) {
    tipEl = document.createElement("div");
    tipEl.className = "tooltip is-hidden";
    tipEl.setAttribute("data-genvst-tooltip", "1");
    const nameRow = document.createElement("span");
    nameRow.className = "tip-name";
    const descRow = document.createElement("span");
    descRow.className = "tip-desc";
    tipEl.appendChild(nameRow);
    tipEl.appendChild(descRow);
    document.body.appendChild(tipEl);
  }
  const nameRow = tipEl.querySelector(".tip-name");
  const descRow = tipEl.querySelector(".tip-desc");

  const enabledBind = bindToggle("tooltips_enabled");
  let enabled = true;
  const unsubEnabled = enabledBind.onChange((on) => {
    enabled = Boolean(on);
    if (!enabled) hideTooltip();
  });

  let enterTimer = null;
  let lastEl = null;

  const findTipHost = (el) => {
    while (el && el !== document.documentElement) {
      if (el.hasAttribute && el.hasAttribute("data-tip-name")
          && el.hasAttribute("data-tip-desc")) {
        return el;
      }
      el = el.parentElement;
    }
    return null;
  };

  const showTooltip = (el, clientX, clientY) => {
    nameRow.textContent = el.getAttribute("data-tip-name") || "";
    descRow.textContent = el.getAttribute("data-tip-desc") || "";
    tipEl.classList.remove("is-hidden");

    // Measure after content set, before position clamp.
    const box = tipEl.getBoundingClientRect();
    let x = clientX + CURSOR_OFFSET_X;
    let y = clientY + CURSOR_OFFSET_Y;
    if (x + box.width  > VIEWPORT_W - EDGE_PAD) x = VIEWPORT_W - EDGE_PAD - box.width;
    if (y + box.height > VIEWPORT_H - EDGE_PAD) y = clientY - CURSOR_OFFSET_Y - box.height;
    if (x < EDGE_PAD) x = EDGE_PAD;
    if (y < EDGE_PAD) y = EDGE_PAD;
    tipEl.style.left = `${x}px`;
    tipEl.style.top  = `${y}px`;
  };

  const hideTooltip = () => {
    tipEl.classList.add("is-hidden");
    if (enterTimer) { clearTimeout(enterTimer); enterTimer = null; }
  };

  const onPointerMove = (e) => {
    if (!enabled) { hideTooltip(); return; }

    const target = e.target instanceof Element ? findTipHost(e.target) : null;
    if (target !== lastEl) {
      lastEl = target;
      hideTooltip();
      if (!target) return;
      const cx = e.clientX, cy = e.clientY;
      enterTimer = window.setTimeout(() => {
        showTooltip(target, cx, cy);
      }, ENTER_DELAY_MS);
    } else if (target && !tipEl.classList.contains("is-hidden")) {
      // Tooltip is already showing; track the cursor.
      showTooltip(target, e.clientX, e.clientY);
    }
  };

  const onPointerLeave = () => {
    lastEl = null;
    hideTooltip();
  };

  root.addEventListener("pointermove",  onPointerMove);
  root.addEventListener("pointerleave", onPointerLeave);

  return {
    dispose() {
      root.removeEventListener("pointermove",  onPointerMove);
      root.removeEventListener("pointerleave", onPointerLeave);
      unsubEnabled();
      hideTooltip();
    },
  };
}
