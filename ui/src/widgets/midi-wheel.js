// `midi-wheel` widget — vertical wheel for pitch-bend (variant "pb") or
// mod-wheel (variant "mw"). Was previously read-only; now bidirectional so
// the user can drag the wheel in the UI and the host can record the gesture
// as parameter automation.
//
// Drag model (mirrors knob.js):
//   - pointerdown : capture, beginGesture, record cursor origin.
//   - pointermove : up = increase. ~80 px = full sweep (typical PB wheel).
//                   Shift = fine drag (1/8 sensitivity).
//   - pointerup / pointercancel : endGesture, release capture.
//   - dblclick    : PB → 0.5 (centre), MW → 0 (rest).
//   - wheel       : ±1 step per scroll (1/100 of range, Shift = 1/500).
//
// Display: a .midi-wheel recess with a .wheel-thumb absolute-positioned
// child whose `bottom` percentage tracks the bound normalised value
// (0..1, 0 = bottom, 1 = top). The thumb position updates from both UI
// drag AND incoming MIDI / DAW automation via the bind.onChange listener.

import { applyTooltip } from "./tooltip-content.js";

const DRAG_PIXELS_FULL  = 80;
const SHIFT_DIVISOR     = 8;
const SCROLL_STEP_COARSE = 1 / 100;
const SCROLL_STEP_FINE   = 1 / 500;

function clamp01(v) { return Math.max(0, Math.min(1, v)); }

export function mount(host, opts = {}) {
  const {
    bind = null,           // bindSlider controller
    variant = "pb",        // "pb" | "mw"
    tipId = null,
  } = opts;

  host.classList.add("midi-wheel");
  host.classList.add(variant === "pb" ? "midi-wheel-pb" : "midi-wheel-mw");
  host.innerHTML = "";
  // Make the host capture pointer events even when the cursor isn't on the
  // thumb itself — touch the recess and you can drag.
  host.style.touchAction = "none";
  if (tipId) applyTooltip(host, tipId);

  const thumb = document.createElement("span");
  thumb.className = "wheel-thumb";
  // The drag target is the host; the thumb shouldn't intercept pointer
  // events or the cursor would feel "sticky" when crossing the thumb.
  thumb.style.pointerEvents = "none";
  host.appendChild(thumb);

  // PB centres at 0.5, MW rests at 0. These are also the dblclick reset
  // targets, exposed via the bind's defaultNormalised escape if available.
  const restValue = variant === "pb" ? 0.5 : 0;

  let value = restValue;        // last-known normalised value

  const applyVisual = (norm) => {
    // The thumb is small; positioning by its bottom-edge percent puts it
    // visually at the right point along the track.
    thumb.style.bottom = `${clamp01(norm) * 100}%`;
  };

  const writeValue = (v, gesture = true) => {
    value = clamp01(v);
    if (bind && gesture) bind.setNormalised(value);
    applyVisual(value);
  };

  applyVisual(value);

  // --- Pointer drag ---------------------------------------------------------
  let dragStart = null;     // { y, startValue, shift }

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    host.setPointerCapture(e.pointerId);
    dragStart = { y: e.clientY, startValue: value, shift: e.shiftKey };
    if (bind) bind.beginGesture();
  };
  const onPointerMove = (e) => {
    if (!dragStart) return;
    const dy = dragStart.y - e.clientY;             // up = increase
    const sensitivity = (dragStart.shift || e.shiftKey)
      ? DRAG_PIXELS_FULL * SHIFT_DIVISOR
      : DRAG_PIXELS_FULL;
    const dv = dy / sensitivity;
    writeValue(dragStart.startValue + dv);
  };
  const onPointerUp = (e) => {
    if (!dragStart) return;
    try { host.releasePointerCapture(e.pointerId); } catch (_) { /* already released */ }
    dragStart = null;
    if (bind) bind.endGesture();
  };
  const onDoubleClick = (e) => {
    e.preventDefault();
    if (bind) bind.beginGesture();
    writeValue(restValue);
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

  // --- Bind ----------------------------------------------------------------
  // onChange fires both on subscribe (hydrating the initial value) and on
  // any later change from MIDI / DAW automation / programmatic set. We
  // gate the write so a programmatic update doesn't double-fire back into
  // the apvts.
  let unsub = null;
  if (bind) unsub = bind.onChange((v) => writeValue(v, /*gesture*/ false));

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
