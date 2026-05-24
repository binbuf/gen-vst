# ADR-0023: Fixed 1200×560 plugin window

- **Status:** Accepted
- **Date:** 2026-05-24
- **Supersedes:** [ADR-0007](0007-fixed-window-size.md)
- **Related:** [ADR-0017](0017-hidpi-display-scaling.md), [ADR-0021](0021-three-mode-single-engine-ui.md), [ADR-0022](0022-modern-vst-aesthetic.md)

## Context

ADR-0007 fixed the v1 window at **960×640** — sized for a four-panel
operator row sitting under a three-column middle row with a header. The v2
layout (modelled on RYM2612) is **wider and shallower**: a single row of
four operator strips arranged as a grid (rows × parameter columns), a thin
header carrying the patch-name LCD + master controls, and a left-side global
column. 960 px is too narrow to fit the RYM2612-style operator grid with
breathing room; 640 px of height wastes vertical space.

The RYM2612 panel itself runs at roughly 750×280 in its source artwork, but
that is too small for a modern VST window on contemporary displays. The v2
target is **1200×560** — landscape aspect (≈ 15:7), large enough that every
control is comfortably clickable at 1× scale, small enough to fit on a
laptop screen and still leave DAW chrome visible.

## Decision

The v2 plugin window is **fixed at 1200×560 logical pixels**. The window is
not resizable for the foreseeable future; the layout is authored at this
specific size.

[ADR-0017](0017-hidpi-display-scaling.md) (HiDPI scaling presets) continues
to apply — the user can pick 1×/2× scale presets — but the integer-snap
rationale tied to pixel-art crispness is no longer binding (see
[ADR-0022](0022-modern-vst-aesthetic.md)); fractional scales are now visually
acceptable. The presets stay integer for predictability, not because the
artwork requires it.

## Consequences

- `EditorMinWidth = EditorMaxWidth = 1200`; `EditorMinHeight = EditorMaxHeight = 560`.
- The Vite-built web bundle assumes a 1200×560 viewport.
- The v1 layout in archived `v1-08-ui-views.md` and `v1-genny-ui.md` does
  not port one-to-one — the new view catalog (`08-ui-views.md`, v2) lays out
  each mode against the 1200×560 canvas from scratch.
- The WebView fallback panel (the v1 view 9, native non-WebView surface
  shown when WebView2 fails to initialise) resizes to 1200×560 as well.
- A future "resizable window" feature would lift this ADR; not in v2 scope.

## Alternatives considered

- **Keep 960×640** — preserves continuity with v1 chassis dimensions, but
  the RYM2612-style operator grid won't fit horizontally without cramming
  the labels. Rejected.
- **Match RYM2612's literal artwork size (~750×280)** — too small for a
  contemporary VST window; users would zoom to 2× immediately. Rejected.
- **1400×720 or larger** — comfortable, but oversized on 13-inch laptop
  screens where DAW chrome already eats 100–150 px on each axis. Rejected
  in favour of the 1200×560 middle ground.
- **Resizable from day one** — adds layout-engine work (scaling rules,
  reflow, per-widget minimum sizes) that's tangential to the v2 visual
  rework. Deferred; can be added later without superseding this ADR.
