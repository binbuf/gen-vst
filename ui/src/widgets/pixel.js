/*
 * Shared pixel-art helpers — 05-ui-ux.md "Pixel-Art Style Rules".
 *
 * Every Canvas widget pulls its colour from the palette CSS custom properties
 * defined in design-system.css (which mirrors the docs/genny-ui.md palette),
 * never an ad-hoc hex value. Widgets call setupPixelCanvas() at construction so
 * imageSmoothingEnabled is off and the canvas's backing store matches its CSS
 * size at the current integer device-pixel ratio. devicePixelRatio is always
 * an integer at the application's three supported scales (1x/2x/3x, ADR-0017).
 */

const PALETTE_VARS = [
  "chassis", "panel", "bevel-light", "bevel-dark",
  "lcd-base", "lcd-base-hi", "lcd-pixel", "lcd-pixel-hi",
  "led-base", "led-dim", "led-on",
  "knob-body", "knob-ring", "knob-dot",
  "logo", "logo-shadow", "logo-underline",
  "label", "select", "badge",
];

let paletteCache = null;

/** Read every palette CSS custom property once and cache. */
export function palette() {
  if (paletteCache !== null) return paletteCache;
  const styles = getComputedStyle(document.documentElement);
  paletteCache = {};
  for (const name of PALETTE_VARS)
    paletteCache[name] = styles.getPropertyValue(`--${name}`).trim();
  return paletteCache;
}

/** Force the next palette() call to re-read CSS (used by the gallery's theme
 *  re-load). */
export function invalidatePalette() { paletteCache = null; }

/**
 * Configure a canvas for pixel-art rendering: integer backing store at the
 * current device-pixel ratio, no smoothing, scale the context so callers can
 * keep authoring at logical 1x. Returns the logical width/height.
 */
export function setupPixelCanvas(canvas) {
  const dpr = Math.max(1, Math.round(window.devicePixelRatio || 1));
  const cssW = canvas.clientWidth || canvas.width;
  const cssH = canvas.clientHeight || canvas.height;

  // Width / height attributes pin the *logical* size; the backing store is
  // dpr * logical pixels so a 2x scale stays crisp without re-authoring.
  canvas.width  = cssW * dpr;
  canvas.height = cssH * dpr;
  canvas.style.width  = cssW + "px";
  canvas.style.height = cssH + "px";

  const ctx = canvas.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  return { ctx, width: cssW, height: cssH, dpr };
}

/** Snap a float to the nearest integer — fills always start on whole pixels. */
export const snap = Math.round;

/** Fill a single hard-edge bevel rectangle: light top/left + dark bottom/right
 *  (or inverted for an inset). Widths are 1 logical pixel per the rules. */
export function drawBevel(ctx, x, y, w, h, raised = true, colors = null) {
  const pal = colors ?? palette();
  const lit = raised ? pal["bevel-light"] : pal["bevel-dark"];
  const dim = raised ? pal["bevel-dark"]  : pal["bevel-light"];
  // Hard-edge 1px borders (05-ui-ux.md). Drawn as fills so we stay on integer
  // coords; strokes default to 0.5px offsets that blur.
  ctx.fillStyle = lit;
  ctx.fillRect(x, y, w, 1);            // top
  ctx.fillStyle = lit;
  ctx.fillRect(x, y, 1, h);            // left
  ctx.fillStyle = dim;
  ctx.fillRect(x, y + h - 1, w, 1);    // bottom
  ctx.fillStyle = dim;
  ctx.fillRect(x + w - 1, y, 1, h);    // right
}

/**
 * Ordered 2x2 Bayer dither fill — two-tone shading without gradients
 * (05-ui-ux.md "No smooth gradients"). pct in [0..1] picks how often the
 * "lit" colour appears.
 */
export function ditherFill(ctx, x, y, w, h, a, b, pct) {
  const t = Math.max(0, Math.min(1, pct));
  // 4-level Bayer pattern (2x2): thresholds 0.25, 0.5, 0.75 give crisp,
  // tileable transitions per docs requirements.
  const bayer = [[0, 2], [3, 1]];
  for (let py = 0; py < h; ++py) {
    for (let px = 0; px < w; ++px) {
      const threshold = (bayer[py & 1][px & 1] + 0.5) / 4;
      ctx.fillStyle = t > threshold ? b : a;
      ctx.fillRect(snap(x + px), snap(y + py), 1, 1);
    }
  }
}

/* -------------------------------------------------------------------------- */
/* 5x7 dot-matrix glyph table                                                 */
/*                                                                            */
/* 7 rows, 5 columns each. MSB is the leftmost dot. The supported character   */
/* set is exactly digits 0-9, the letters O and F (for the "OFF" readout),    */
/* "-" for negative numbers, and " " (blank). 05-ui-ux.md "5x7 Dot-Matrix     */
/* Readouts" specifies *what* glyphs exist and how they're drawn, not the     */
/* exact bitmap — the pixels below are hand-pixeled to read crisply at 1px    */
/* dots and remain recognizable at integer scaling.                           */
/* -------------------------------------------------------------------------- */

