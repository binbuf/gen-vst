// v2 production entry. Mounts the persistent header + an empty mode-panel
// host that Tasks 05-07 fill with the FM / SQ / D panels. Also mounts the
// global notification-toast host and fires the `uiReady` event so the C++
// editor knows the page is alive.
//
// Header (Task 08) carries the mode pill, patch-name LCD, output filter /
// ladder toggles, stacked L/R meters, DAC PRESC + VOL + TIPS, and the gear /
// wordmark click handlers that open the Settings / About modals.
//
// Mode dispatch: subscribes to `mode_select` and swaps the panel contents.

import "./styles/design-system.css";

import { mount as mountToast } from "./widgets/notification-toast.js";
import { installTooltips }     from "./widgets/tooltip.js";
import { bindCombo }            from "./binding.js";
import { mount as mountHeader } from "./header.js";
import { mount as mountFmView } from "./views/fm-view.js";
import { mount as mountSqView } from "./views/sq-view.js";
import { mount as mountDView }  from "./views/d-view.js";
import { open as openSettings }      from "./modals/settings.js";
import { open as openAbout }         from "./modals/about.js";
import { open as openPresetBrowser } from "./modals/preset-browser.js";

const MODE_FM = 0;
const MODE_SQ = 1;
const MODE_D  = 2;

function init() {
  try {
    // Notification toast — global, lives over the chassis (and above modals,
    // per 08-ui-views.md *Modal behaviour (shared)*; the toast host's
    // z-index 100 > modal-host z-index 80).
    const toastHost = document.createElement("div");
    document.body.appendChild(toastHost);
    mountToast(toastHost);

    // Tooltips installed against the chassis frame. The handler picks up
    // every interactive control in the header + per-mode panels.
    const frame = document.querySelector(".frame");
    if (frame) installTooltips(frame);

    // Persistent header — Task 08. Wires gear → Settings, wordmark → About,
    // and the 📂 button to the unified preset browser modal (Task 09).
    const headerHost = document.querySelector(".hdr");
    if (headerHost) {
      mountHeader(headerHost, {
        onOpenSettings: () => openSettings(),
        onOpenAbout:    () => openAbout(),
        onOpenBrowser:  () => openPresetBrowser(),
      });
    }

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
      window.__JUCE__.backend.emitEvent("uiReady", { ok: true });
    }
  } catch (err) {
    // Permanent guard: a thrown mount must never leave the editor blank
    // without a visible error trail. Surface it three ways so any
    // diagnostic channel works:
    //   1. console.error — visible via WebView devtools.
    //   2. uiReady payload — PluginEditor.cpp logs it via juce::Logger.
    //   3. On-page overlay — visible in the editor with zero tooling.
    const message = (err && err.message) ? String(err.message) : String(err);
    const stack   = (err && err.stack)   ? String(err.stack)   : "";
    console.error("Gen VST init failed:", err);
    if (window.__JUCE__ && window.__JUCE__.backend) {
      window.__JUCE__.backend.emitEvent("uiReady", {
        ok: false,
        error: message,
        stack,
      });
    }
    renderInitError(message, stack);
    throw err;
  }
}

function renderInitError(message, stack) {
  // On-page overlay so a mount failure is impossible to miss. Inline styles
  // so design-system.css being unavailable can't suppress this.
  const root = document.querySelector(".frame") || document.body;
  const box  = document.createElement("div");
  box.setAttribute("data-genvst-init-error", "1");
  box.style.cssText = [
    "position:absolute","inset:8px","z-index:9999","overflow:auto",
    "background:#1a1f2a","color:#ff8a8a","border:1px solid #ff5252",
    "border-radius:4px","padding:10px 14px",
    "font:500 12px/1.4 ui-monospace, 'IBM Plex Mono', monospace",
    "white-space:pre-wrap","word-break:break-word",
  ].join(";");
  box.textContent = "Gen VST — init failed\n\n" + message + "\n\n" + stack;
  root.appendChild(box);
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
