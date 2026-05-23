/*
 * MIDI routing editor — 08-ui-views.md view 5.
 *
 * One row per destination (6 FM parts, 3 PSG tone slots, PSG noise, DAC),
 * each with a MIDI-channel selector (0 = Off / 1..16). Conflict highlighting:
 * if two destinations share a channel, both rows are flagged and a warning
 * line is shown. `Reset to defaults` restores FM 1..6, PSG 11..13, DAC 16.
 *
 * The table is the same MidiRouter data the inline step-fields on views 1/2/3
 * edit — they share the routing-controls.js state via setChannelForDestination,
 * so flipping a value here is immediately visible in those step-fields.
 */

import { openModal } from "./modal-host.js";
import {
  fetchRouting,
  listAllDestinations,
  setChannelForDestination,
  resetRoutingToDefaults,
  conflictingChannels,
  onRoutingChange,
} from "../views/routing-controls.js";

const CHOICES = ["OFF", "1", "2", "3", "4", "5", "6", "7", "8",
                 "9", "10", "11", "12", "13", "14", "15", "16"];

export function openMidiRoutingModal() {
  openModal({
    title: "MIDI ROUTING",
    width: 520,
    build: (body, ctx) => {
      const table = document.createElement("div");
      table.className = "routing-table";
      body.appendChild(table);

      // Header row.
      const head = document.createElement("div");
      head.className = "routing-head";
      const hLeft = document.createElement("span");
      hLeft.className = "label";
      hLeft.textContent = "DESTINATION";
      const hRight = document.createElement("span");
      hRight.className = "label";
      hRight.textContent = "MIDI CHANNEL";
      head.appendChild(hLeft);
      head.appendChild(hRight);
      table.appendChild(head);

      const rowsHost = document.createElement("div");
      rowsHost.className = "routing-rows";
      table.appendChild(rowsHost);

      const warning = document.createElement("div");
      warning.className = "routing-warning label";
      body.appendChild(warning);

      const footer = document.createElement("div");
      footer.className = "modal-footer routing-footer";
      body.appendChild(footer);

      const resetBtn = button("RESET TO DEFAULTS");
      resetBtn.addEventListener("click", () => resetRoutingToDefaults());
      footer.appendChild(resetBtn);

      const closeBtn = button("CLOSE");
      closeBtn.addEventListener("click", () => ctx.close());
      footer.appendChild(closeBtn);

      const rebuild = () => renderRows(rowsHost, warning);
      const unsubscribe = onRoutingChange(rebuild);

      fetchRouting().then(() => rebuild());
      rebuild();

      const closeOrig = ctx.close;
      ctx.close = (...args) => {
        unsubscribe?.();
        closeOrig.apply(ctx, args);
      };
    },
  });
}

function button(text) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "settings-button bevel-raised label";
  b.textContent = text;
  return b;
}

function renderRows(rowsHost, warning) {
  rowsHost.innerHTML = "";

  const dests = listAllDestinations();
  const conflicts = conflictingChannels();

  for (const dest of dests) {
    const row = document.createElement("div");
    row.className = "routing-row";
    if (conflicts.has(dest.channel)) row.classList.add("routing-row-conflict");

    const lbl = document.createElement("span");
    lbl.className = "label";
    lbl.textContent = dest.label;
    row.appendChild(lbl);

    const select = document.createElement("select");
    select.className = "routing-select label";
    for (let i = 0; i < CHOICES.length; ++i) {
      const opt = document.createElement("option");
      opt.value = String(i);
      opt.textContent = CHOICES[i];
      select.appendChild(opt);
    }
    select.value = String(dest.channel);
    select.addEventListener("change", () => {
      setChannelForDestination(dest, parseInt(select.value, 10));
    });
    row.appendChild(select);

    rowsHost.appendChild(row);
  }

  if (conflicts.size > 0) {
    const list = Array.from(conflicts).sort((a, b) => a - b).join(", ");
    warning.textContent = `⚠ Channel ${list} assigned to multiple destinations.`;
    warning.classList.add("active");
  } else {
    warning.textContent = "";
    warning.classList.remove("active");
  }
}
