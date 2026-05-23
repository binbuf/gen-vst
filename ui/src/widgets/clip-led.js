/*
 * clip-led — single red LED at the end of the header voice row that lights
 * from the telemetry `clip` flag and decays over ~1 s (08-ui-views.md view 1).
 *
 * The C++ side reports a sticky boolean each meterData tick — true means the
 * soft-clipper has been engaging since the last read. We translate that into
 * a 1-second linear decay envelope so the LED looks like a held-and-fading
 * indicator rather than flickering at the ~30 Hz tick rate.
 */

import { setupPixelCanvas, palette } from "./pixel.js";

const SIZE = 6;
const DECAY_MS = 1000;   // 08-ui-views.md "lights and then decays over ~1 s"

export class ClipLed {
  constructor(canvas) {
    this.canvas = canvas;
    this.intensity = 0;          // 0..1, drives the colour blend each frame
    this.lastClipAt = 0;         // ms since epoch of the last positive read
    this.animationFrame = null;

    canvas.style.width = (SIZE + 2) + "px";
    canvas.style.height = (SIZE + 2) + "px";

    const setup = setupPixelCanvas(canvas);
    this.ctx = setup.ctx;
    this.w = setup.width;
    this.h = setup.height;

    this.render();
  }

  // Called every telemetry tick. `flag` is the sticky C++ clip bit — true
  // means a clip happened since the last read. The decay continues regardless
  // of subsequent ticks; a new clip just resets the timer back to full.
  setClip(flag) {
    if (flag) {
      this.lastClipAt = performance.now();
      this.intensity = 1;
      this.render();
      this.ensureAnimating();
    } else if (this.intensity > 0) {
      this.ensureAnimating();
    }
  }

  ensureAnimating() {
    if (this.animationFrame !== null) return;
    const step = () => {
      const elapsed = performance.now() - this.lastClipAt;
      const next = Math.max(0, 1 - elapsed / DECAY_MS);
      if (next !== this.intensity) {
        this.intensity = next;
        this.render();
      }
      if (this.intensity > 0) {
        this.animationFrame = requestAnimationFrame(step);
      } else {
        this.animationFrame = null;
      }
    };
    this.animationFrame = requestAnimationFrame(step);
  }

  render() {
    const ctx = this.ctx;
    const pal = palette();
    ctx.clearRect(0, 0, this.w, this.h);

    // Pixel-art rules forbid blurry blends, but a 4-level discrete intensity
    // step keeps the decay readable while still snapping to the palette
    // bevels everywhere else. Below ~0.25 we drop to the base "off" colour.
    let fill;
    if (this.intensity >= 0.75)      fill = pal["led-on"];
    else if (this.intensity >= 0.5)  fill = pal["led-on"];
    else if (this.intensity >= 0.25) fill = pal["led-dim"];
    else                             fill = pal["led-base"];

    ctx.fillStyle = fill;
    ctx.fillRect(1, 1, SIZE, SIZE);
  }
}
