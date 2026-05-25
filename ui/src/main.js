// v2 production entry. Mounts the chassis skeleton — the persistent
// header + an empty mode-panel host that Tasks 05-07 will fill with the
// FM / SQ / D panels. Also mounts the global notification-toast host and
// fires the `uiReady` event so the C++ editor knows the page is alive.
//
// Task 04 scope: prove the WebView pipe. The chassis frame uses the
// design-system.css recipes (chassis + inset surfaces); the mode panel is
// stubbed empty.

import "./styles/design-system.css";

import { mount as mountToast } from "./widgets/notification-toast.js";
import { installTooltips }     from "./widgets/tooltip.js";

function init() {
  // Notification toast — global, lives over the chassis.
  const toastHost = document.createElement("div");
  document.body.appendChild(toastHost);
  mountToast(toastHost);

  // Tooltips installed against the chassis frame. Once Tasks 05-07 wire up
  // their widgets, the same handler picks up every control inside.
  const frame = document.querySelector(".frame");
  if (frame) installTooltips(frame);

  // Signal mount. The C++ side logs it (PluginEditor::makeOptions).
  if (window.__JUCE__ && window.__JUCE__.backend) {
    window.__JUCE__.backend.emitEvent("uiReady", {});
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
