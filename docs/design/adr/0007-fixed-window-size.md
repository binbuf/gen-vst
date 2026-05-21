# ADR-0007: Fixed 960x560 plugin window for MVP

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [05-ui-ux.md](../05-ui-ux.md), [07-feature-spec.md](../07-feature-spec.md), [ADR-0001](0001-juce8-webview-ui.md)

## Context

The pixel-art UI ([ADR-0001](0001-juce8-webview-ui.md)) is authored on a 1x
pixel grid with `image-rendering: pixelated` and an 8px base grid. Fractional
window scaling breaks pixel-snapping. The design docs disagree on the exact
size: `05-ui-ux.md` and the project notes say 960x560, while an open question
in `07-feature-spec.md` says 900x600.

## Decision

The plugin window is **fixed at 960x560 px** for the MVP, with no user resizing.
The WebView scales only by **integer** factors (1x/2x/3x) on HiDPI displays.

This resolves the discrepancy in favour of 960x560; the `07-feature-spec.md`
open question citing 900x600 is **superseded** and should be removed.

## Consequences

- All UI layout is authored against a fixed 960x560 grid.
- HiDPI correctness relies on integer WebView scaling, not layout reflow.
- A resizable window is deferred to post-MVP and would be a follow-up ADR once
  the layout is stable.

## Alternatives considered

- **Resizable window** — deferred: reflowing a pixel-snapped, integer-grid
  layout is non-trivial and not worth it before the layout is settled.
