# Visual Specification (v2)

This document is the **authoritative visual reference** for Gen VST v2.
The v1 visual spec at `docs/genny-ui.md` is archived under
`docs/design/archive/v1-genny-ui.md` and superseded.

[`05-ui-ux.md`](05-ui-ux.md) defines the binding **principles**
(top-left light, layered shadows, gradients, monospace labels, press
feedback); this doc pins **exact values** — the palette, typography
choices, and CSS / Canvas recipes per widget — built against
Inphonik's RYM2612 (FM mode reference) and PCM2612 Retro Decimator Unit
(D mode reference).

Anything not covered here defers to the principles in `05-ui-ux.md`.
**New colours, fonts, or widget recipes must be added to this file
first**, never invented ad hoc inside a component.

---

## Palette

CSS custom properties. Every widget references these tokens; no
hand-typed hex inside components.

### Chassis (the panel substrate, light region — PCM2612-style)

| Token | Hex | Use |
|---|---|---|
| `--chassis-bg-top`    | `#c8ccd0` | Top stop of the chassis vertical gradient |
| `--chassis-bg-mid`    | `#aaaeb2` | Middle stop |
| `--chassis-bg-bottom` | `#6b6e72` | Bottom stop |
| `--chassis-edge`      | `#3a3c40` | Outer chassis bevel edge |

### Inset (recessed surfaces, dark region — bottom band on PCM2612, all of RYM2612)

| Token | Hex | Use |
|---|---|---|
| `--inset-bg`         | `#1c1f24` | Recessed inset background |
| `--inset-edge-dark`  | `#0a0c10` | Dark inset edge (bottom-right under top-left light) |
| `--inset-edge-light` | `#2a2d32` | Lit inset highlight (top-left edge) |

### Knobs (chunky physical control surfaces)

| Token | Hex | Use |
|---|---|---|
| `--knob-body-light` | `#4a5570`            | Top of knob radial / linear gradient |
| `--knob-body-dark`  | `#1f2532`            | Bottom of knob gradient (deep navy) |
| `--knob-rim`        | `#0a0e15`            | Rim shadow |
| `--knob-indicator`  | `#f8fafc`            | White indicator line / dot |
| `--knob-highlight`  | `rgba(255,255,255,0.15)` | Top sheen overlay (gloss) |

The PCM2612 large knob is darker (matte black-ish); use
`--knob-body-dark` as the dominant stop and skip the lighter highlight
for the `decimator-knob` variant.

### LCDs (patch-name display + numeric readouts)

| Token | Hex | Use |
|---|---|---|
| `--lcd-bg`         | `#0d1424`            | LCD base colour (deep navy-black) |
| `--lcd-bg-edge`    | `#06080f`            | Recessed edge / inner shadow |
| `--lcd-text-on`    | `#4ea0ff`            | Active LCD glyphs (bright cyan-blue) |
| `--lcd-text-glow`  | `rgba(78,160,255,0.6)` | Phosphor bloom around active glyphs |
| `--lcd-text-off`   | `#1a2438`            | Dim outline of unlit segments / placeholder text |

### LED meters

| Token | Hex | Use |
|---|---|---|
| `--led-on`        | `#2196f3` | Active level meter bar |
| `--led-on-warm`   | `#ff5252` | Peak/clip indicator (last 1–2 bars) |
| `--led-dim`       | `#082040` | Inactive bar background |
| `--led-meter-bg`  | `#060810` | Meter chassis (inset background) |

### Toggle switches & buttons

| Token | Hex | Use |
|---|---|---|
| `--toggle-on`        | `#2196f3` | Lit toggle (MONO when on, FILTERING when on) |
| `--toggle-off`       | `#2a2d32` | Unlit toggle |
| `--toggle-rim`       | `#0a0c10` | Toggle bezel shadow |
| `--btn-bg-top`       | `#4a5570` | Pill / square button top stop |
| `--btn-bg-bottom`    | `#2a3245` | Bottom stop |
| `--btn-active-bg`    | `#2196f3` | Pressed / active button fill |
| `--btn-text`         | `#e8eaee` | Button text |

