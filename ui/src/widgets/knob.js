// `knob` widget — skeuomorphic rotary control.
//
// DOM:  <div class="knob"><canvas/></div>
// Body comes from the .knob CSS recipe in design-system.css (gradient body,
// top sheen, layered shadows). The canvas overlays a 1.5 px white indicator
// line from centre to rim, rotated by the current value.
//
// Interaction (08-ui-views.md widget references):
//   - Vertical drag: up = increase. ~150 px = full range.
//   - Shift = fine drag (1/8 sensitivity).
//   - Double-click = reset to opts.defaultNormalised (or 0.5).
//   - Scroll wheel = ±1 step (1/100 of range, finer with Shift).
//   - :active scales to 0.97 via the CSS recipe — no JS needed there.
//
// Sweep ~270°, rest at the 7 o'clock position; both numbers match
// 09-visual-spec.md *Knob*.

import { applyTooltip } from "./tooltip-content.js";

const TWO_PI = Math.PI * 2;
// 7 o'clock = 210° measured from the 12 o'clock vertical axis. Atan2 / canvas
// coords give us angles measured from +x rotated clockwise; the 7 o'clock
// position in standard math (atan2-style, +x = 0, +y = π/2 down) sits at
// 0.75π (135°) — converted to canvas: π * 0.75 starting from 9 o'clock,
// rotating clockwise. Easier to express as:
//   start angle (value=0) = 7 o'clock = 0.75π below horizontal-left
//   end angle (value=1)   = 5 o'clock = 0.25π below horizontal-right
// Sweep = 270° = 3π/2. The math below is in standard canvas convention
// where 0 = +x (3 o'clock), positive = clockwise (down on screen).
const KNOB_START_ANGLE = Math.PI * 0.75;     // 7 o'clock — value = 0
const KNOB_SWEEP       = Math.PI * 1.5;      // 270° sweep
// → end at KNOB_START_ANGLE + KNOB_SWEEP = 9π/4 ≡ π/4 = 5 o'clock. Good.

const SCROLL_STEP_COARSE = 1 / 100;
const SCROLL_STEP_FINE   = 1 / 500;
const DRAG_PIXELS_FULL   = 150;
const DRAG_SHIFT_DIVISOR = 8;

function cssVar(name, fallback = "#fff") {
  const v = getComputedStyle(document.documentElement)
    .getPropertyValue(name).trim();
  return v || fallback;
}

function clamp01(v) { return Math.max(0, Math.min(1, v)); }

export function mount(host, opts = {}) {
  const {
    bind = null,           // controller from binding.js bindSlider(...)
    size = 48,             // default 48px; decimator-knob passes 96
    extraClass = "",       // e.g. "decimator-knob", "knob-sm"
    tipId = null,          // tooltip key in tooltip-content.js
    defaultNormalised = null,  // explicit reset value; falls back to bind's default
  } = opts;

  // Ensure the host carries the .knob class — works for both bare <div>
  // hosts and pre-classed ones (e.g. the gallery wraps with .knob-cell).
  host.classList.add("knob");
  if (extraClass) {
    for (const cls of extraClass.split(/\s+/)) {
      if (cls) host.classList.add(cls);
    }
  }
  host.style.width = host.style.height = `${size}px`;

  // Wipe any static indicator (the mockup uses ::after; the live widget
  // draws on a canvas so the pseudo-element would double-render).
  // We can't actually remove a pseudo-element from JS, but the recipe lets
  // us override it via the data-render attribute trick: add a stylesheet
  // rule once, then opt the host into the override.
  installCanvasOverride();
  host.dataset.knobLive = "1";

  const canvas = document.createElement("canvas");
  canvas.style.position = "absolute";
  canvas.style.inset = "0";
  canvas.style.width  = `${size}px`;
  canvas.style.height = `${size}px`;
  // Touch the canvas pointer events off so the host's cursor/drag still works
  // when the user clicks the indicator line itself.
  canvas.style.pointerEvents = "none";
  host.appendChild(canvas);

  if (tipId) applyTooltip(host, tipId);

  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  canvas.width  = Math.round(size * dpr);
  canvas.height = Math.round(size * dpr);
  ctx.scale(dpr, dpr);
  ctx.imageSmoothingEnabled = true;

  let value = 0;        // last-known normalised value
  const draw = () => {
    ctx.clearRect(0, 0, size, size);

    const cx = size / 2;
    const cy = size / 2;
    const radius = size * 0.42;

    const angle = KNOB_START_ANGLE + KNOB_SWEEP * value;

    ctx.strokeStyle = cssVar("--knob-indicator", "#f8fafc");
    ctx.lineCap = "round";
    ctx.lineWidth = Math.max(1.25, size * 0.038);
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(angle) * radius * 0.85,
               cy + Math.sin(angle) * radius * 0.85);
    ctx.stroke();
  };

  const writeValue = (v, gesture = true) => {
    value = clamp01(v);
    if (bind && gesture) bind.setNormalised(value);
    draw();
  };

  // --- Pointer drag ---------------------------------------------------
  let dragStart = null;        // { y, startValue, shift }

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    host.setPointerCapture(e.pointerId);
    dragStart = { y: e.clientY, startValue: value, shift: e.shiftKey };
    if (bind) bind.beginGesture();
  };
  const onPointerMove = (e) => {
    if (!dragStart) return;
    const dy = dragStart.y - e.clientY;           // up = increase
    const sensitivity = (dragStart.shift || e.shiftKey)
      ? DRAG_PIXELS_FULL * DRAG_SHIFT_DIVISOR
      : DRAG_PIXELS_FULL;
    const dv = dy / sensitivity;
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

  host.addEventListener("pointerdown",  onPointerDown);
  host.addEventListener("pointermove",  onPointerMove);
  host.addEventListener("pointerup",    onPointerUp);
  host.addEventListener("pointercancel", onPointerUp);
  host.addEventListener("dblclick",     onDoubleClick);
  host.addEventListener("wheel",        onWheel, { passive: false });

  // --- Bind --------------------------------------------------------------
  let unsub = null;
  if (bind) unsub = bind.onChange((v) => writeValue(v, /*gesture*/ false));
  else      draw();   // hydrate from value=0 with no bind

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
      if (canvas.parentNode === host) host.removeChild(canvas);
    },
  };
}

// The .knob CSS recipe draws a static indicator line via ::after for the
// mockup pages. Live knobs draw the indicator on a canvas, so suppress the
// pseudo-element only on hosts that have been wired by mount() — keying on
// data-knob-live="1". Installed once per document.
let canvasOverrideInstalled = false;
function installCanvasOverride() {
  if (canvasOverrideInstalled) return;
  canvasOverrideInstalled = true;
  const style = document.createElement("style");
  style.textContent = `
    .knob[data-knob-live="1"]::after { content: none !important; }
    .knob[data-knob-live="1"] { position: relative; }
  `;
  document.head.appendChild(style);
}
