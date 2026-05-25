// `envelope-curve` widget — canvas ADSR polyline + segment labels + key
// markers for a YM2612 operator.
//
// setEnvelope(ar, dr, sl, sr, rr) accepts the 5 hardware-style envelope
// values:
//   AR ∈ [0..31] (attack rate, higher = faster)
//   DR ∈ [0..31] (decay rate)
//   SL ∈ [0..15] (sustain level, 0 = full, 15 = silent)
//   SR ∈ [0..31] (sustain rate)
//   RR ∈ [0..15] (release rate)
//
// The widget reduces those to a 5-segment polyline + the key-on / key-off
// vertical markers per 09-visual-spec.md *Envelope curve (canvas)*. Labels
// (AR / DR / SL / SR / RR) sit at each segment midpoint with the same
// phosphor bloom as the LCD readouts.

import { applyTooltip } from "./tooltip-content.js";

function cssVar(name, fallback = "#000") {
  const v = getComputedStyle(document.documentElement)
    .getPropertyValue(name).trim();
  return v || fallback;
}

// Reduce a hardware "rate" 0..maxRate to a relative segment length 0..1.
// Lower rate = slower envelope = longer segment. We map linearly with a
// floor so even max-rate stays visible.
function rateToLength(rate, maxRate) {
  const inv = 1 - rate / maxRate;
  return 0.06 + inv * 0.94;       // floor 6%, top 100%
}

export function mount(host, opts = {}) {
  const {
    width = 220,
    height = 110,
    tipId = null,
  } = opts;

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
  ctx.imageSmoothingEnabled = true;

  let env = { ar: 25, dr: 10, sl: 6, sr: 6, rr: 7 };

  const draw = () => {
    const w = width, h = height;
    ctx.clearRect(0, 0, w, h);

    // 1. LCD background
    ctx.fillStyle = cssVar("--lcd-bg", "#0d1424");
    ctx.fillRect(0, 0, w, h);
    ctx.strokeStyle = "rgba(0,0,0,0.7)";
    ctx.lineWidth = 1;
    ctx.strokeRect(0.5, 0.5, w - 1, h - 1);

    const padX = 8;
    const padTop = 6;
    const padBot = 14;        // KEY ON / KEY OFF captions sit here
    const plotW = w - padX * 2;
    const plotH = h - padTop - padBot;
    const yTop = padTop;
    const yBot = padTop + plotH;
    const yFor = (norm) => yBot - norm * plotH;     // 0..1 -> y
    const xFor = (norm) => padX + norm * plotW;

    // 2. Compute segment widths.
    const attackLen   = rateToLength(env.ar, 31);
    const decayLen    = rateToLength(env.dr, 31);
    const sustainLen  = 0.30;                       // fixed sustain window
    const releaseLen  = rateToLength(env.rr, 15);
    const sumPlot     = attackLen + decayLen + sustainLen + releaseLen;

    // Scale all segments so they fit horizontally with a small reserved tail
    // (5%) past KEY OFF for the release segment's start.
    const scale = 0.95 / sumPlot;
    const xA0 = 0;
    const xA1 = xA0 + attackLen * scale;
    const xD1 = xA1 + decayLen  * scale;
    const xS1 = xD1 + sustainLen * scale;     // KEY OFF marker
    const xR1 = xS1 + releaseLen * scale;

    // Sustain level: SL=0 -> full, SL=15 -> silent.
    const sustainAmp = 1 - env.sl / 15;
    // SR pulls the sustain level down across the sustain segment toward 0.
    const sustainDecay = (env.sr / 31) * 0.4;
    const sustainEndAmp = Math.max(0, sustainAmp - sustainDecay);

    // 3. Draw polyline.
    ctx.strokeStyle = cssVar("--lcd-text-on", "#4ea0ff");
    ctx.lineWidth = 1.5;
    ctx.lineJoin = "round";
    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur = 3;

    ctx.beginPath();
    ctx.moveTo(xFor(xA0), yFor(0));
    ctx.lineTo(xFor(xA1), yFor(1));               // attack to peak
    ctx.lineTo(xFor(xD1), yFor(sustainAmp));      // decay to sustain
    ctx.lineTo(xFor(xS1), yFor(sustainEndAmp));   // sustain (slope or flat)
    ctx.lineTo(xFor(xR1), yFor(0));               // release
    ctx.stroke();
    ctx.shadowBlur = 0;

    // 4. KEY ON / KEY OFF dashed verticals.
    const keyOnX  = xFor(xA0);
    const keyOffX = xFor(xS1);
    ctx.setLineDash([2, 2]);
    ctx.strokeStyle = "rgba(78,160,255,0.45)";
    ctx.lineWidth = 0.8;
    for (const mx of [keyOnX, keyOffX]) {
      ctx.beginPath();
      ctx.moveTo(mx, yTop);
      ctx.lineTo(mx, yBot);
      ctx.stroke();
    }
    ctx.setLineDash([]);

    // 5. Segment labels at the midpoint of each segment.
    const labels = [
      { tag: "AR", x0: xA0, x1: xA1, y: 0.85 },
      { tag: "DR", x0: xA1, x1: xD1, y: 0.55 },
      { tag: "SL", x0: xD1, x1: xS1, y: Math.min(0.95, sustainAmp + 0.10) },
      { tag: "SR", x0: xD1, x1: xS1, y: Math.max(0.05, sustainEndAmp - 0.18) },
      { tag: "RR", x0: xS1, x1: xR1, y: 0.45 },
    ];
    ctx.font = "500 7px 'IBM Plex Mono', monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillStyle = cssVar("--lcd-text-on", "#4ea0ff");
    for (const lab of labels) {
      const mx = xFor((lab.x0 + lab.x1) / 2);
      const my = yFor(lab.y);
      ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
      ctx.shadowBlur = 3;
      ctx.fillText(lab.tag, mx, my);
      ctx.shadowBlur = 0;
      ctx.fillText(lab.tag, mx, my);
    }

    // 6. KEY ON / KEY OFF captions.
    ctx.font = "500 6px 'IBM Plex Mono', monospace";
    ctx.textBaseline = "alphabetic";
    ctx.shadowColor = cssVar("--lcd-text-glow", "rgba(78,160,255,0.6)");
    ctx.shadowBlur = 2;
    ctx.fillText("KEY ON",  keyOnX,  h - 3);
    ctx.fillText("KEY OFF", keyOffX, h - 3);
    ctx.shadowBlur = 0;
  };

  draw();

  return {
    setEnvelope(ar, dr, sl, sr, rr) {
      if (Number.isFinite(ar)) env.ar = ar;
      if (Number.isFinite(dr)) env.dr = dr;
      if (Number.isFinite(sl)) env.sl = sl;
      if (Number.isFinite(sr)) env.sr = sr;
      if (Number.isFinite(rr)) env.rr = rr;
      draw();
    },
    getEnvelope() { return { ...env }; },
    redraw: draw,
    dispose() {
      if (canvas.parentNode === host) host.removeChild(canvas);
    },
  };
}
