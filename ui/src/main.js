// v2 production entry. Mounts the chassis skeleton — the persistent
// header + an empty mode-panel host that Tasks 05-07 fill with the
// FM / SQ / D panels. Also mounts the global notification-toast host and
// fires the `uiReady` event so the C++ editor knows the page is alive.
//
// Mode dispatch: subscribes to `mode_select` and swaps the panel contents.
// FM (Task 05), SQ (Task 06), and D (Task 07) views are all wired.

import "./styles/design-system.css";

import { mount as mountToast } from "./widgets/notification-toast.js";
import { installTooltips }     from "./widgets/tooltip.js";
import { bindCombo }            from "./binding.js";
import { mount as mountFmView } from "./views/fm-view.js";
import { mount as mountSqView } from "./views/sq-view.js";
import { mount as mountDView }  from "./views/d-view.js";

const MODE_FM = 0;
const MODE_SQ = 1;
const MODE_D  = 2;

function init() {
  // Notification toast — global, lives over the chassis.
  const toastHost = document.createElement("div");
  document.body.appendChild(toastHost);
  mountToast(toastHost);

  // Tooltips installed against the chassis frame. Once Tasks 05-07 wire up
  // their widgets, the same handler picks up every control inside.
  const frame = document.querySelector(".frame");
  if (frame) installTooltips(frame);

  // Mount the active mode panel into #mode-panel; subscribe to mode_select
  // and swap the panel contents on every change. Each mount returns a
  // disposer so the previous view's listeners are cleanly removed.
  const modePanel = document.getElementById("mode-panel");
  if (modePanel) {
    const modeBind = bindCombo("mode_select");
    let currentDisposer = null;
    const mountForMode = (idx) => {
      if (currentDisposer) { currentDisposer.dispose(); currentDisposer = null; }
      modePanel.innerHTML = "";
      switch (idx) {
        case MODE_FM:
          currentDisposer = mountFmView(modePanel);
          break;
        case MODE_SQ:
          currentDisposer = mountSqView(modePanel);
          break;
        case MODE_D:
          currentDisposer = mountDView(modePanel);
          break;
        default:
          modePanel.appendChild(makePlaceholder("?"));
      }
    };
    modeBind.onChange(mountForMode);
  }

  // Signal mount. The C++ side logs it (PluginEditor::makeOptions).
  if (window.__JUCE__ && window.__JUCE__.backend) {
    window.__JUCE__.backend.emitEvent("uiReady", {});
  }
}

function makePlaceholder(label) {
  const div = document.createElement("div");
  div.style.display = "flex";
  div.style.alignItems = "center";
  div.style.justifyContent = "center";
  div.style.width = "100%";
  div.style.height = "100%";
  div.style.opacity = "0.4";
  div.style.font = "500 14px/1 'IBM Plex Mono', monospace";
  div.style.letterSpacing = "0.2em";
  div.style.textTransform = "uppercase";
  div.style.color = "var(--label-text)";
  div.textContent = `${label} mode panel — coming soon`;
  return div;
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
