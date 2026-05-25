// `slider` widget — horizontal slider with a chunky thumb.
//
// CSS-only chassis (groove + thumb gradients) + JS drag handler. Interaction
// matches the knob but along the X axis:
//   - Horizontal drag = change value.
//   - Shift = fine.
//   - Double-click = reset.
//   - Scroll wheel = ±1 step.

import { applyTooltip } from "./tooltip-content.js";

const SCROLL_STEP_COARSE = 1 / 100;
const SCROLL_STEP_FINE   = 1 / 500;
const DRAG_SHIFT_DIVISOR = 8;

function ensureStyles() {
  if (document.getElementById("genvst-slider-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-slider-style";
  style.textContent = `
    .slider {
      position: relative;
      width: 140px;
      height: 22px;
      background: linear-gradient(180deg, #0d1018 0%, #1a1d22 100%);
      border: 1px solid var(--inset-edge-dark);
      border-radius: 4px;
      box-shadow:
        inset 2px 2px 4px rgba(0, 0, 0, 0.6),
        inset -1px -1px 0 var(--inset-edge-light);
      cursor: ew-resize;
      touch-action: none;
    }
    .slider .slider-thumb {
      position: absolute;
      top: 2px;
      bottom: 2px;
      width: 14px;
      transform: translateX(-50%);
      background: linear-gradient(180deg,
        rgba(255,255,255,0.20) 0%,
        var(--knob-body-light) 35%,
        var(--knob-body-dark)  100%);
      border: 1px solid var(--knob-rim);
      border-radius: 2px;
      box-shadow:
        1px 1px 2px rgba(0,0,0,0.5),
        inset 1px 1px 0 rgba(255,255,255,0.18);
      pointer-events: none;
    }
    .slider:active .slider-thumb {
      transform: translateX(-50%) scale(0.97);
      transition: transform 80ms ease-out;
    }
  `;
  document.head.appendChild(style);
}

function clamp01(v) { return Math.max(0, Math.min(1, v)); }

export function mount(host, opts = {}) {
  const { bind = null, tipId = null, defaultNormalised = null } = opts;

  ensureStyles();
  host.classList.add("slider");
  if (tipId) applyTooltip(host, tipId);

  const thumb = document.createElement("div");
  thumb.className = "slider-thumb";
  host.appendChild(thumb);

  let value = 0;
  const updateThumb = () => {
    thumb.style.left = `${value * 100}%`;
  };

  const writeValue = (v, gesture = true) => {
    value = clamp01(v);
    if (bind && gesture) bind.setNormalised(value);
    updateThumb();
  };

  // --- Drag ----------------------------------------------------------
  let dragStart = null;        // { x, startValue, width, shift }

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    host.setPointerCapture(e.pointerId);
    const rect = host.getBoundingClientRect();
    dragStart = {
      x: e.clientX,
      startValue: value,
      width: rect.width,
      shift: e.shiftKey,
    };
    if (bind) bind.beginGesture();
    // Click-to-position: jump the value to where the user clicked unless
    // they're holding Shift (fine drag mode — start where we are).
    if (!e.shiftKey) {
      const rel = (e.clientX - rect.left) / rect.width;
      writeValue(rel);
      dragStart.startValue = value;
    }
  };
  const onPointerMove = (e) => {
    if (!dragStart) return;
    const dx = e.clientX - dragStart.x;
    const sensitivity = (dragStart.shift || e.shiftKey) ? DRAG_SHIFT_DIVISOR : 1;
    const dv = (dx / dragStart.width) / sensitivity;
    writeValue(dragStart.startValue + dv);
  };
  const onPointerUp = (e) => {
    if (!dragStart) return;
    host.releasePointerCapture(e.pointerId);
    dragStart = null;
    if (bind) bind.endGesture();
  };
  const onDoubleClick = (e) => {
    e.preventDefault();
    const reset = defaultNormalised != null
      ? defaultNormalised
      : (bind ? bind.defaultNormalised(0.5) : 0.5);
    if (bind) bind.beginGesture();
    writeValue(reset);
    if (bind) bind.endGesture();
  };
  const onWheel = (e) => {
    e.preventDefault();
    const step = e.shiftKey ? SCROLL_STEP_FINE : SCROLL_STEP_COARSE;
    const dir  = e.deltaY < 0 ? +1 : -1;
    if (bind) bind.beginGesture();
    writeValue(value + dir * step);
    if (bind) bind.endGesture();
  };

  host.addEventListener("pointerdown",   onPointerDown);
  host.addEventListener("pointermove",   onPointerMove);
  host.addEventListener("pointerup",     onPointerUp);
  host.addEventListener("pointercancel", onPointerUp);
  host.addEventListener("dblclick",      onDoubleClick);
  host.addEventListener("wheel",         onWheel, { passive: false });

  let unsub = null;
  if (bind) unsub = bind.onChange((v) => writeValue(v, false));
  else      updateThumb();

  return {
    setValue(v) { writeValue(v, false); },
    getValue()  { return value; },
    dispose() {
      host.removeEventListener("pointerdown",   onPointerDown);
      host.removeEventListener("pointermove",   onPointerMove);
      host.removeEventListener("pointerup",     onPointerUp);
      host.removeEventListener("pointercancel", onPointerUp);
      host.removeEventListener("dblclick",      onDoubleClick);
      host.removeEventListener("wheel",         onWheel);
      if (unsub) unsub();
      if (thumb.parentNode === host) host.removeChild(thumb);
    },
  };
}
