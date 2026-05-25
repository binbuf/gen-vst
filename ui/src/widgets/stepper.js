// `stepper` widget — LCD value + ▲/▼ click-and-hold buttons.
//
// CSS frame (.stepper-readout in design-system.css) holds an .lcd readout
// flanked by two .stepper-btn arrows. Behaviour (08-ui-views.md widget
// references):
//   - ▲/▼ click = ±step.
//   - Click-and-hold ≥ 300 ms = repeat at ~10 Hz.
//   - Scroll wheel = ±step.
//
// Bound to an integer apvts param via bindSlider; the controller exposes
// state.properties.start / .end so we can map the relay's normalised value
// to the integer range for display.

import { mount as mountLcd } from "./lcd-readout.js";
import { applyTooltip } from "./tooltip-content.js";

const HOLD_DELAY_MS = 300;
const HOLD_RATE_MS  = 100;       // ~10 Hz

export function mount(host, opts = {}) {
  const {
    bind = null,
    step = 1,
    formatter = (intVal) => String(intVal),
    sizeMini = false,
    tipId = null,
  } = opts;

  host.classList.add("stepper-readout");
  if (sizeMini) host.classList.add("is-mini");
  host.innerHTML = "";
  if (tipId) applyTooltip(host, tipId);

  // LCD cell -- styled via the .lcd recipe so the host's CSS captures the
  // recessed chrome; the canvas readout draws the glowing digits inside.
  const lcdCell = document.createElement("div");
  lcdCell.className = "lcd";
  if (sizeMini) {
    lcdCell.style.minWidth = "38px";
    lcdCell.style.padding  = "2px 4px";
  }
  host.appendChild(lcdCell);

  const lcd = mountLcd(lcdCell, {
    width:  sizeMini ? 36 : 52,
    height: sizeMini ? 14 : 16,
    fontPx: sizeMini ? 9  : 11,
    align:  "center",
    initialText: "0",
  });

  const buttons = document.createElement("div");
  buttons.className = "stepper-buttons";
  const btnUp   = document.createElement("button");
  const btnDown = document.createElement("button");
  btnUp.type   = "button";
  btnDown.type = "button";
  btnUp.className   = "stepper-btn";
  btnDown.className = "stepper-btn";
  btnUp.textContent   = "▲";
  btnDown.textContent = "▼";
  buttons.appendChild(btnUp);
  buttons.appendChild(btnDown);
  host.appendChild(buttons);

  // Integer-range helpers driven by the bound parameter's properties. Falls
  // back to a 0..127 range if no bind is given (gallery uses bind).
  const range = () => {
    if (bind && bind.state) {
      const { start, end } = bind.state.properties;
      if (Number.isFinite(start) && Number.isFinite(end)) {
        return { lo: start, hi: end };
      }
    }
    return { lo: 0, hi: 127 };
  };

  const normToInt = (norm) => {
    const { lo, hi } = range();
    return Math.round(lo + norm * (hi - lo));
  };
  const intToNorm = (val) => {
    const { lo, hi } = range();
    return hi === lo ? 0 : (val - lo) / (hi - lo);
  };

  let currentValue = 0;
  const updateLcd = () => lcd.setText(formatter(currentValue));

  const writeInt = (n, gesture = true) => {
    const { lo, hi } = range();
    currentValue = Math.max(lo, Math.min(hi, Math.round(n)));
    if (bind && gesture) bind.setNormalised(intToNorm(currentValue));
    updateLcd();
  };

  const inc = () => writeInt(currentValue + step);
  const dec = () => writeInt(currentValue - step);

  // --- Click-and-hold ----------------------------------------------------
  const installHold = (btn, action) => {
    let holdTimer = null;
    let repeatTimer = null;
    let pointerActive = false;

    const start = (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      pointerActive = true;
      if (bind) bind.beginGesture();
      action();
      holdTimer = window.setTimeout(() => {
        repeatTimer = window.setInterval(action, HOLD_RATE_MS);
      }, HOLD_DELAY_MS);
    };
    const stop = () => {
      if (!pointerActive) return;
      pointerActive = false;
      if (holdTimer)   { clearTimeout(holdTimer);   holdTimer = null; }
      if (repeatTimer) { clearInterval(repeatTimer); repeatTimer = null; }
      if (bind) bind.endGesture();
    };

    btn.addEventListener("pointerdown",   start);
    btn.addEventListener("pointerup",     stop);
    btn.addEventListener("pointerleave",  stop);
    btn.addEventListener("pointercancel", stop);
    return () => {
      btn.removeEventListener("pointerdown",   start);
      btn.removeEventListener("pointerup",     stop);
      btn.removeEventListener("pointerleave",  stop);
      btn.removeEventListener("pointercancel", stop);
      if (holdTimer)   clearTimeout(holdTimer);
      if (repeatTimer) clearInterval(repeatTimer);
    };
  };

  const teardownUp   = installHold(btnUp,   inc);
  const teardownDown = installHold(btnDown, dec);

  // --- Scroll wheel ------------------------------------------------------
  const onWheel = (e) => {
    e.preventDefault();
    if (bind) bind.beginGesture();
    if (e.deltaY < 0) inc(); else dec();
    if (bind) bind.endGesture();
  };
  host.addEventListener("wheel", onWheel, { passive: false });

  // --- Bind --------------------------------------------------------------
  let unsub = null;
  if (bind) {
    unsub = bind.onChange((norm) => {
      currentValue = normToInt(norm);
      updateLcd();
    });
  } else {
    updateLcd();
  }

  return {
    setValue(v) { writeInt(v, false); },
    getValue()  { return currentValue; },
    dispose() {
      teardownUp();
      teardownDown();
      host.removeEventListener("wheel", onWheel);
      lcd.dispose();
      if (unsub) unsub();
    },
  };
}
