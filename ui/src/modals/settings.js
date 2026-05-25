// Settings modal — `08-ui-views.md` view 6.
//
// Controls (top to bottom):
//   - HARDWARE STRICT (FM)     — `hardware_strict` toggle
//   - UI SCALE                 — `ui_scale` 3-segment pill (1x / 2x / 3x)
//   - VELOCITY → TL (FM)       — `velocity_to_tl` toggle
//   - AFTERTOUCH               — `aftertouch_target` 3-segment pill
//                                  (Off / LFO depth / Carrier TL)
//   - TOOLTIPS                 — `tooltips_enabled` toggle (same param as
//                                  the header `TIPS` toggle — flipping
//                                  either flips both)
//   - ABOUT / CREDITS…         — replaces the Settings modal with About
//   - RESET ALL TO DEFAULTS    — destructive; confirmation modal first
//   - Close                    — dismiss

import {
  bindToggle,
  bindCombo,
} from "../binding.js";

import { mount as mountToggle } from "../widgets/toggle-switch.js";
import { applyTooltip }          from "../widgets/tooltip-content.js";

import { openModal, openConfirm } from "./modal-host.js";
import { open as openAbout }       from "./about.js";

import { getNativeFunction } from "../juce/index.js";

function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  return node;
}

function makeRow(label, control) {
  const row = el("div", { className: "modal-row" });
  row.appendChild(el("div", { className: "modal-label", text: label }));
  const wrap = el("div", { className: "modal-control" });
  wrap.appendChild(control);
  row.appendChild(wrap);
  return row;
}

function makePill(choices, currentIdx, onSelect, tipId) {
  const pill = el("div", { className: "btn-pill" });
  const btns = choices.map((label, idx) => {
    const btn = el("button", { className: "btn" });
    btn.type = "button";
    btn.textContent = label;
    btn.addEventListener("click", () => onSelect(idx));
    pill.appendChild(btn);
    return btn;
  });
  const refresh = (idx) => btns.forEach((b, i) => b.classList.toggle("is-active", i === idx));
  refresh(currentIdx);
  if (tipId) applyTooltip(pill, tipId);
  return { pill, refresh };
}

export function open() {
  return openModal({
    build: (close) => {
      const wrap = el("div");

      wrap.appendChild(el("div", {
        className: "modal-title",
        text: "SETTINGS",
      }));

      const closeX = el("button", { className: "modal-close", text: "X" });
      closeX.type = "button";
      closeX.addEventListener("click", () => close());
      wrap.appendChild(closeX);

      const body = el("div", { className: "modal-body" });

      // --- HARDWARE STRICT (FM) ----------------------------------------
      const strictHost = el("div");
      mountToggle(strictHost, {
        bind: bindToggle("hardware_strict"),
        tipId: "hardware_strict",
      });
      body.appendChild(makeRow("HARDWARE STRICT (FM)", strictHost));

      // --- UI SCALE ----------------------------------------------------
      const uiScaleCombo = bindCombo("ui_scale");
      const uiPill = makePill(
        ["1x", "2x", "3x"],
        uiScaleCombo.getIndex(),
        (idx) => uiScaleCombo.setIndex(idx),
        "ui_scale",
      );
      uiScaleCombo.onChange((idx) => uiPill.refresh(idx));
      body.appendChild(makeRow("UI SCALE", uiPill.pill));

      // --- VELOCITY → TL ----------------------------------------------
      const velHost = el("div");
      mountToggle(velHost, {
        bind: bindToggle("velocity_to_tl"),
        tipId: "velocity_to_tl",
      });
      body.appendChild(makeRow("VELOCITY → TL (FM)", velHost));

      // --- AFTERTOUCH --------------------------------------------------
      const atCombo = bindCombo("aftertouch_target");
      const atPill = makePill(
        ["Off", "LFO depth", "Carrier TL"],
        atCombo.getIndex(),
        (idx) => atCombo.setIndex(idx),
        "aftertouch_target",
      );
      atCombo.onChange((idx) => atPill.refresh(idx));
      body.appendChild(makeRow("AFTERTOUCH", atPill.pill));

      // --- TOOLTIPS ---------------------------------------------------
      const tipsHost = el("div");
      mountToggle(tipsHost, {
        bind: bindToggle("tooltips_enabled"),
        tipId: "tooltips_enabled",
      });
      body.appendChild(makeRow("TOOLTIPS", tipsHost));

      // --- Section: actions -------------------------------------------
      body.appendChild(el("div", {
        className: "modal-section-title",
        text: "Actions",
      }));

      const aboutBtn = el("button", { className: "btn", text: "ABOUT / CREDITS…" });
      aboutBtn.type = "button";
      aboutBtn.addEventListener("click", () => {
        // Replaces Settings — openModal closes the current one first.
        openAbout();
      });
      const aboutRow = el("div");
      aboutRow.appendChild(aboutBtn);
      body.appendChild(aboutRow);

      const resetBtn = el("button", {
        className: "btn modal-btn-destructive",
        text: "RESET ALL TO DEFAULTS",
      });
      resetBtn.type = "button";
      applyTooltip(resetBtn, "reset_all");
      resetBtn.addEventListener("click", () => {
        openConfirm({
          title: "RESET ALL TO DEFAULTS",
          message: "This will reset every parameter and clear the active patch path. Continue?",
          confirmLabel: "RESET",
          destructive: true,
          onConfirm: () => {
            let fn = null;
            try { fn = getNativeFunction("resetAllToDefaults"); } catch (e) { fn = null; }
            if (fn) fn();
          },
        });
      });
      const resetRow = el("div");
      resetRow.appendChild(resetBtn);
      body.appendChild(resetRow);

      wrap.appendChild(body);

      const footer = el("div", { className: "modal-footer" });
      const closeBtn = el("button", { className: "btn", text: "CLOSE" });
      closeBtn.type = "button";
      closeBtn.addEventListener("click", () => close());
      footer.appendChild(closeBtn);
      wrap.appendChild(footer);

      return wrap;
    },
  });
}
