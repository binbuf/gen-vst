// D mode panel — `08-ui-views.md` view 4.
//
// PCM2612-inspired Retro Decimator panel, reduced to the two D-only controls:
//
//   - Large central DRY/WET knob (96 px `decimator-knob` body variant) bound
//     to the `dry_wet` apvts param.
//   - MONO toggle beneath it, bound to the `mono` apvts param.
//
// Everything else — DAC PRESCALER, output filter, ladder effect, L/R output
// meters — lives on the header (Task 08), so the panel itself stays spartan
// per view 4's "Deliberate divergences from PCM2612" notes. A stylised
// `RETRO DECIMATOR` wordmark fills the empty band above the knob; pure
// HTML/CSS, no custom font asset (IBM Plex Mono Bold per 09-visual-spec.md).
//
// No MIDI surface, no sample loader, no input-side meters — ADR-0021 / view 4.

import {
  bindSlider,
  bindToggle,
} from "../binding.js";

import { mount as mountDecimatorKnob } from "../widgets/decimator-knob.js";
import { mount as mountToggle }        from "../widgets/toggle-switch.js";

// D panel CSS scoped to .d-view. Same self-contained pattern as fm-view /
// sq-view — widget recipes (.knob.decimator-knob, .toggle, .t-wordmark) come
// from design-system.css; this sheet only positions the two controls.
function ensureStyles() {
  if (document.getElementById("genvst-d-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-d-view-style";
  style.textContent = `
    .d-view {
      width: 100%;
      height: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .d-view .d-stack {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 18px;
    }
    .d-view .d-wordmark {
      margin-bottom: 6px;
      opacity: 0.85;
    }
    .d-view .knob-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
    }
    .d-view .knob-cell .knob-label {
      font: 500 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.22em;
      text-transform: uppercase;
      color: var(--label-text);
    }
    .d-view .toggle-cell {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
    }
    .d-view .toggle-cell .toggle-label {
      font: 500 9px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.22em;
      text-transform: uppercase;
      color: var(--label-text);
    }
  `;
  document.head.appendChild(style);
}

function el(tag, opts = {}) {
  const node = document.createElement(tag);
  if (opts.className) node.className = opts.className;
  if (opts.text)      node.textContent = opts.text;
  if (opts.children)  for (const c of opts.children) node.appendChild(c);
  return node;
}

// Build & mount the D panel into `root`. Returns a disposer that clears the
// host; main.js calls it on mode change.
export function mount(root) {
  ensureStyles();
  root.classList.add("d-view");
  root.innerHTML = "";

  const stack = el("div", { className: "d-stack" });

  // Wordmark — fills the empty band above the central knob (view 4 layout
  // note). t-wordmark token from design-system.css provides the IBM Plex
  // Mono Bold treatment.
  stack.appendChild(el("div", {
    className: "d-wordmark t-wordmark",
    text: "RETRO DECIMATOR",
  }));

  // DRY / WET — the panel's centerpiece. 96 px decimator-knob body so the
  // panel keeps the prominent central anchor PCM2612 made iconic, even though
  // the parameter binding moved from prescaler (v1) to dry_wet (v2).
  const knobCell = el("div", { className: "knob-cell" });
  const knobHost = el("div");
  knobCell.appendChild(knobHost);
  knobCell.appendChild(el("div", { className: "knob-label", text: "DRY / WET" }));
  mountDecimatorKnob(knobHost, {
    bind: bindSlider("dry_wet"),
    tipId: "dry_wet",
  });
  stack.appendChild(knobCell);

  // MONO — single 28 px toggle, lit when on. Bound to the global `mono` param
  // that PluginProcessor reads after the D-mode decimator runs (see
  // processBlock's MONO collapse step).
  const toggleCell = el("div", { className: "toggle-cell" });
  const toggleHost = el("div");
  toggleCell.appendChild(toggleHost);
  toggleCell.appendChild(el("div", { className: "toggle-label", text: "MONO" }));
  mountToggle(toggleHost, {
    bind: bindToggle("mono"),
    tipId: "mono",
  });
  stack.appendChild(toggleCell);

  root.appendChild(stack);

  return {
    dispose() {
      root.classList.remove("d-view");
      root.innerHTML = "";
    },
  };
}
