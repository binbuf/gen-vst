/*
 * voice-leds — a row of 16 tiny LEDs (one per pool voice, ADR-0010) along the
 * lower edge of the header (08-ui-views.md view 1).
 *
 * Each LED reflects one bit of the C++ voiceMask telemetry (bit i = voice
 * slot i is Active / keyed on). Updated each meterData tick at ~30 Hz; the
 * widget skips the repaint when the mask hasn't changed so a long-held note
 * doesn't cost an unnecessary canvas fill every tick.
 */

import { setupPixelCanvas, palette, drawBevel } from "./pixel.js";

const NUM_VOICES = 16;
const LED_SIZE = 6;
const GAP = 2;

export class VoiceLeds {
  constructor(canvas) {
    this.canvas = canvas;
    this.mask = 0;   // bit i = voice i keyed on

    const totalW = NUM_VOICES * LED_SIZE + (NUM_VOICES - 1) * GAP;
    canvas.style.width = totalW + "px";
    canvas.style.height = (LED_SIZE + 2) + "px";

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  setMask(m) {
    const next = (m | 0) & 0xffff;
    if (next === this.mask) return;
    this.mask = next;
    this.render();
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.clearRect(0, 0, this.w, this.h);

    for (let i = 0; i < NUM_VOICES; ++i) {
      const x = i * (LED_SIZE + GAP);
      const lit = (this.mask >> i) & 1;
      // Lit LEDs use the bright phosphor; unlit LEDs use the dim olive base
      // and pick up a 1px inset bevel so the row reads as a panel of
      // physical indicators rather than a flat strip of dots.
      ctx.fillStyle = lit ? pal["lcd-pixel-hi"] : pal["lcd-base"];
      ctx.fillRect(x, 1, LED_SIZE, LED_SIZE);
      drawBevel(ctx, x, 1, LED_SIZE, LED_SIZE, false);
    }
  }
}
