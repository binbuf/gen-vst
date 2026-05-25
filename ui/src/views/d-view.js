// D mode panel — `08-ui-views.md` view 4.
//
// PCM2612-inspired Retro Decimator panel. Layout + CSS ported verbatim
// from the mvp2/01 mockup (deleted in task 04 per the mockup's own
// disposal note): see `ui/mockup-d.html` and `ui/mockup/mockup-d.css`
// at commit 2120d65 for the throwaway source-of-truth.
//
//   - Centered `RETRO DECIMATOR` wordmark band
//   - Large central DRY/WET decimator-knob bound to `dry_wet`
//   - MONO toggle beneath, bound to `mono`
//   - Vertical brushed-metal "wing" decorations flanking the column
//     (CSS pseudo-elements; pure decoration so the wide empty regions
//     don't read as dead space — view 4 *Layout note*).
//
// Header chrome (DAC PRESCALER, filter, ladder, meters) lives outside
// the panel per task 08. No MIDI surface, no sample loader (ADR-0021).

import {
  bindSlider,
  bindToggle,
} from "../binding.js";

import { mount as mountDecimatorKnob } from "../widgets/decimator-knob.js";
import { mount as mountToggle }        from "../widgets/toggle-switch.js";

// D panel CSS — ported verbatim from mvp2/01 mockup-d.css. Widget
// recipes (.knob.decimator-knob, .toggle, .t-wordmark) come from
// design-system.css; this sheet only positions the two controls and
// adds the "wing" chrome.
function ensureStyles() {
  if (document.getElementById("genvst-d-view-style")) return;
  const style = document.createElement("style");
  style.id = "genvst-d-view-style";
  style.textContent = `
    .d-panel {
      width: 100%;
      height: 100%;
      display: grid;
      grid-template-rows: auto 1fr auto 1fr;
      align-items: center;
      justify-items: center;
      padding: 36px 14px 14px;
      gap: 14px;
      position: relative;
    }

    .d-panel .d-wordmark {
      font: 700 14px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.35em;
      text-transform: uppercase;
      color: var(--label-text);
      opacity: 0.7;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.4);
    }

    .d-panel .d-drywet {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 10px;
    }
    .d-panel .d-drywet-label {
      font: 500 12px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.30em;
      text-transform: uppercase;
      color: var(--label-text);
    }

    .d-panel .d-mono {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
    }
    .d-panel .d-mono .t-label {
      font: 500 10px/1 "IBM Plex Mono", monospace;
      letter-spacing: 0.30em;
      text-transform: uppercase;
      color: var(--label-text);
    }

    /* "Wing" decorations — vertical brushed-metal bands flanking the
     * centered column. Pure decoration; PCM2612's portrait artwork
     * doesn't have this issue but our 1200x560 landscape canvas does
     * (08-ui-views.md §4 *Layout note*). */
    .d-panel::before,
    .d-panel::after {
      content: "";
      position: absolute;
      top: 12px;
      bottom: 12px;
      width: 120px;
      border-radius: 3px;
      background:
        repeating-linear-gradient(
          90deg,
          rgba(255, 255, 255, 0.04) 0 1px,
          transparent 1px 4px
        ),
        linear-gradient(180deg,
          rgba(255, 255, 255, 0.06) 0%,
          rgba(0, 0, 0, 0.25) 100%);
      box-shadow:
        inset 1px 1px 0 rgba(255, 255, 255, 0.08),
        inset -1px -1px 0 rgba(0, 0, 0, 0.45);
      pointer-events: none;
    }
    .d-panel::before { left: 24px; }
    .d-panel::after  { right: 24px; }
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

// Build & mount the D panel into `root`. Returns a disposer.
export function mount(root) {
  ensureStyles();
  root.classList.add("d-panel");
  root.innerHTML = "";

  // RETRO DECIMATOR wordmark band (top, single row of the 4-row grid).
  root.appendChild(el("div", {
    className: "d-wordmark",
    text: "RETRO DECIMATOR",
  }));

  // DRY / WET — centerpiece, large decimator-knob with caption below.
  const drywet = el("div", { className: "d-drywet" });
  const knobHost = el("div");
  drywet.appendChild(knobHost);
  drywet.appendChild(el("div", { className: "d-drywet-label", text: "DRY / WET" }));
  mountDecimatorKnob(knobHost, {
    bind: bindSlider("dry_wet"),
    tipId: "dry_wet",
  });
  root.appendChild(drywet);

  // MONO — single toggle, lit when on.
  const mono = el("div", { className: "d-mono" });
  const toggleHost = el("div");
  mono.appendChild(toggleHost);
  mono.appendChild(el("div", { className: "t-label", text: "MONO" }));
  mountToggle(toggleHost, {
    bind: bindToggle("mono"),
    tipId: "mono",
  });
  root.appendChild(mono);

  return {
    dispose() {
      root.classList.remove("d-panel");
      root.innerHTML = "";
    },
  };
}
