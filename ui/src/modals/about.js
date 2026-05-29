// About modal — `08-ui-views.md` view 7.
//
// Static content. The version string lives here only (the v2 first-pass
// bottom status bar that previously surfaced it was removed during the
// post-mockup review). The license attribution table mirrors the
// *Legal Notes* section of `04-patch-system.md` — kept in sync by hand.
//
// Entry points: clicking the GEN VST wordmark in the header, or clicking
// ABOUT / CREDITS… in the Settings modal (the latter replaces Settings —
// modal-host.js closes the previous modal before opening the new one).

import { openModal } from "./modal-host.js";

// Pulled from a Vite `define` (configured at build time in vite.config.js,
// see comment below) — falls back to a placeholder so the modal still
// renders cleanly if the define is missing.
const VERSION = (typeof __GENVST_VERSION__ !== "undefined")
  ? __GENVST_VERSION__
  : "0.3.0";

const SOURCE_URL = (typeof __GENVST_SOURCE_URL__ !== "undefined")
  ? __GENVST_SOURCE_URL__
  : "https://github.com/binbuf/gen-vst";

const ATTRIBUTIONS = [
  ["ymfm library",                       "BSD-3-Clause",
   "Compatible with GPL project"],
  ["JUCE",                               "GPL v3",
   "Plugin must be GPL v3"],
  ["Furnace tfilib preset data",         "GPL",
   "Shipped as the factory FM bank — include Furnace attribution"],
  ["Factory .psg presets (extern/patches/sq/)",
   "Original works by project authors",
   "No external attribution required"],
  ["Game-derived .tfi library",
   "Derivative of copyrighted game audio",
   "Not shipped, not committed — local developer test material only"],
  ["DMP PSG community presets (user-imported)",
   "Varies per file",
   "Import-only path; not bundled; user responsibility"],
  ["IBM Plex Mono (typeface)",
   "SIL OFL 1.1",
   "Bundled binary font asset (extern/fonts/ibm-plex-mono)"],
];

function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  return node;
}

function ensureStyles() {
  if (document.getElementById("genvst-about-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-about-style";
  style.textContent = `
    .about-modal .about-heading {
      display: flex;
      align-items: baseline;
      gap: 10px;
      margin-bottom: 4px;
    }
    .about-modal .about-heading .about-wordmark {
      font: 700 18px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.15em;
      text-transform: uppercase;
      color: var(--brand-mark);
    }
    .about-modal .about-heading .about-version {
      font: 500 11px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.10em;
      color: var(--text-on-chassis);
      opacity: 0.85;
    }
    .about-modal .about-tagline {
      font: 400 11px/1.4 "IBM Plex Mono", monospace;
      color: var(--text-on-chassis);
      opacity: 0.85;
      margin-bottom: 12px;
    }
    .about-modal .about-gpl {
      font: 500 10px/1.4 "IBM Plex Mono", monospace;
      letter-spacing: 0.06em;
      color: var(--text-on-chassis);
      margin-bottom: 12px;
    }
    .about-modal .about-table {
      width: 100%;
      border-collapse: collapse;
      font: 400 10px/1.3 "IBM Plex Mono", monospace;
      color: var(--text-on-chassis);
      margin-bottom: 12px;
    }
    .about-modal .about-table th {
      text-align: left;
      font-weight: 500;
      letter-spacing: 0.10em;
      text-transform: uppercase;
      font-size: 9px;
      color: var(--label-text-dim);
      border-bottom: 1px solid rgba(0, 0, 0, 0.30);
      padding: 4px 6px;
    }
    .about-modal .about-table td {
      vertical-align: top;
      padding: 4px 6px;
      border-bottom: 1px solid rgba(0, 0, 0, 0.10);
    }
    .about-modal .about-source {
      font: 400 10px/1.3 "IBM Plex Mono", monospace;
      color: var(--text-on-chassis);
      margin-top: 4px;
    }
    .about-modal .about-source a {
      color: var(--accent-info);
      text-decoration: none;
    }
  `;
  document.head.appendChild(style);
}

export function open() {
  ensureStyles();
  return openModal({
    build: (close) => {
      const wrap = el("div", { className: "about-modal" });

      wrap.appendChild(el("div", {
        className: "modal-title",
        text: "ABOUT",
      }));

      const closeX = el("button", { className: "modal-close", text: "X" });
      closeX.type = "button";
      closeX.addEventListener("click", () => close());
      wrap.appendChild(closeX);

      const body = el("div", { className: "modal-body" });

      const heading = el("div", { className: "about-heading" });
      heading.appendChild(el("span", { className: "about-wordmark", text: "GEN VST" }));
      heading.appendChild(el("span", { className: "about-version", text: "v" + VERSION }));
      body.appendChild(heading);

      body.appendChild(el("div", {
        className: "about-tagline",
        text: "Sega Genesis YM2612 + SN76489 emulation",
      }));

      body.appendChild(el("div", {
        className: "about-gpl",
        text: "Gen VST is free software under the GNU GPL v3.",
      }));

      const table = el("table", { className: "about-table" });
      const thead = el("thead");
      const headRow = el("tr");
      ["Asset", "License", "Note"].forEach((h) => {
        headRow.appendChild(el("th", { text: h }));
      });
      thead.appendChild(headRow);
      table.appendChild(thead);
      const tbody = el("tbody");
      for (const [asset, license, note] of ATTRIBUTIONS) {
        const row = el("tr");
        row.appendChild(el("td", { text: asset }));
        row.appendChild(el("td", { text: license }));
        row.appendChild(el("td", { text: note }));
        tbody.appendChild(row);
      }
      table.appendChild(tbody);
      body.appendChild(table);

      const sourceRow = el("div", { className: "about-source" });
      sourceRow.appendChild(document.createTextNode("Source: "));
      const link = el("a", { text: SOURCE_URL });
      link.href = SOURCE_URL;
      link.target = "_blank";
      link.rel = "noopener noreferrer";
      sourceRow.appendChild(link);
      body.appendChild(sourceRow);

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
