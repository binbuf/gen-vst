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

### Operator badges

| Token | Hex | Use |
|---|---|---|
| `--op-badge-bg`        | `#2196f3` | Filled blue square background for op number badges 1..4 |
| `--op-badge-text`      | `#ffffff` | Numeral on the badge |
| `--op-badge-active-glow` | `rgba(33,150,243,0.55)` | Outer glow when this operator row owns the envelope-curve focus |

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
  background:
    /* subtle vertical brushed-metal striations — RYM2612 / PCM2612 idiom */
    repeating-linear-gradient(
      90deg,
      rgba(255,255,255,0.025) 0 1px,
      transparent 1px 3px
    ),
    /* main silver gradient */
    linear-gradient(
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

The brushed-metal striation layer is intentionally subtle (≈ 2 %
opacity); it lifts the panel from "flat coloured gradient" to
"physical surface" without competing with the controls on top. Both
RYM2612 and PCM2612 reference panels carry a similar vertical-grain
treatment.

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

The widget draws the ADSR polyline plus **segment labels** (`AR`, `DR`,
`SL`, `SR`, `RR`) at the midpoint of each segment and two dashed
vertical markers labelled `KEY ON` / `KEY OFF` at the columns where the
note-on and note-off transitions sit on the time axis. Labels and
markers share the polyline's `--lcd-text-on` colour and phosphor bloom,
matching the RYM2612 reference panel.

```js
// Compute ADSR shape from the 5 envelope param values.
// 1. Draw a 1.5 px antialiased polyline on the LCD background.
ctx.fillStyle = getCssVar('--lcd-bg');
ctx.fillRect(0, 0, w, h);

ctx.strokeStyle = getCssVar('--lcd-text-on');
ctx.lineWidth = 1.5;
ctx.lineJoin = 'round';
ctx.shadowColor = getCssVar('--lcd-text-glow');
ctx.shadowBlur = 4;

ctx.beginPath();
// ...compute (x, y) points across the AR, DR, SL, SR, RR segments...
ctx.stroke();

// 2. Segment labels — place each label at the midpoint of its segment,
//    clamped 4 px from the LCD edges. Labels use the same bloom recipe
//    as the patch-name LCD (two-pass text, shadowBlur ≈ 4 px).
ctx.font = `500 7px 'IBM Plex Mono', monospace`;
ctx.textAlign = 'center';
ctx.textBaseline = 'middle';
for (const seg of ['AR', 'DR', 'SL', 'SR', 'RR']) {
  const { x, y } = segmentLabelPosition(seg);
  ctx.shadowBlur = 4;
  ctx.fillText(seg, x, y);   // bloom pass
  ctx.shadowBlur = 0;
  ctx.fillText(seg, x, y);   // sharp pass
}

// 3. KEY ON / KEY OFF dashed vertical markers + caption.
ctx.setLineDash([2, 2]);
ctx.lineWidth = 0.6;
for (const mark of [keyOnX, keyOffX]) {
  ctx.beginPath();
  ctx.moveTo(mark, 4);
  ctx.lineTo(mark, h - 12);
  ctx.stroke();
}
ctx.setLineDash([]);
ctx.font = `500 6px 'IBM Plex Mono', monospace`;
ctx.fillText('KEY ON',  keyOnX,  h - 4);
ctx.fillText('KEY OFF', keyOffX, h - 4);
```

### Operator badge

The `[1] [2] [3] [4]` numeric badges in the FM operator grid. Each badge
is the click target that selects which operator the envelope curve
tracks, so it needs a visibly "active" state in addition to its resting
form.

```css
.op-badge {
  width: 22px;
  height: 22px;
  border-radius: 3px;
  background: var(--op-badge-bg);
  color: var(--op-badge-text);
  font: 500 12px/1 'IBM Plex Mono', monospace;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid var(--knob-rim);
  box-shadow:
    inset 1px 1px 0 rgba(255,255,255,0.18),
    inset -1px -1px 0 rgba(0,0,0,0.35),
    1px 1px 2px rgba(0,0,0,0.45);
  cursor: pointer;
  transition: box-shadow 120ms ease-out;
}
.op-badge.is-active {
  /* envelope-curve is currently tracking this operator */
  box-shadow:
    inset 1px 1px 0 rgba(255,255,255,0.25),
    inset -1px -1px 0 rgba(0,0,0,0.35),
    0 0 8px var(--op-badge-active-glow);
}
```

The blue-filled square matches the RYM2612 reference where the op
badges read as prominent indicators rather than ambient labels.

### Algorithm picker — `algo-grid` (CSS)

The FM panel exposes all 8 YM2612 algorithm topologies as visible
numbered buttons in a 2-column × 4-row grid. The selected algorithm
button carries `.is-active` (blue fill + outer glow), so the picker
reads at a glance without a popover.

```css
.algo-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 3px;
  padding: 3px;
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid var(--inset-edge-dark);
  border-radius: 3px;
  box-shadow: inset 1px 1px 2px rgba(0, 0, 0, 0.5);
}
.algo-grid .algo-btn {
  width: 22px;
  height: 22px;
  border-radius: 2px;
  font: 500 10px/1 'IBM Plex Mono', monospace;
  color: var(--btn-text);
  background: linear-gradient(180deg, var(--btn-bg-top), var(--btn-bg-bottom));
  border: 1px solid var(--knob-rim);
  box-shadow:
    1px 1px 2px rgba(0,0,0,0.4),
    inset 1px 1px 0 rgba(255,255,255,0.10);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}
.algo-grid .algo-btn.is-active {
  background: var(--btn-active-bg);
  box-shadow:
    inset 2px 2px 3px rgba(0,0,0,0.45),
    inset -1px -1px 0 rgba(255,255,255,0.10),
    0 0 6px rgba(33,150,243,0.45);
}
```

### Algorithm topology diagram (canvas)

A separate, *larger* LCD tile next to the `algo-grid` draws the topology
of the **currently selected** algorithm — operator boxes + arrows, sized
**roughly 2×** the picker buttons (target ≈ 112×112 px on the FM panel).
The widget is read-only; selection happens via the `algo-grid` above.
Lines are drawn in `--lcd-text-on` on `--lcd-bg`, 1.4 px antialiased
strokes with the standard LCD phosphor bloom. The 8 hard-coded routings
are stored as a small JS table mapping `algorithm_index → {boxes, lines}`.

### NOTE ON LED — paired with text label

```css
.note-on {
  display: inline-flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
}
.note-on .note-on-text {
  font: 500 7px/1 'IBM Plex Mono', monospace;
  letter-spacing: 0.20em;
  text-transform: uppercase;
  color: var(--label-text);
  opacity: 0.85;
}
.note-on-led {
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: radial-gradient(circle at 30% 30%, #ff8a8a, #b71c1c 70%, #5a0c0c 100%);
  border: 1px solid #050608;
  box-shadow:
    inset 1px 1px 1px rgba(255,255,255,0.35),
    inset -1px -1px 1px rgba(0,0,0,0.5),
    0 0 8px rgba(255,82,82,0.65);
}
.note-on-led.is-off {
  background: radial-gradient(circle at 30% 30%, #4a2828, #1a0808 80%);
  box-shadow:
    inset 1px 1px 1px rgba(255,255,255,0.05),
    inset -1px -1px 1px rgba(0,0,0,0.5);
}
```

The LED + `NOTE ON` text label sit together at the far-left of the
header. The 16 px LED is large enough to read across the panel; the
text label avoids the "what does the red dot mean?" ambiguity of an
unlabelled indicator. Mirrors the RYM2612 reference.

### MIDI wheel (`midi-wheel`) — PB / MW visualisation

The FM panel's `GLOBAL IN` block surfaces incoming pitch-bend and
modwheel MIDI values as **read-only vertical wheel widgets** rather
than LED meters, matching the RYM2612 reference's bottom-left PB / MW
block (which reads as hardware pitch-bend / mod-wheel controls).

```css
.midi-wheel {
  position: relative;
  width: 22px;
  height: 80px;
  background:
    linear-gradient(180deg, #1a1d22 0%, #2a2d32 50%, #1a1d22 100%);
  border: 1px solid var(--inset-edge-dark);
  border-radius: 3px;
  box-shadow:
    inset 2px 2px 4px rgba(0,0,0,0.7),
    inset -1px -1px 0 var(--inset-edge-light);
  overflow: hidden;
}
.midi-wheel .wheel-thumb {
  position: absolute;
  left: 1px; right: 1px;
  height: 6px;
  background: linear-gradient(180deg,
    rgba(255,255,255,0.25) 0%, rgba(120,130,145,0.95) 35%,
    rgba(60,70,85,0.95) 65%, rgba(0,0,0,0.4) 100%);
  /* `bottom: N%` is set inline by the binding layer based on the
   * current MIDI value (50% = center for PB; 0% = bottom for MW). */
}
.midi-wheel.midi-wheel-pb::before {
  /* PB variant draws a faint centerline so the user reads
   * "centered = 0 bend." */
  content: "";
  position: absolute; left: 2px; right: 2px; top: 50%;
  height: 1px;
  background: rgba(78,160,255,0.4);
}
```

The MW variant has no centerline (modwheel rests at 0 = bottom, not
center). Both widgets share the recess + thumb chrome.

### Stepper readout (`stepper-readout`) — LCD + ▲/▼ buttons

A horizontal cluster: LCD readout flanked by a small vertical pair of
▲/▼ buttons. Used for the `RETRIG RATE` value on the FM panel (where
the value is an integer 0–1023 better edited by stepping than by
dragging a continuous knob) and for the `POLY` / `RANGE` global
steppers in the LFO block.

```css
.stepper-readout {
  display: inline-flex; align-items: center; gap: 3px;
}
.stepper-readout .lcd { min-width: 52px; padding: 3px 6px; }
.stepper-readout .stepper-buttons {
  display: inline-flex; flex-direction: column; gap: 1px;
}
.stepper-readout .stepper-btn {
  width: 16px; height: 12px;
  border-radius: 2px;
  background: linear-gradient(180deg, var(--btn-bg-top), var(--btn-bg-bottom));
  border: 1px solid var(--knob-rim);
  color: var(--btn-text);
  font: 500 8px/1 'IBM Plex Mono', monospace;
}
```

### Connector overlay (`connector-overlay`) — routing decoration

The FM panel uses subtle blue connector lines to communicate fixed
routing relationships between controls — `OP1 FB` is wired to operator
1 only (a short vertical line beneath the knob); the `CH VOL` master
TL knob fans out to all 4 operator TL knobs (a vertical spine through
the TL column plus short horizontal stubs into each operator row). The
overlay is decoration only — no apvts param, no click handling — but
it earns its keep by making the hardware-emulation relationships read
at a glance, as on the RYM2612 reference panel.

```css
.connector-overlay {
  position: absolute; inset: 0;
  pointer-events: none;
  z-index: 0;
  width: 100%; height: 100%;
}
.connector-overlay line,
.connector-overlay path {
  stroke: var(--lcd-text-on);
  stroke-width: 1.2;
  vector-effect: non-scaling-stroke;  /* lines don't fatten with viewBox */
  fill: none;
  opacity: 0.5;
}
.connector-overlay .connector-junction {
  fill: var(--lcd-text-on);
  opacity: 0.7;
}
```

Inside the FM panel, the SVG uses a pixel-coordinate viewBox sized to
match the parent block's natural width × height (e.g.
`viewBox="0 0 1000 270"`), so line endpoints can be specified at the
column / row centers that the operator grid produces. The `OP1 FB`
short connector is implemented as a CSS `::after` pseudo-element on
the OP1 FB knob (no SVG needed — single short vertical line).

### Level meter — `level-meter-mini` modifier (header)

The persistent header carries stacked L/R output meters using a thinner
variant of the canonical `.level-meter`. Segments are 3 × 5 px (vs.
4 × 8 px on the full-fat meter), gap is 1 px, padding 2 px — two rows
fit in ~24 px of header height.

```css
.level-meter.level-meter-mini {
  padding: 2px;
  gap: 1px;
}
.level-meter.level-meter-mini .seg {
  width: 3px;
  height: 5px;
  border-radius: 0;
}
```

The full-fat `.level-meter` recipe (above) stays in use on the D-mode
panel where the meters are the loudest visual element.

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
