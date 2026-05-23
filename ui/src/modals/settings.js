/*
 * Settings modal — 08-ui-views.md view 6.
 *
 * VOICE COUNT (8/12/16), PITCH BEND RANGE (±1/±2/±7/±12), UI SCALE (1x/2x/3x),
 * VELOCITY → TL toggle, AFTERTOUCH (Off/LFO depth/Carrier TL), plus two
 * buttons opening the MIDI ROUTING and ABOUT modals (which REPLACE this one
 * per shared modal behaviour).
 *
 * All settings are bound to apvts parameters. VOICE COUNT and UI SCALE become
 * functional in Tasks 15 and 17 respectively — wiring them here means those
 * later tasks only need to read the parameter values.
 */

import {
  SectionTabs, Toggle,
  bindCombo, bindToggle,
} from "../widgets/index.js";
import { openModal, confirmModal } from "./modal-host.js";
import { openMidiRoutingModal } from "./midi-routing.js";
import { openAboutModal }       from "./about.js";
import * as Juce from "../juce/index.js";

export function openSettingsModal() {
  openModal({
    title: "SETTINGS",
    width: 520,
    build: (body, ctx) => {
      const grid = document.createElement("div");
      grid.className = "settings-grid";
      body.appendChild(grid);

      addChoiceRow (grid, "VOICE COUNT",       "voice_count",        ["8","12","16"], 150);
      addChoiceRow (grid, "PITCH BEND RANGE",  "bend_range",         ["±1","±2","±7","±12"], 200);
      addChoiceRow (grid, "UI SCALE",          "ui_scale",           ["1x","2x","3x"], 130);
      addToggleRow (grid, "VELOCITY → TL",     "vel_to_tl");
      addChoiceRow (grid, "AFTERTOUCH",        "aftertouch_target",  ["OFF","LFO","TL"], 180);
      addToggleRow (grid, "TOOLTIPS",          "tooltips_enabled");

      // Separator + the two sub-modal buttons.
      const sep = document.createElement("div");
      sep.className = "settings-sep";
      body.appendChild(sep);

      const buttonRow = document.createElement("div");
      buttonRow.className = "settings-button-row";
      body.appendChild(buttonRow);

      const midiBtn = makeBigButton("MIDI ROUTING…");
      midiBtn.addEventListener("click", () => openMidiRoutingModal());
      buttonRow.appendChild(midiBtn);

      const aboutBtn = makeBigButton("ABOUT / CREDITS…");
      aboutBtn.addEventListener("click", () => openAboutModal());
      buttonRow.appendChild(aboutBtn);

      // RESET ALL — destructive: clears every patch + setting + routing back
      // to defaults. Confirmation modal first so a stray click can't undo a
      // long tweaking session.
      const resetRow = document.createElement("div");
      resetRow.className = "settings-button-row";
      const resetBtn = makeBigButton("RESET ALL TO DEFAULTS");
      resetBtn.classList.add("settings-button-destructive");
      resetBtn.addEventListener("click", () => {
        confirmModal({
          title: "RESET ALL",
          message: "This will reset every patch, setting and routing entry. Are you sure?",
          confirmLabel: "RESET ALL",
          onConfirm: async () => {
            const fn = Juce.getNativeFunction("resetAllToDefaults");
            try { await fn(); } catch (e) { console.error(e); }
          },
        });
      });
      resetRow.appendChild(resetBtn);
      body.appendChild(resetRow);

      // Footer: Close.
      const footer = document.createElement("div");
      footer.className = "modal-footer";
      const close = makeBigButton("CLOSE");
      close.addEventListener("click", () => ctx.close());
      footer.appendChild(close);
      body.appendChild(footer);
    },
  });
}

function addChoiceRow(grid, label, paramId, labels, width) {
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = label;
  grid.appendChild(lbl);

  const canvas = document.createElement("canvas");
  canvas.width = width; canvas.height = 14;
  grid.appendChild(canvas);
  new SectionTabs(canvas, bindCombo(paramId), {
    style: "pill", fontSize: 8, labels,
  });
}

function addToggleRow(grid, label, paramId) {
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = label;
  grid.appendChild(lbl);

  const canvas = document.createElement("canvas");
  canvas.width = 22; canvas.height = 14;
  grid.appendChild(canvas);
  new Toggle(canvas, bindToggle(paramId));
}

function makeBigButton(text) {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "settings-button bevel-raised label";
  btn.textContent = text;
  return btn;
}
