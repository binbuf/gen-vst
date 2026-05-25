// `note-on-led` widget — 16 px red LED + "NOTE ON" caption.
//
// Pure DOM. Either:
//   - Bind to a bool apvts param via opts.bind (bindToggle controller) — used
//     by the gallery so a manual toggle drives the LED, or
//   - Auto-bind to the `meterData` event push (telemetry-driven) by setting
//     opts.fromTelemetry = true — the canonical chassis use.
//
// Recipe: .note-on cluster wraps a .note-on-led (lit) + .note-on-text caption.
// `.is-off` modifier dims the LED.

import { applyTooltip } from "./tooltip-content.js";
import { onBackendEvent } from "../binding.js";

export function mount(host, opts = {}) {
  const {
    bind = null,                // bindToggle controller; optional
    fromTelemetry = false,      // subscribe to meterData.noteOn
    caption = "Note On",
    tipId = null,
  } = opts;

  host.classList.add("note-on");
  host.innerHTML = "";

  const led = document.createElement("span");
  led.className = "note-on-led is-off";
  if (tipId) applyTooltip(led, tipId);
  host.appendChild(led);

  const text = document.createElement("span");
  text.className = "note-on-text";
  text.textContent = caption;
  host.appendChild(text);

  const setLit = (on) => led.classList.toggle("is-off", !on);

  let unsubBind = null;
  let unsubEvent = null;

  if (bind) unsubBind = bind.onChange((v) => setLit(Boolean(v)));
  if (fromTelemetry) {
    unsubEvent = onBackendEvent("meterData", (payload) => {
      setLit(Boolean(payload && payload.noteOn));
    });
  }
  if (!bind && !fromTelemetry) setLit(false);

  return {
    setLit,
    dispose() {
      if (unsubBind)  unsubBind();
      if (unsubEvent) unsubEvent();
    },
  };
}
