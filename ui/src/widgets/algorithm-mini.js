// `algorithm-mini` widget — SVG read-only diagram of the currently selected
// YM2612 algorithm's operator topology.
//
// Eight hard-coded topologies, each rendered in the same SVG recipe as the
// approved mockup at commit 2120d65 (`ui/mockup/mockup-fm.css` *Algorithm
// diagram tile* + `ui/mockup-fm.html` *fm-algo-diagram*):
//
//   - 112×112 viewBox inside the LCD-styled host
//   - 20×20 operator rectangles, 1.4px stroke in --lcd-text-on
//   - drop-shadow(0 0 2px --lcd-text-glow) phosphor bloom on every stroke
//   - IBM Plex Mono 9px 500-weight blue numeric label centred in each box
//   - Output arrow exits each carrier rightward to the right edge of the SVG
//
// Carriers cross-check against `kCarrierMaskForAlg` in
// src/PluginProcessor.cpp:153-162 (the C++ side is the source of truth for
// who's a carrier — that's what the chip actually hears).
//
// setAlgorithm(idx) repaints; no click handler (selection picker is `algo-grid`).

import { applyTooltip } from "./tooltip-content.js";

const SVG_NS = "http://www.w3.org/2000/svg";

// Box and viewBox geometry. Keep these as named constants so the line / arrow
// math below stays readable.
const VIEW = 112;
const BOX  = 20;
const RIGHT_EDGE = 104;   // where the output arrow terminates

// YM2612 algorithm topologies (0..7). The indices match
// kCarrierMaskForAlg in PluginProcessor.cpp; the label users see is
// `ALG (idx+1)` per the mockup.
//
// Each entry:
//   boxes:   [{op, x, y}, ...]  — top-left coords of each 20×20 operator box
//   lines:   [[fromOp, toOp], ...]  — modulator → modulated, edge-to-edge
//   outputs: [op, ...]  — carriers that get an output arrow
//
// Layouts mirror the mockup's visual language: 2-column grid (left x=14,
// right x=56) with rows roughly at y=14 / y=46 / y=78. Algorithms that
// don't fit cleanly get bespoke positions (the linear chain, the centred
// fan-out) — readability trumps grid orthodoxy.
const ALGOS = [
  // ALG 0 (label 1): 1→2→3→4 (linear chain), carrier=4.
  {
    boxes: [
      { op: 1, x: 46, y:  6 },
      { op: 2, x: 46, y: 34 },
      { op: 3, x: 46, y: 62 },
      { op: 4, x: 46, y: 90 },
    ],
    lines: [[1, 2], [2, 3], [3, 4]],
    outputs: [4],
  },
  // ALG 1 (label 2): (1+2)→3→4, carrier=4.
  {
    boxes: [
      { op: 1, x: 14, y: 10 },
      { op: 2, x: 56, y: 10 },
      { op: 3, x: 35, y: 44 },
      { op: 4, x: 35, y: 78 },
    ],
    lines: [[1, 3], [2, 3], [3, 4]],
    outputs: [4],
  },
  // ALG 2 (label 3): 1→4, 2→3→4, carrier=4.
  {
    boxes: [
      { op: 1, x: 14, y: 10 },
      { op: 2, x: 56, y: 10 },
      { op: 3, x: 56, y: 44 },
      { op: 4, x: 35, y: 78 },
    ],
    lines: [[1, 4], [2, 3], [3, 4]],
    outputs: [4],
  },
  // ALG 3 (label 4): (S1→S2)+S3→S4. S1 modulates S2, then S2 and S3 both
  // modulate the carrier S4. Matches docs/design/02-fm-synthesis.md
  // "FM Algorithms" row 3.
  {
    boxes: [
      { op: 1, x: 14, y: 14 },
      { op: 2, x: 14, y: 46 },
      { op: 3, x: 14, y: 78 },
      { op: 4, x: 56, y: 46 },
    ],
    lines: [[1, 2], [2, 4], [3, 4]],
    outputs: [4],
  },
  // ALG 4 (label 5): 1→2, 3→4 (two parallel chains), carriers=2,4.
  {
    boxes: [
      { op: 1, x: 14, y: 14 },
      { op: 2, x: 14, y: 62 },
      { op: 3, x: 56, y: 14 },
      { op: 4, x: 56, y: 62 },
    ],
    lines: [[1, 2], [3, 4]],
    outputs: [2, 4],
  },
  // ALG 5 (label 6): 1→{2,3,4}, carriers=2,3,4.
  // Op 1 on the left fanning out to three carriers on the right.
  {
    boxes: [
      { op: 1, x: 14, y: 46 },
      { op: 2, x: 56, y: 14 },
      { op: 3, x: 56, y: 46 },
      { op: 4, x: 56, y: 78 },
    ],
    lines: [[1, 2], [1, 3], [1, 4]],
    outputs: [2, 3, 4],
  },
  // ALG 6 (label 7): 1→2, ops 3 & 4 untouched, carriers=2,3,4.
  {
    boxes: [
      { op: 1, x: 14, y: 30 },
      { op: 2, x: 56, y: 14 },
      { op: 3, x: 56, y: 46 },
      { op: 4, x: 56, y: 78 },
    ],
    lines: [[1, 2]],
    outputs: [2, 3, 4],
  },
  // ALG 7 (label 8): four independent carriers, no modulation.
  {
    boxes: [
      { op: 1, x: 14, y: 14 },
      { op: 2, x: 56, y: 14 },
      { op: 3, x: 14, y: 62 },
      { op: 4, x: 56, y: 62 },
    ],
    lines: [],
    outputs: [1, 2, 3, 4],
  },
];

