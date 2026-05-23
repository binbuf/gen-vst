/*
 * tooltip — global hover-tooltip controller.
 *
 * Any element carrying a `data-tip="..."` attribute shows the tooltip on
 * pointerover and hides it on pointerout. A single shared bubble element
 * lives on the document so we don't create one per widget.
 *
 * The visibility is gated by the `tooltips_enabled` apvts boolean, so a
 * Settings-modal toggle can switch them off globally without each widget
 * having to opt in.
 */

import { bindToggle } from "../binding.js";

let bubbleEl = null;
let installed = false;
let enabled = true;
let unsubscribeFromBinding = null;

function ensureBubble() {
  if (bubbleEl) return bubbleEl;
  bubbleEl = document.createElement("div");
  bubbleEl.className = "tooltip-bubble bevel-raised label";
  bubbleEl.style.position = "fixed";
  bubbleEl.style.display = "none";
  bubbleEl.style.zIndex = "200";
  bubbleEl.style.pointerEvents = "none";
  document.body.appendChild(bubbleEl);
  return bubbleEl;
}

function showTip(target, x, y) {
  if (!enabled) return;
  const text = target?.getAttribute?.("data-tip");
  if (!text) return;
  const b = ensureBubble();
  b.textContent = text.toUpperCase();
  b.style.display = "block";
  // Position above-and-right of the cursor; clamp inside the viewport so the
  // bubble doesn't overflow the 960x560 chassis.
  const padding = 6;
  // Position is computed after the element is visible so we know its size.
  const rect = b.getBoundingClientRect();
  let nx = x + 10;
  let ny = y - rect.height - 8;
  if (nx + rect.width  > window.innerWidth - padding)  nx = window.innerWidth  - rect.width  - padding;
  if (ny < padding) ny = y + 16;
  b.style.left = `${Math.round(nx)}px`;
  b.style.top  = `${Math.round(ny)}px`;
}

function hideTip() {
  if (bubbleEl) bubbleEl.style.display = "none";
}

function onOver(e) {
  const t = e.target?.closest?.("[data-tip]");
  if (!t) return;
  showTip(t, e.clientX, e.clientY);
}

function onMove(e) {
  if (!bubbleEl || bubbleEl.style.display === "none") return;
  const t = e.target?.closest?.("[data-tip]");
  if (!t) { hideTip(); return; }
  // Re-position to follow the cursor.
  showTip(t, e.clientX, e.clientY);
}

function onOut(e) {
  const t = e.target?.closest?.("[data-tip]");
  if (!t) return;
  if (e.relatedTarget && t.contains(e.relatedTarget)) return;
  hideTip();
}

/**
 * Install the global hover tooltip listeners. Idempotent — calling twice
 * is a no-op (the FM view mounts before other views, so the install order
 * is well-defined). Subscribes to the `tooltips_enabled` toggle so the
 * Settings modal can flip them on/off.
 */
export function installTooltips() {
  if (installed) return;
  installed = true;

  document.addEventListener("pointerover", onOver);
  document.addEventListener("pointermove", onMove);
  document.addEventListener("pointerout",  onOut);
  // Hide on any user gesture that takes focus elsewhere.
  window.addEventListener("blur", hideTip);
  document.addEventListener("pointerdown", hideTip);

  try {
    const binding = bindToggle("tooltips_enabled");
    enabled = !!binding.getValue?.() ?? true;
    unsubscribeFromBinding = binding.onChange?.(() => {
      enabled = !!binding.getValue?.();
      if (!enabled) hideTip();
    }) ?? null;
  } catch {
    // Binding not yet available (dev gallery, fallback panel). Default on.
    enabled = true;
  }
}

/** For tests / hot-reload — tear down everything we installed. */
export function uninstallTooltips() {
  if (!installed) return;
  document.removeEventListener("pointerover", onOver);
  document.removeEventListener("pointermove", onMove);
  document.removeEventListener("pointerout",  onOut);
  window.removeEventListener("blur", hideTip);
  document.removeEventListener("pointerdown", hideTip);
  unsubscribeFromBinding?.();
  unsubscribeFromBinding = null;
  if (bubbleEl) { bubbleEl.remove(); bubbleEl = null; }
  installed = false;
}