### Text

| Token | Hex | Use |
|---|---|---|
| `--label-text`        | `#d8dce0` | Label text on dark inset surfaces |
| `--label-text-dim`    | `#888a8d` | Secondary / disabled label text |
| `--text-on-chassis`   | `#2a2c30` | Text on the light silver chassis (e.g., section titles) |

### Brand / accents / toasts

| Token | Hex | Use |
|---|---|---|
| `--accent-info`    | `#2196f3` | Info toasts |
| `--accent-warning` | `#ff9100` | Warning toasts |
| `--accent-error`   | `#ff5252` | Error toasts |
| `--brand-mark`     | `#5a5d62` | Wordmark colour (medium-grey, PCM2612-style) |

---

## Typography

**One family, two roles.** All text uses **IBM Plex Mono** (SIL OFL,
Regular and Medium weights). Labels and LCD readouts differentiate by
size, colour, and post-processing rather than by switching font family
— this keeps the bundle lean and the typography coherent.

| Use | Family | Weight | Size | Letter-spacing | Color |
|---|---|---|---|---|---|
| Section title (FM/SQ/D panel headers) | IBM Plex Mono | Medium | 11 px | 0.18 em | `--text-on-chassis` (or `--label-text` on insets) |
| Control label (`AR`, `MUL`, `FREQ`, etc.) | IBM Plex Mono | Medium | 9 px | 0.20 em | `--label-text` |
| Knob value readout (small LCD) | IBM Plex Mono | Medium | 10 px | 0.06 em | `--lcd-text-on` + bloom filter |
| Header patch-name LCD | IBM Plex Mono | Medium | 18 px | 0.08 em | `--lcd-text-on` + heavier bloom |
| Algorithm number, op badges | IBM Plex Mono | Medium | 12 px | 0 | `--label-text` |
| Wordmark `GEN VST` | IBM Plex Mono | Bold | 22 px | 0.15 em | `--brand-mark` |
| Body text (modals, About) | IBM Plex Mono | Regular | 12 px | 0 | `--label-text` |

All caps for labels (CSS `text-transform: uppercase`); patch names and
modal body text are case-preserving.

**Font file.** Single woff2 — `IBM-Plex-Mono.woff2` with Regular + Medium
+ Bold weights subset to Latin Basic + Latin Extended-A. Lives in
`extern/fonts/ibm-plex-mono/`. Loaded once via `@font-face` in the v2
`design-system.css`.

The v1 fonts (Press Start 2P, torinak 7-segment, custom 5×7 dot-matrix
renderer) are retired from the active build.

---

## CSS recipes per widget

These are the canonical recipes. Per-widget JS / HTML files implement
them; any deviation needs to be justified and added to this doc.

### Chassis panel (the silver outer surface)

```css
.chassis {
  background: linear-gradient(
    180deg,
    var(--chassis-bg-top)    0%,
    var(--chassis-bg-mid)   50%,
    var(--chassis-bg-bottom) 100%
  );
  border: 1px solid var(--chassis-edge);
  box-shadow:
    /* outer drop shadow (light from top-left) */
    4px 4px 12px rgba(0,0,0,0.45),
    /* inner top-left highlight (1px solid) */
    inset 1px 1px 0 rgba(255,255,255,0.35),
    /* inner bottom-right shadow (subtle) */
    inset -1px -1px 0 rgba(0,0,0,0.25);
  border-radius: 6px;
}
```

### Recessed inset (the dark region holding controls)

```css
.inset {
  background: var(--inset-bg);
  border: 1px solid var(--inset-edge-dark);
  box-shadow:
    inset 2px 2px 6px rgba(0,0,0,0.6),
    inset -1px -1px 0 var(--inset-edge-light);
  border-radius: 4px;
}
```

