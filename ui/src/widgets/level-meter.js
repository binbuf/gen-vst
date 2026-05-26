// `level-meter` widget — canvas row of LED segments.
//
// Two ways to drive level:
//   1. setLevel(0..1) — direct push (gallery uses a slider).
//   2. Subscribe to the `meterData` event (peakL / peakR) by passing
//      opts.channel = "L" | "R". This is the canonical chassis use; the
//      30 Hz timer in the C++ editor emits one event per tick.
//
// Last 2 segments turn `--led-on-warm` red as peak indication
// (09-visual-spec.md *Level meter*).

import { onBackendEvent } from "../binding.js";
import { applyTooltip } from "./tooltip-content.js";

function cssVar(name, fallback = "#000") {
  const v = getComputedStyle(document.documentElement)
    .getPropertyValue(name).trim();
  return v || fallback;
}

export function mount(host, opts = {}) {
  const {
    width = 120,
    height = 14,
    segments = 20,
    channel = null,             // "L" or "R" -> subscribes to meterData
    orientation = "horizontal", // currently only horizontal is drawn
    tipId = null,
  } = opts;
  void orientation;

  host.style.display = "inline-block";
  host.style.width  = `${width}px`;
  host.style.height = `${height}px`;
  if (tipId) applyTooltip(host, tipId);

  const canvas = document.createElement("canvas");
  canvas.style.width  = `${width}px`;
  canvas.style.height = `${height}px`;
  canvas.style.display = "block";
  host.appendChild(canvas);

  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  canvas.width  = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  ctx.scale(dpr, dpr);

  let level = 0;
  const draw = () => {
    const w = width, h = height;
    ctx.clearRect(0, 0, w, h);

    // Chassis (--led-meter-bg) + inset edge.
    ctx.fillStyle = cssVar("--led-meter-bg", "#060810");
    ctx.fillRect(0, 0, w, h);

    ctx.strokeStyle = "rgba(0,0,0,0.6)";
    ctx.lineWidth = 1;
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);

    const pad = 3;
    const gap = 1;
    const innerW = w - pad * 2;
    const segW = (innerW - gap * (segments - 1)) / segments;
    const innerH = h - pad * 2;
    const lit = Math.round(Math.min(1, Math.max(0, level)) * segments);

    const colOn   = cssVar("--led-on",      "#2196f3");
    const colWarm = cssVar("--led-on-warm", "#ff5252");
    const colDim  = cssVar("--led-dim",     "#082040");

    for (let i = 0; i < segments; ++i) {
      const isLit  = i < lit;
      const isWarm = i >= segments - 2;
      ctx.fillStyle = isLit
        ? (isWarm ? colWarm : colOn)
        : colDim;
      const x = pad + i * (segW + gap);
      ctx.fillRect(x, pad, segW, innerH);
    }
  };

  draw();

  let unsubMeter = null;
  if (channel === "L" || channel === "R") {
    unsubMeter = onBackendEvent("meterData", (payload) => {
      if (!payload) return;
      const v = channel === "L" ? payload.peakL : payload.peakR;
      if (typeof v === "number") {
        level = v;
        draw();
      }
    });
  }

  return {
    setLevel(v) { level = Math.max(0, Math.min(1, v)); draw(); },
    getLevel()  { return level; },
    redraw: draw,
    dispose() {
      if (unsubMeter) unsubMeter();
      if (canvas.parentNode === host) host.removeChild(canvas);
    },
  };
}
