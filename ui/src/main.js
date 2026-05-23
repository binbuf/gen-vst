/*
 * Gen VST UI entry point.
 *
 * Mounts the FM view (08-ui-views.md view 1) inside the chassis declared by
 * index.html. The view is the plugin's primary screen — the SQ (PSG) and D
 * (DAC) sections land in Task 13.
 */

import "./styles/design-system.css";
import "./styles/chassis.css";
import { mountFmView } from "./views/fm-view.js";

function mount() {
  const chassis = document.getElementById("chassis");
  mountFmView(chassis);

  // Signal the editor that the UI has mounted. JUCE injects window.__JUCE__
  // before any resource loads; check_native_interop.js provides a no-op
  // placeholder when the page is opened outside the plugin.
  window.__JUCE__.backend.emitEvent("uiReady", { view: "fm" });
}

if (document.readyState === "loading")
  window.addEventListener("DOMContentLoaded", mount);
else mount();
