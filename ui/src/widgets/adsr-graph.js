/*
 * adsr-graph — per-operator envelope display inside a green-LCD inset
 * (genny-ui.md "Bottom Row" — LEV display).
 *
 * The curve is computed **analytically in JS** from the five envelope
 * parameters (ATK/DR1/SUS/DR2/RR) — there is no C++ round-trip per
 * docs/design/05-ui-ux.md "Component Inventory". Redraws on any envelope
 * value-changed event.
 *
 * The envelope model is a simplified 4-stage attack/decay/sustain/release
 * cycle aligned with docs/design/02-fm-synthesis.md "Envelope Generator":
 *   AR  (0..31)  attack rate    -> faster = shorter attack
 *   DR  (0..31)  first decay    -> falls from peak to sustain level
 *   SL  (0..15)  sustain level  -> 0=peak, 15=-45 dB
 *   SR  (0..31)  second decay   -> 0 = hold; else continues falling
 *   RR  (0..15)  release rate   -> after key-off, falls to silence
 *
 * Curve shape is a rough caricature, not a hardware-accurate emulation — the
 * goal is a recognisable per-patch silhouette in the LCD, as in the genny-ui
 * reference. Each rate maps to a duration via a power curve so that the
 * extreme values produce visibly distinct shapes without dominating the
 * display.
 */

import { setupPixelCanvas, palette, drawBevel, snap } from "./pixel.js";

// Tuning constants for the visual envelope; chosen so a "no-attack" (AR=0)
// envelope still draws a flat line and a "max-attack" (AR=31) draws an
// almost-instant rise.
const MAX_W_SEGMENTS = 4;            // attack, decay, sustain, release
const PAD_X = 2;
const PAD_Y = 2;

function rateToFraction(rate, maxRate) {
  // 0 => long time (occupies the full segment), maxRate => near-zero time.
  // Power curve to emphasise the middle values.
  if (rate <= 0) return 1.0;
  const t = Math.max(0, Math.min(1, rate / maxRate));
  return Math.pow(1.0 - t, 1.8) + 0.02;   // never quite zero
}

export class AdsrGraph {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    // Bindings for the five envelope parameters — passed in by the operator
    // panel composite. Stored as { ar, dr, sl, sr, rr } sliders.
    this.bindings = options.bindings ?? {};

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this._unsubscribes = [];
    for (const b of Object.values(this.bindings)) {
      this._unsubscribes.push(b.onChange(() => this.render()));
    }

    this.render();
  }

  _read(name, max) {
    const b = this.bindings[name];
    if (!b) return 0;
    const v = Math.max(0, Math.min(max, Math.round(b.getScaled())));
    return v;
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();

    // Green-LCD base + recessed bevel.
    ctx.fillStyle = pal["lcd-base"];
    ctx.fillRect(0, 0, this.w, this.h);
    drawBevel(ctx, 0, 0, this.w, this.h, false);

    const ar = this._read("ar", 31);
    const dr = this._read("dr", 31);
    const sl = this._read("sl", 15);
    const sr = this._read("sr", 31);
    const rr = this._read("rr", 15);

    // Convert each rate to a horizontal fraction of the inner display width.
    // The segments are normalised so they always sum to 1 (the chart fits).
    const fAttack  = rateToFraction(ar, 31);
    const fDecay1  = rateToFraction(dr, 31);
    const fSustain = (sr === 0) ? 0.6 : rateToFraction(sr, 31);  // visible "hold" portion
    const fRelease = rateToFraction(rr, 15);
    const total = fAttack + fDecay1 + fSustain + fRelease;
    const norm = (x) => x / (total > 0 ? total : 1);

    const wAttack  = (this.w - PAD_X * 2) * norm(fAttack);
    const wDecay1  = (this.w - PAD_X * 2) * norm(fDecay1);
    const wSustain = (this.w - PAD_X * 2) * norm(fSustain);
    const wRelease = (this.w - PAD_X * 2) * norm(fRelease);

    // Vertical levels: peak at top, sustain at sl-derived row, baseline at
    // bottom. SL=0 => sustain at top (no decay drop); SL=15 => sustain near
    // baseline.
    const topY  = PAD_Y;
    const baseY = this.h - PAD_Y - 1;
    const peakLevel = baseY;                          // baseline
    const peakY = topY;                                // top of LCD
    const slFraction = sl / 15;                        // 0..1
    const sustainY = topY + Math.round((baseY - topY) * (ar === 0 ? 1 : slFraction));
    const endOfDecay2Y = (sr === 0)
      ? sustainY
      : Math.min(baseY, sustainY + Math.round((baseY - sustainY) * 0.5));

    // AR=0 produces a totally silent envelope — draw a flat line at baseline.
    const startY = ar === 0 ? baseY : baseY;
    const finalAttackY = ar === 0 ? baseY : peakY;

    let x = PAD_X;
    const points = [];
    points.push([x, startY]);
    // Attack: rise to peak (or stay flat if AR=0).
    x += Math.round(wAttack);
    points.push([x, finalAttackY]);
    // First decay: fall to sustain level (unless AR=0).
    x += Math.round(wDecay1);
    points.push([x, ar === 0 ? baseY : sustainY]);
    // Sustain / SR phase: hold or continued fall.
    x += Math.round(wSustain);
    points.push([x, ar === 0 ? baseY : endOfDecay2Y]);
    // Release: fall to silence.
    x += Math.round(wRelease);
    points.push([Math.min(this.w - PAD_X - 1, x), baseY]);

    // Plot the polyline as 1px segments (Bresenham-style) for crisp 1-pixel
    // pixel-art strokes — no antialiased curves.
    ctx.fillStyle = pal["lcd-pixel"];
    for (let i = 0; i < points.length - 1; ++i) {
      this._line(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1]);
    }
  }

  _line(x0, y0, x1, y1) {
    const ctx = this.ctx;
    let X0 = snap(x0), Y0 = snap(y0);
    const X1 = snap(x1), Y1 = snap(y1);
    const dx = Math.abs(X1 - X0);
    const dy = -Math.abs(Y1 - Y0);
    const sx = X0 < X1 ? 1 : -1;
    const sy = Y0 < Y1 ? 1 : -1;
    let err = dx + dy;
    while (true) {
      ctx.fillRect(X0, Y0, 1, 1);
      if (X0 === X1 && Y0 === Y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; X0 += sx; }
      if (e2 <= dx) { err += dx; Y0 += sy; }
    }
  }

  destroy() {
    for (const fn of this._unsubscribes) fn?.();
    this._unsubscribes = [];
  }
}
