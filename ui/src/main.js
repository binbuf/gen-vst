/*
 * Gen VST UI entry point.
 *
 * The main page (index.html) is still the Task 03 placeholder shell: the
 * static chassis layout + the one working master-gain control. The full
 * widget library (Task 10) is mounted via the gallery dev page
 * (gallery.html) — index.html uses the same library for its one knob so the
 * gallery and the real UI exercise the same code paths.
 *
 * Task 11 will replace the placeholder rows with real widget-backed views.
 */

import "./styles/design-system.css";
import "./styles/chassis.css";
import { bindSlider } from "./binding.js";
import { Knob } from "./widgets/knob.js";

function mount() {
  const canvas = document.getElementById("master-knob");
  const readout = document.getElementById("master-readout");

  // The relay name equals the apvts parameter ID (05-ui-ux.md "Parameter
  // binding" — naming contract). master_gain comes from Task 02.
  const binding = bindSlider("master_gain");

  new Knob(canvas, binding, { defaultNormalised: 0.8 });

  binding.onChange(() => {
    readout.textContent = binding.getScaled().toFixed(2);
  });
  readout.textContent = binding.getScaled().toFixed(2);

  // Signal the editor that the UI has mounted. JUCE injects window.__JUCE__
  // before any resource loads; check_native_interop.js provides a no-op
  // placeholder when the page is opened outside the plugin.
  window.__JUCE__.backend.emitEvent("uiReady", {});
}

if (document.readyState === "loading")
  window.addEventListener("DOMContentLoaded", mount);
else mount();