### Knob — body (CSS) + indicator (Canvas)

CSS for the body — a circular gradient with a top sheen:

```css
.knob {
  width: 48px;  /* default; decimator-knob = 96px */
  height: 48px;
  border-radius: 50%;
  background:
    /* top sheen */
    radial-gradient(circle at 30% 25%, var(--knob-highlight), transparent 55%),
    /* main body gradient (top-left light) */
    linear-gradient(135deg, var(--knob-body-light), var(--knob-body-dark));
  border: 1px solid var(--knob-rim);
  box-shadow:
    /* outer hard shadow (sits in inset) */
    2px 2px 4px rgba(0,0,0,0.55),
    /* inner rim definition */
    inset -1px -1px 2px rgba(0,0,0,0.55),
    inset 1px 1px 1px rgba(255,255,255,0.10);
  cursor: ns-resize;
}
.knob:active {
  transform: scale(0.97);
  transition: transform 80ms ease-out;
}
```

Canvas overlay draws the indicator line:

```js
// Canvas sized to match knob; draw a 1.5 px white line from center
// to rim at the current angle (rest at 7 o'clock; sweep ~270°).
ctx.strokeStyle = getCssVar('--knob-indicator');
ctx.lineWidth = 1.5;
ctx.lineCap = 'round';
ctx.imageSmoothingEnabled = true;
ctx.beginPath();
ctx.moveTo(cx, cy);
ctx.lineTo(cx + Math.cos(angle) * radius * 0.85,
           cy + Math.sin(angle) * radius * 0.85);
ctx.stroke();
```

**`decimator-knob` variant** (the big PCM2612-style central knob): same
mechanics, larger (96 px), and skips the top-sheen radial gradient so
the body reads as matte black — replace the first gradient layer with
`var(--knob-body-dark)` flat.

### Button (pill / square)

```css
.btn {
  font: 500 9px/1 'IBM Plex Mono', monospace;
  letter-spacing: 0.20em;
  text-transform: uppercase;
  color: var(--btn-text);
  padding: 6px 12px;
  background: linear-gradient(180deg,
    var(--btn-bg-top) 0%,
    var(--btn-bg-bottom) 100%);
  border: 1px solid var(--knob-rim);
  border-radius: 3px;
  box-shadow:
    2px 2px 3px rgba(0,0,0,0.4),
    inset 1px 1px 0 rgba(255,255,255,0.10);
  cursor: pointer;
  transition: transform 100ms ease-out, box-shadow 100ms ease-out;
}
.btn:active,
.btn.is-active {
  transform: scale(0.97);
  background: var(--btn-active-bg);
  box-shadow:
    inset 2px 2px 4px rgba(0,0,0,0.45),
    inset -1px -1px 0 rgba(255,255,255,0.10);
}
```

### Toggle switch (lit / unlit)

```css
.toggle {
  width: 28px;
  height: 28px;
  border-radius: 4px;
  background: var(--toggle-off);
  border: 1px solid var(--toggle-rim);
  box-shadow:
    inset 1px 1px 2px rgba(0,0,0,0.5),
    inset -1px -1px 0 rgba(255,255,255,0.05);
  transition: background 120ms ease-out, box-shadow 120ms ease-out;
}
.toggle.is-on {
  background: var(--toggle-on);
  box-shadow:
    0 0 8px rgba(33,150,243,0.6),  /* outer glow when lit */
    inset 1px 1px 2px rgba(255,255,255,0.25);
}
```

### LCD readout (canvas)

The patch-name LCD and the small per-knob LCD readouts use the same
recipe at different sizes:

