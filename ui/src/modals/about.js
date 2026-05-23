/*
 * About / credits modal — 08-ui-views.md view 7.
 *
 * Version + the license attributions. Required because the project is
 * GPL v3 with bundled third-party code. The attribution list must stay in
 * sync with the Legal Notes table in 04-patch-system.md and ADRs 0003/0004.
 */

import { openModal } from "./modal-host.js";

const VERSION = "v0.1.0";

const ATTRIBUTIONS = [
  { name: "ymfm",                license: "BSD-3-Clause", desc: "(YM2612 core)" },
  { name: "libvgm sn764xx",      license: "LGPL",         desc: "(SN76489 core)" },
  { name: "JUCE 8",              license: "GPL v3",       desc: "" },
  { name: "Furnace tfilib",      license: "GPL",          desc: "(factory patch bank)" },
  { name: "Press Start 2P",      license: "SIL OFL",      desc: "(label font)" },
];

export function openAboutModal() {
  openModal({
    title: "ABOUT",
    width: 540,
    build: (body, ctx) => {
      const head = document.createElement("div");
      head.className = "about-head";
      const wm = document.createElement("span");
      wm.className = "label about-wordmark";
      wm.textContent = "GEN VST";
      const ver = document.createElement("span");
      ver.className = "label about-version";
      ver.textContent = VERSION;
      head.appendChild(wm);
      head.appendChild(ver);
      body.appendChild(head);

      const tagline = document.createElement("p");
      tagline.className = "label about-tagline";
      tagline.textContent = "Sega Genesis YM2612 + SN76489 emulation";
      body.appendChild(tagline);

      const license = document.createElement("p");
      license.className = "label about-tagline";
      license.textContent = "Gen VST is free software under the GNU GPL v3.";
      body.appendChild(license);

      const table = document.createElement("div");
      table.className = "about-attributions";
      for (const a of ATTRIBUTIONS) {
        const row = document.createElement("div");
        row.className = "about-row";

        const name = document.createElement("span");
        name.className = "label";
        name.textContent = a.name;
        const lic = document.createElement("span");
        lic.className = "label";
        lic.textContent = a.license;
        const desc = document.createElement("span");
        desc.className = "label about-desc";
        desc.textContent = a.desc;

        row.appendChild(name);
        row.appendChild(lic);
        row.appendChild(desc);
        table.appendChild(row);
      }
      body.appendChild(table);

      const footer = document.createElement("div");
      footer.className = "modal-footer";
      const close = document.createElement("button");
      close.type = "button";
      close.className = "settings-button bevel-raised label";
      close.textContent = "CLOSE";
      close.addEventListener("click", () => ctx.close());
      footer.appendChild(close);
      body.appendChild(footer);
    },
  });
}
