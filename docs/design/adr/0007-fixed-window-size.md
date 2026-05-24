# ADR-0007: Fixed 960x640 plugin window for MVP

- **Status:** Superseded by [ADR-0023](0023-fixed-window-1200x560.md) (2026-05-24). Was Accepted 2026-05-23 (revised from 960x560, 2026-05-21).
- **Date:** 2026-05-23
- **Related:** [ADR-0023](0023-fixed-window-1200x560.md), [ADR-0001](0001-juce8-webview-ui.md)
- **Historical note:** The 960×640 window was sized for the v1 pixel-art chassis; v2 (modern aesthetic, RYM2612-style layout) uses 1200×560. The v1 UI design documents are archived under `docs/design/archive/`.

## Context

The pixel-art UI ([ADR-0001](0001-juce8-webview-ui.md)) is authored on a 1x
pixel grid with `image-rendering: pixelated` and an 8px base grid. Fractional
window scaling breaks pixel-snapping.

The initial size choice was 960x560 (resolving an earlier 900x600 conflict in
the docs). After Task 16's MVP and the polish pass, hands-on QA found:

- The center column's polyphony group (POLY / GLIDE / SPREAD pills + slider)
  pushed the middle row's required height to ~279 px, but the row was only
  260 px tall → visible clipping below the SPREAD row.
- The SQ section's noise panel (the tallest of the four PSG panels — TYPE,
  RATE, AUTO rows in addition to the standard VOL/PAN/MIDI) pushed close to
  the 220 px bottom-region budget. With panel padding + section header it
  spilled past the chassis edge.
- The user confirmed during planning that growing the window is preferred to
  squeezing the layouts further.

## Decision

The plugin window is **fixed at 960x640 px** for the MVP, with no user
resizing. The WebView still scales only by **integer** factors (1x/2x/3x) on
HiDPI displays — see [ADR-0017](0017-hidpi-display-scaling.md). The grid
breakdown is now:

| Row        | Height | Was   | Used by                                |
|------------|--------|-------|----------------------------------------|
| Header     | 80 px  | 80 px | Wordmark, VU/scope, patch display, gear|
| Middle     | 280 px | 260 px| LFO/ALG, Instruments+routing, Presets  |
| Bottom     | 280 px | 220 px| FM operator panels / SQ panels / D     |

The 80 + 280 + 280 = 640 split keeps the 8 px base grid clean. The header
keeps its original 80 px; the additional 80 px is split evenly between the
middle row (room for the polyphony group) and the bottom row (room for SQ's
noise panel without compression).

## Consequences

- All UI layout is authored against a fixed 960x640 grid.
- HiDPI correctness still relies on integer WebView scaling, not layout
  reflow ([ADR-0017](0017-hidpi-display-scaling.md)).
- The operator panels' ADSR canvas, sliders, and knob row can absorb the
  bottom-region growth in a follow-up polish pass — the immediate fix is
  just to stop the clip.
- The DAW will accept the larger editor without code changes (the
  `setSize(960, 640)` in `GenVstAudioProcessorEditor`'s constructor is the
  authoritative size).
- A resizable window remains deferred to post-MVP and would be a follow-up
  ADR once the layout is stable.

## Alternatives considered

- **Tighten layouts inside 960x560** — would require removing the NOTE
  readout from SQ tone panels, shrinking knobs again (we just bumped them
  during the polish pass), or condensing the polyphony group inline. Rejected
  as more invasive and contrary to the polish-pass direction.
- **960x600** (smaller bump) — fits SQ but leaves the center column +19 px
  over. Rejected as still tight.
- **Resizable window** — deferred: reflowing a pixel-snapped, integer-grid
  layout is non-trivial and not worth it before the layout is settled.