export const DOT_SIZE = 1;
export const DOT_PITCH = 2;

const G = (...rows) => rows;        // tidy 7-row literal

export const GLYPHS = {
  "0": G(
    0b01110,
    0b10001,
    0b10011,
    0b10101,
    0b11001,
    0b10001,
    0b01110,
  ),
  "1": G(
    0b00100,
    0b01100,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b01110,
  ),
  "2": G(
    0b01110,
    0b10001,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b11111,
  ),
  "3": G(
    0b01110,
    0b10001,
    0b00001,
    0b00110,
    0b00001,
    0b10001,
    0b01110,
  ),
  "4": G(
    0b00010,
    0b00110,
    0b01010,
    0b10010,
    0b11111,
    0b00010,
    0b00010,
  ),
  "5": G(
    0b11111,
    0b10000,
    0b11110,
    0b00001,
    0b00001,
    0b10001,
    0b01110,
  ),
  "6": G(
    0b00110,
    0b01000,
    0b10000,
    0b11110,
    0b10001,
    0b10001,
    0b01110,
  ),
  "7": G(
    0b11111,
    0b00001,
    0b00010,
    0b00100,
    0b01000,
    0b01000,
    0b01000,
  ),
  "8": G(
    0b01110,
    0b10001,
    0b10001,
    0b01110,
    0b10001,
    0b10001,
    0b01110,
  ),
  "9": G(
    0b01110,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b00010,
    0b01100,
  ),
  "O": G(
    0b01110,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b01110,
  ),
  "F": G(
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b10000,
    0b10000,
    0b10000,
  ),
  "-": G(
    0b00000,
    0b00000,
    0b00000,
    0b01110,
    0b00000,
    0b00000,
    0b00000,
  ),
  " ": G(
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
  ),
};

/** Width / height of one glyph cell in pixels at 1x. */
export const GLYPH_W = 5 * DOT_PITCH - (DOT_PITCH - DOT_SIZE);
export const GLYPH_H = 7 * DOT_PITCH - (DOT_PITCH - DOT_SIZE);
/** Gap between two adjacent glyphs (one empty dot column). */
export const GLYPH_GAP = DOT_PITCH;

/** Pixel width of a `widthChars`-character readout including inter-char gaps. */
export function readoutWidth(widthChars) {
  return widthChars * GLYPH_W + (widthChars - 1) * GLYPH_GAP;
}

/**
 * Draw a 5x7 LED-style readout at (x, y).
 * - `text`: arbitrary string; any character outside the supported set is
 *   replaced with blank glyphs (the calling widget should already format
 *   exactly what the readout can render).
 * - `widthChars`: number of glyph cells the readout reserves. Text is
 *   right-aligned within this width (LED readouts right-justify).
 * - `colors`: optional `{ lit, dim }` overrides; defaults read from the palette.
 *
 * Both lit and unlit dots are drawn — the unlit grid stays faintly visible
 * behind the lit characters, exactly as a real dot-matrix display does
 * (05-ui-ux.md "Render algorithm"). No bloom by default.
 */
export function drawLedReadout(ctx, x, y, text, widthChars, colors) {
  const pal = palette();
  const lit = (colors && colors.lit) || pal["led-on"];
  const dim = (colors && colors.dim) || pal["led-dim"];

  // Right-align: pad with blanks on the left to fill widthChars.
  const padded = (text.length >= widthChars)
    ? text.slice(text.length - widthChars)
    : " ".repeat(widthChars - text.length) + text;

  for (let c = 0; c < widthChars; ++c) {
    const ch = padded.charAt(c);
    const glyph = GLYPHS[ch] || GLYPHS[" "];
    const cellX = x + c * (GLYPH_W + GLYPH_GAP);
    for (let row = 0; row < 7; ++row) {
      const bits = glyph[row];
      for (let col = 0; col < 5; ++col) {
        const on = (bits >> (4 - col)) & 1;
        ctx.fillStyle = on ? lit : dim;
        ctx.fillRect(
          snap(cellX + col * DOT_PITCH),
          snap(y + row * DOT_PITCH),
          DOT_SIZE,
          DOT_SIZE,
        );
      }
    }
  }
}

/* -------------------------------------------------------------------------- */
/* Press Start 2P bitmap text                                                 */
/*                                                                            */
/* All text labels in the UI use the Press Start 2P font at 8 or 16px (never  */
/* fractional). When drawing on canvas we re-enable smoothing-OFF and use the */
/* bitmap font directly. Helper exists so widgets don't duplicate the         */
/* font-string formatting.                                                    */
/* -------------------------------------------------------------------------- */

export function drawLabel(ctx, x, y, text, size = 8, color = null) {
  const pal = palette();
  ctx.save();
  ctx.imageSmoothingEnabled = false;
  ctx.font = `${size}px "Press Start 2P", monospace`;
  ctx.textBaseline = "top";
  ctx.fillStyle = color || pal["label"];
  ctx.fillText(text.toUpperCase(), snap(x), snap(y));
  ctx.restore();
}
