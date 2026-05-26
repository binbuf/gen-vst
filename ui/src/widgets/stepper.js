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
    // Optional sparse value list — e.g. [0, 8, 9, 10, 11, 12, 13, 14, 15] for
    // SSG-EG, where 1..7 are not valid hardware states. When set, ▲/▼ cycle
    // through the sequence, and any bound apvts value outside the sequence
    // displays as the nearest lower entry (so a loaded patch with ssg=5
    // reads as "OFF" rather than a misleading shape name).
    valueSequence = null,
  } = opts;

  host.classList.add("stepper-readout");
  if (sizeMini) host.classList.add("is-mini");
  host.innerHTML = "";
  if (tipId) applyTooltip(host, tipId);

  // LCD cell — canvas-only; the canvas draws its own background, border, and
  // glow. No .lcd CSS class here because that class adds padding + overflow:
  // hidden which clips the canvas inside a smaller visible area.
  const lcdCell = document.createElement("div");
  host.appendChild(lcdCell);

  const lcd = mountLcd(lcdCell, {
    width:  sizeMini ? 36 : 52,
    height: sizeMini ? 14 : 20,
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

  // Snap an arbitrary int to the largest sequence value ≤ n. SSG-EG: ssg=5
  // → 0 (OFF), ssg=12 → 12. Only used when valueSequence is set.
  const snap = (n) => {
    if (!valueSequence || valueSequence.length === 0) return n;
    for (let i = valueSequence.length - 1; i >= 0; --i) {
      if (valueSequence[i] <= n) return valueSequence[i];
    }
    return valueSequence[0];
  };

  const writeInt = (n, gesture = true) => {
    const { lo, hi } = range();
    currentValue = Math.max(lo, Math.min(hi, Math.round(n)));
    if (bind && gesture) bind.setNormalised(intToNorm(currentValue));
    updateLcd();
  };

  const inc = () => {
    if (valueSequence) {
      const cur = snap(currentValue);
      const idx = valueSequence.indexOf(cur);
      const next = idx < 0 ? 0 : Math.min(valueSequence.length - 1, idx + 1);
      writeInt(valueSequence[next]);
    } else {
      writeInt(currentValue + step);
    }
  };
  const dec = () => {
    if (valueSequence) {
      const cur = snap(currentValue);
      const idx = valueSequence.indexOf(cur);
      const prev = idx <= 0 ? 0 : idx - 1;
      writeInt(valueSequence[prev]);
    } else {
      writeInt(currentValue - step);
    }
  };

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
      // Display the snapped label when a sparse sequence is configured —
      // a freshly loaded patch may carry a value that isn't on the grid
      // (e.g. legacy TFI with ssg=5), and showing the raw value would
      // misrepresent the hardware behaviour.
      if (valueSequence) currentValue = snap(currentValue);
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
