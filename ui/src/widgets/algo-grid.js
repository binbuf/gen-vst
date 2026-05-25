// `algo-grid` widget — 8-button picker for the YM2612 algorithm topologies.
//
// 2-column × 4-row CSS grid (.algo-grid recipe in design-system.css). All 8
// buttons visible at once; the selected one carries .is-active. No popover,
// no overflow.
//
// Binds to an integer apvts param via bindSlider; the relay's normalised
// value maps to the integer 0..7. The optional `mini` companion redraws via
// onChange callback (the per-mode panel wires that, not this widget).

import { applyTooltip } from "./tooltip-content.js";

const NUM_ALGOS = 8;

export function mount(host, opts = {}) {
  const {
    bind = null,
    onSelect = null,    // optional: called with idx whenever selection changes
    tipId = null,
  } = opts;

  host.classList.add("algo-grid");
  host.innerHTML = "";
  if (tipId) applyTooltip(host, tipId);

  const buttons = [];
  for (let i = 0; i < NUM_ALGOS; ++i) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "algo-btn";
    btn.textContent = String(i + 1);
    btn.dataset.algo = String(i);
    host.appendChild(btn);
    buttons.push(btn);
  }

  let currentIdx = 0;

  const intToNorm = (n) => {
    if (!bind || !bind.state) return n / (NUM_ALGOS - 1);
    const { start, end } = bind.state.properties;
    if (!Number.isFinite(start) || !Number.isFinite(end) || end === start) {
      return n / (NUM_ALGOS - 1);
    }
    return (n - start) / (end - start);
  };
  const normToInt = (norm) => {
    if (!bind || !bind.state) return Math.round(norm * (NUM_ALGOS - 1));
    const { start, end } = bind.state.properties;
    if (!Number.isFinite(start) || !Number.isFinite(end)) {
      return Math.round(norm * (NUM_ALGOS - 1));
    }
    return Math.round(start + norm * (end - start));
  };

  const applyHighlight = () => {
    for (let i = 0; i < NUM_ALGOS; ++i) {
      buttons[i].classList.toggle("is-active", i === currentIdx);
    }
  };

  const setIndex = (idx, gesture = true) => {
    currentIdx = Math.max(0, Math.min(NUM_ALGOS - 1, idx));
    applyHighlight();
    if (gesture && bind) {
      bind.beginGesture();
      bind.setNormalised(intToNorm(currentIdx));
      bind.endGesture();
    }
    if (onSelect) onSelect(currentIdx);
  };

  for (const btn of buttons) {
    btn.addEventListener("click", () => {
      setIndex(parseInt(btn.dataset.algo, 10), true);
    });
  }

  let unsub = null;
  if (bind) unsub = bind.onChange((norm) => {
    currentIdx = normToInt(norm);
    applyHighlight();
    if (onSelect) onSelect(currentIdx);
  });
  else      applyHighlight();

  return {
    setIndex(i) { setIndex(i, false); },
    getIndex() { return currentIdx; },
    dispose() {
      if (unsub) unsub();
    },
  };
}
