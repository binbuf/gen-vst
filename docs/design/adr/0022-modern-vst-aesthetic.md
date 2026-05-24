# ADR-0022: Modern hardware-VST aesthetic

- **Status:** Accepted
- **Date:** 2026-05-24
- **Supersedes:** The pixel-art discipline in [ADR-0001](0001-juce8-webview-ui.md)'s consequences and the v1 `genny-ui.md` (archived as `archive/v1-genny-ui.md`)
- **Related:** [ADR-0021](0021-three-mode-single-engine-ui.md), [ADR-0023](0023-fixed-window-1200x560.md), `docs/design/09-visual-spec.md`

## Context

The v1 visual direction was a **pixel-art skeuomorphic 1991 rackmount synth**:
1× authoring grid, `image-rendering: pixelated`, no `border-radius`, hard 1px
bevels, ordered-dither shading, bitmap fonts, no smooth gradients or blurred
shadows. It was a coherent style and shipped working code, but the v2 pivot
toward a clean **RYM2612-equivalent** UI is incompatible with that discipline:
RYM2612 uses smooth gradients on its chassis, antialiased rotary knobs with a
glossy gradient body, soft drop shadows, and an LCD display rendered with
phosphor-bloom blur — none of which the v1 rules allow.

The user specified the new direction during the 2026-05-24 design pivot:
**modern VST hardware aesthetic** — gradients, layered shadows, monospace
labels with wide letter-spacing, depressed-on-click button feel,
brushed-metal chassis, glowing LCDs.

## Decision

The v2 UI adopts a **modern hardware-VST aesthetic**, modelled directly on
Inphonik's RYM2612. The v1 pixel-art rules are retired wholesale.

**Visual principles** (binding for all v2 UI work):

1. **Consistent light source — top-left.** Every shadow and bevel honors this
   single light direction. No "let the artist choose per widget."
2. **Layered shadows for depth.** A chassis or recessed inset uses 2–4 stacked
   `box-shadow`s: a tight dark inner shadow + a wider soft outer shadow +
   optionally a 1px light highlight on the lit edge. No single hard 1px
   bevel; no blurry single-shadow "drop shadow" effect on its own.
3. **Gradients on physical surfaces.** Knob bodies, button caps, the chassis
   itself, and slider thumbs use `linear-gradient` to mimic plastic or
   brushed metal. Flat fills are reserved for LCDs (where a flat dark base
   reads as "screen") and pure-text labels.
4. **Press feedback = scale + inset shadow.** Buttons depress on
   `:active` via a `transform: scale(0.97)` plus an `inset` `box-shadow`,
   paired with a short `transition` (≤120 ms) so the motion feels weighted
   rather than instantaneous.
5. **Typography: monospace labels with letter-spacing.** Labels (`AR`, `DR`,
   `MUL`, `FREQ`, `ALGORITHM`, etc.) render in a monospaced font with wide
   `letter-spacing` (≈ 0.1–0.2 em) to evoke pro-audio / studio-gear
   typography. LCD displays use a dedicated LCD-style typeface; the patch
   name in the header is the most prominent example.
6. **Antialiasing is on.** `image-rendering` is the browser default
   (smooth). Canvas widgets enable `imageSmoothingEnabled = true` and draw
   with antialiased strokes.
7. **`border-radius` is allowed**, but used sparingly — a 2–4 px radius on
   buttons and pill toggles, a slightly larger radius on chassis panels.
   Pure-square corners remain idiomatic for LCD insets.
8. **Smooth gradients allowed; ordered dithering removed.** Bayer dithering
   is a pixel-art-specific technique and is not used.

**Palette and exact dimensions** are specified in
`docs/design/09-visual-spec.md` and are not duplicated here — the visual spec
is the single authoritative source for hex codes, gradient stops, shadow
recipes, and per-widget renderings.

**ADR-0001's WebView choice is unchanged.** Only the visual discipline
authored on top of that WebView is replaced.

## Consequences

- All v1 widget code in `ui/src/widgets/` is **deleted** (Task v2/01); the v2
  widget library is built from scratch against the new principles.
- The `ui/src/styles/` files (`chassis.css`, `design-system.css`,
  `sections.css`, `modals.css`, `gallery.css`) are likewise replaced. The
  v2 design system lives in a new `design-system.css` with CSS custom
  properties for the palette, typography, gradients, and shadow recipes.
- The Press Start 2P and torinak 7-segment fonts in `extern/fonts/` are
  retired from the active build. A monospace family for labels and an
  LCD-style face for displays are introduced; exact choices are recorded in
  `09-visual-spec.md`.
- The 5×7 dot-matrix canvas renderer used for the v1 LED readouts is
  retired. v2 LCD readouts use the LCD-style typeface directly.
- Canvas widgets that previously snapped to integer pixel coordinates no
  longer need to — antialiased strokes and subpixel positioning are
  acceptable everywhere.
- Display scaling (ADR-0017) still applies, but the "integer-only scale"
  rationale (preserving pixel grid integrity) is gone. Fractional scales
  are visually acceptable now; the integer-snap behaviour can be relaxed in
  a follow-up if desired.

## Alternatives considered

- **Keep the pixel-art rules and just freshen the palette** — rejected. The
  v1 style is internally coherent but reads as "retro chiptune toy" rather
  than "professional Genesis FM instrument." Producers who reach for
  RYM2612 do so partly because it doesn't look like a Game Boy app.
- **Hybrid: modern chassis, pixel-art widgets** — rejected. The two
  disciplines fight each other (one demands antialiased gradients, the other
  forbids them); the inconsistency is more jarring than committing fully to
  either.
- **Material Design / iOS HIG aesthetic** — rejected. Modern OS design
  systems are flat-ish and "screen-native" — they look like an app, not an
  instrument. Hardware-VST aesthetic (chunky knobs, LCD insets, brushed
  metal) is the convention for synth plugins and is what users expect.
