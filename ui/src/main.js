/*
 * Gen VST UI entry point.
 *
 * Mounts the main views (FM/SQ/D), the modal sub-system (Settings, MIDI
 * routing, About; the patch browser is Task 14), and the notification toast.
 * The gear-icon header button opens the Settings modal; the body's
 * data-section attribute drives which bottom-region view is visible.
 */

import "./styles/design-system.css";
import "./styles/chassis.css";
import "./styles/sections.css";
import "./styles/modals.css";
import { mountFmView }    from "./views/fm-view.js";
import { mountSqView }    from "./views/sq-view.js";
import { mountDView }     from "./views/d-view.js";
import { openSettingsModal } from "./modals/settings.js";
import { NotificationToastHost } from "./widgets/notification-toast.js";
import { installTooltips } from "./widgets/tooltip.js";

function mount() {
  const chassis = document.getElementById("chassis");

  // The FM bottom-region content lives in #bottom (kept for Task 11 layout
  // compatibility). The SQ and D views mount into sibling containers in
  // #bottom-host and the body's [data-section] attribute toggles visibility
  // via CSS — see sections.css.
  mountFmView(chassis);
  const sqHost = document.getElementById("bottom-sq");
  const dHost  = document.getElementById("bottom-d");
  mountSqView(sqHost);
  mountDView (dHost);
  document.body.dataset.section = "FM";

  // Gear icon -> Settings modal.
  const gear = chassis.querySelector("#gear");
  if (gear) {
    gear.style.cursor = "pointer";
    gear.addEventListener("click", () => openSettingsModal());
  }

  // Notification toast host — sits outside the chassis so toast slides over
  // the chassis and over any modal (modals.css gives the toast container the
  // highest z-index).
  const toastHost = document.createElement("div");
  toastHost.id = "toast-host";
  document.body.appendChild(toastHost);
  const toast = new NotificationToastHost(toastHost);

  // C++ -> JS notification channel (05-ui-ux.md "C++ -> JS notifications").
  window.__JUCE__.backend.addEventListener("notify", (event) => {
    if (!event) return;
    toast.push({
      level:   event.level   ?? "info",
      message: event.message ?? "",
    });
  });

  // Global hover tooltips — listens for elements with data-tip attributes.
  // The toggle in Settings flips the tooltips_enabled apvts boolean, which
  // this module subscribes to.
  installTooltips();

  // Signal the editor that the UI has mounted. JUCE injects window.__JUCE__
  // before any resource loads; check_native_interop.js provides a no-op
  // placeholder when the page is opened outside the plugin.
  window.__JUCE__.backend.emitEvent("uiReady", { view: "fm" });
}

if (document.readyState === "loading")
  window.addEventListener("DOMContentLoaded", mount);
else mount();