```js
// 1. Fill recessed background
ctx.fillStyle = getCssVar('--lcd-bg');
ctx.fillRect(0, 0, w, h);

// 2. Draw inner shadow (recessed feel) via gradient overlay
const innerShadow = ctx.createLinearGradient(0, 0, 0, h);
innerShadow.addColorStop(0, 'rgba(0,0,0,0.5)');
innerShadow.addColorStop(0.15, 'transparent');
ctx.fillStyle = innerShadow;
ctx.fillRect(0, 0, w, h);

// 3. Draw text with bloom — two-pass:
//    pass 1 = blurred glow underneath
ctx.font = `500 ${sizePx}px 'IBM Plex Mono', monospace`;
ctx.textAlign = 'center';
ctx.textBaseline = 'middle';
ctx.shadowColor = getCssVar('--lcd-text-glow');
ctx.shadowBlur = sizePx * 0.6;
ctx.fillStyle = getCssVar('--lcd-text-on');
ctx.fillText(value, cx, cy);
//    pass 2 = sharp text on top (resets shadow)
ctx.shadowBlur = 0;
ctx.fillText(value, cx, cy);
```

Result: glowing cyan-blue text on a deep navy background — reads as
"backlit LCD" without needing a separate font file.

### Level meter (canvas, blue LEDs)

```js
// Draw a row of N segments; first N×level are lit, the rest are dim.
// Last 1-2 segments use --led-on-warm for peak indication.
ctx.fillStyle = getCssVar('--led-meter-bg');
ctx.fillRect(0, 0, w, h);

const SEGMENTS = 20;
const litCount = Math.round(level * SEGMENTS);
for (let i = 0; i < SEGMENTS; i++) {
  const isLit = i < litCount;
  const isWarm = i >= SEGMENTS - 2;
  ctx.fillStyle = isLit
    ? (isWarm ? getCssVar('--led-on-warm') : getCssVar('--led-on'))
    : getCssVar('--led-dim');
  const x = (i / SEGMENTS) * w + segmentGap;
  ctx.fillRect(x, 2, w / SEGMENTS - segmentGap, h - 4);
}
```

### Envelope curve (canvas)

```js
// Compute ADSR shape from the 5 envelope param values.
// Draw a 1.5 px antialiased polyline on the LCD background.
ctx.fillStyle = getCssVar('--lcd-bg');
ctx.fillRect(0, 0, w, h);

ctx.strokeStyle = getCssVar('--lcd-text-on');
ctx.lineWidth = 1.5;
ctx.lineJoin = 'round';
ctx.shadowColor = getCssVar('--lcd-text-glow');
ctx.shadowBlur = 4;

ctx.beginPath();
// ...compute (x, y) points across the curve...
ctx.stroke();
```

### Algorithm mini diagram (canvas)

8 hard-coded operator-routing diagrams. The selected one is drawn in
`--lcd-text-on` on `--lcd-bg`; the unselected mini-thumbnails (if any)
in `--lcd-text-off`. Operator boxes are square; connection lines are
1 px antialiased.

---

## Reference screenshots

The visual reference images live in `docs/design/reference/` (not
committed — placed manually for design work):

- `rym2612-panelfront.jpg` — FM mode visual reference.
- `pcm2612-VST.jpg` — D mode visual reference.

These are **inspiration, not pixel-targets** — the v2 layout obeys
[ADR-0015](adr/0015-webview-backend-support.md)'s "functional parity,
not pixel parity" principle. Specific design choices that depart from
the references (e.g., header layout, brand wordmark) live in
[`08-ui-views.md`](08-ui-views.md).

---

## When to extend this doc

- Adding a new colour: add a CSS custom property to the palette section
  first; never hand-type a hex in a widget.
- Adding a new widget kind: add a CSS recipe section first; never copy a
  widget from elsewhere on the web without ensuring it follows the
  binding principles in [`05-ui-ux.md`](05-ui-ux.md).
- Adopting a new font weight or size: add a row to the typography table
  and bump the woff2 subset if needed.

If a v2 widget cannot be expressed with the recipes here, raise it as a
design issue before writing the widget — likely either the recipes need
a new variant, or the design intent is drifting and should be
re-examined.