// Edge anchor for a line going from `a` to `b`. Picks the box edge nearest
// the target so lines never pass through other boxes' interiors.
//   - same column (same x): use top/bottom edges
//   - different column:     use left/right edges
function lineEndpoints(a, b) {
  if (a.x === b.x) {
    if (b.y > a.y) {
      return {
        x1: a.x + BOX / 2, y1: a.y + BOX,
        x2: b.x + BOX / 2, y2: b.y,
      };
    }
    return {
      x1: a.x + BOX / 2, y1: a.y,
      x2: b.x + BOX / 2, y2: b.y + BOX,
    };
  }
  const aIsLeft = a.x < b.x;
  return {
    x1: aIsLeft ? a.x + BOX : a.x,
    y1: a.y + BOX / 2,
    x2: aIsLeft ? b.x       : b.x + BOX,
    y2: b.y + BOX / 2,
  };
}

function svgEl(name, attrs = {}) {
  const node = document.createElementNS(SVG_NS, name);
  for (const [k, v] of Object.entries(attrs)) {
    node.setAttribute(k, String(v));
  }
  return node;
}

export function mount(host, opts = {}) {
  const {
    size = 112,
    tipId = null,
  } = opts;

  host.style.display = "inline-block";
  host.style.width  = `${size}px`;
  host.style.height = `${size}px`;
  if (tipId) applyTooltip(host, tipId);

  // We render every algorithm inside the same 112-unit viewBox; `size`
  // controls the rendered pixel area. The LCD background + bezel come
  // from the chassis CSS recipe (.fm-algo-diagram .algo-diagram in
  // design-system.css / fm-view inline styles).
  const svg = svgEl("svg", {
    width: size,
    height: size,
    viewBox: `0 0 ${VIEW} ${VIEW}`,
    "shape-rendering": "geometricPrecision",
  });
  // Phosphor bloom on every stroke + fill inside the SVG.
  svg.style.filter = "drop-shadow(0 0 2px rgba(78,160,255,0.6))";
  host.appendChild(svg);

  let algoIdx = 0;

  const draw = () => {
    while (svg.firstChild) svg.removeChild(svg.firstChild);

    const algo = ALGOS[Math.max(0, Math.min(ALGOS.length - 1, algoIdx))];

    // Stroke colour comes from the CSS variable so the diagram tracks any
    // future palette change (themed LCD readouts).
    const stroke = "var(--lcd-text-on, #4ea0ff)";

    // 1. Modulator lines (rendered before boxes so the box fills cover line
    //    endpoints cleanly).
    const boxByOp = new Map(algo.boxes.map((b) => [b.op, b]));
    for (const [from, to] of algo.lines) {
      const a = boxByOp.get(from);
      const b = boxByOp.get(to);
      if (!a || !b) continue;
      const { x1, y1, x2, y2 } = lineEndpoints(a, b);
      svg.appendChild(svgEl("line", {
        x1, y1, x2, y2,
        stroke,
        "stroke-width": 1.4,
        "stroke-linecap": "round",
      }));
    }

    // 2. Output arrows: a short horizontal line + small triangle from each
    //    carrier's right-middle edge to the SVG's right edge.
    for (const op of algo.outputs) {
      const b = boxByOp.get(op);
      if (!b) continue;
      const ax = b.x + BOX;
      const ay = b.y + BOX / 2;
      svg.appendChild(svgEl("line", {
        x1: ax, y1: ay, x2: RIGHT_EDGE, y2: ay,
        stroke,
        "stroke-width": 1.4,
        "stroke-linecap": "round",
      }));
      // Triangle arrowhead. 5px wide, 4px tall.
      svg.appendChild(svgEl("polygon", {
        points: `${RIGHT_EDGE},${ay - 3} ${RIGHT_EDGE + 4},${ay} ${RIGHT_EDGE},${ay + 3}`,
        fill: stroke,
        stroke: "none",
      }));
    }

    // 3. Operator boxes — rectangles with a dark fill so they read as solid
    //    even where modulator lines pass beneath, and a numeric label
    //    centred inside.
    for (const b of algo.boxes) {
      svg.appendChild(svgEl("rect", {
        x: b.x, y: b.y,
        width:  BOX, height: BOX,
        fill: "var(--lcd-bg-edge, #06080f)",
        stroke,
        "stroke-width": 1.2,
      }));
      const label = svgEl("text", {
        x: b.x + BOX / 2,
        y: b.y + BOX / 2 + 0.5,
        "text-anchor": "middle",
        "dominant-baseline": "middle",
        "font-family": "'IBM Plex Mono', monospace",
        "font-size": 10,
        "font-weight": 500,
        fill: stroke,
      });
      label.textContent = String(b.op);
      svg.appendChild(label);
    }

    // 4. ALG N caption at the bottom — small and dimmer than the diagram.
    const caption = svgEl("text", {
      x: VIEW / 2,
      y: VIEW - 2,
      "text-anchor": "middle",
      "font-family": "'IBM Plex Mono', monospace",
      "font-size": 9,
      "font-weight": 500,
      fill: stroke,
    });
    caption.textContent = `ALG ${algoIdx + 1}`;
    svg.appendChild(caption);
  };

  draw();

  return {
    setAlgorithm(idx) {
      algoIdx = Math.max(0, Math.min(ALGOS.length - 1, idx));
      draw();
    },
    getAlgorithm() { return algoIdx; },
    dispose() {
      if (svg.parentNode === host) host.removeChild(svg);
    },
  };
}
