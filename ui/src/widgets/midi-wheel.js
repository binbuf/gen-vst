// `midi-wheel` widget — read-only vertical wheel visualising live MIDI
// pitch-bend (variant "pb") or mod-wheel (variant "mw") state.
//
// Pure DOM: a .midi-wheel recess with a .wheel-thumb absolute-positioned
// child whose `bottom` percentage tracks the bound value.
//
// Read-only — no drag handler. Driven by the apvts params `pitch_bend_value`
// (normalised 0..1, 0.5 = centre) and `mod_wheel_value` (0..1, 0 = bottom).
// The variant controls the centerline pseudo-element via the CSS class.

import { applyTooltip } from "./tooltip-content.js";

export function mount(host, opts = {}) {
  const {
    bind = null,           // bindSlider controller
    variant = "pb",        // "pb" | "mw"
    tipId = null,
  } = opts;

  host.classList.add("midi-wheel");
  host.classList.add(variant === "pb" ? "midi-wheel-pb" : "midi-wheel-mw");
  host.innerHTML = "";
  if (tipId) applyTooltip(host, tipId);

  const thumb = document.createElement("span");
  thumb.className = "wheel-thumb";
  host.appendChild(thumb);

  const applyNorm = (norm) => {
    // The thumb is 6 px tall; positioning by its bottom-edge percent puts it
    // visually at the right point along the track.
    const clamped = Math.max(0, Math.min(1, norm));
    thumb.style.bottom = `${clamped * 100}%`;
  };

  // Initial position: PB centres at 0.5, MW rests at 0.
  applyNorm(variant === "pb" ? 0.5 : 0);

  let unsub = null;
  if (bind) unsub = bind.onChange((v) => applyNorm(v));

  return {
    setValue(v) { applyNorm(v); },
    dispose() {
      if (unsub) unsub();
      if (thumb.parentNode === host) host.removeChild(thumb);
    },
  };
}
