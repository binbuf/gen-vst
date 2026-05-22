/*
 * Gen VST UI entry point.
 *
 * Loads the design-system CSS, binds the one working control (the master-gain
 * knob) through a JUCE WebSliderRelay, and fires the `uiReady` event the editor
 * listens for once the page is mounted (05-ui-ux.md "Editor host").
 */

import "./styles/design-system.css";
import "./styles/chassis.css";
import * as Juce from "./juce/index.js";
import { Knob } from "./knob.js";

function mount() {
  const canvas = document.getElementById("master-knob");
  const readout = document.getElementById("master-readout");

  // The relay name equals the apvts parameter ID (05-ui-ux.md "Parameter
  // binding" — naming contract). master_gain comes from Task 02.
  const masterGain = Juce.getSliderState("master_gain");

  new Knob(canvas, masterGain, {
    // master_gain's apvts default is 0.8 (Task 02). Task 10's widget library
    // will source the reset value from the relay's properties instead.
    resetValue: 0.8,
    onChange: (v) => {
      readout.textContent = v.toFixed(2);
    },
  });

  // Signal the editor that the UI has mounted. JUCE injects window.__JUCE__
  // before any resource loads; check_native_interop.js provides a no-op
  // placeholder when the page is opened outside the plugin.
  window.__JUCE__.backend.emitEvent("uiReady", {});
}

if (document.readyState === "loading")
  window.addEventListener("DOMContentLoaded", mount);
else mount();
