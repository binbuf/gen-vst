# ADR-0017: HiDPI / display scaling across platforms

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [05-ui-ux.md](../05-ui-ux.md), [08-ui-views.md](../08-ui-views.md), [ADR-0001](0001-juce8-webview-ui.md), [ADR-0007](0007-fixed-window-size.md), [ADR-0015](0015-webview-backend-support.md)

## Context

[ADR-0007](0007-fixed-window-size.md) fixes the plugin window at 960×560 and
states the WebView scales only by **integer** factors, because the pixel-art UI
is authored on a 1× grid and fractional scaling breaks pixel-snapping. It left
*how the UI behaves on a display whose scale factor is fractional* as an open
question (also [05-ui-ux.md](../05-ui-ux.md) Open Question #1).

This is genuinely cross-platform: the three OSes report display scale
differently. Windows per-monitor DPI is routinely fractional (125%, 150%, 175%
are common laptop defaults); macOS uses a clean integer 2× backing scale on
Retina; Linux scaling varies by desktop environment and differs between X11 and
Wayland. A pixel-art UI with no defined fractional-DPI behaviour has, in effect,
undefined appearance on a default Windows laptop.

## Decision

**Integer scale presets, snapped to the nearest integer on fractional displays.**

- The UI offers explicit **1× / 2× / 3×** scale presets.
- On a display whose effective scale factor is fractional, the UI renders at the
  **nearest integer scale** — it never scales fractionally. Pixel-art crispness
  is preserved; the window's physical size may differ slightly from what the OS
  scale factor would nominally imply.
- On first open, the UI picks the integer preset nearest the display's reported
  scale. The user can override via the preset control; the choice is persisted
  in plugin state.
- The web content is authored at logical 1× (the [05-ui-ux.md](../05-ui-ux.md)
  pixel-art rules); the selected preset is applied as a whole-window integer zoom.

## Consequences

- **Extends** [ADR-0007](0007-fixed-window-size.md); ADR-0007 remains in force
  (960×560 base size, integer-only scaling) and is **not** superseded.
- Resolves [05-ui-ux.md](../05-ui-ux.md) Open Question #1.
- On a 150% Windows display the plugin renders at 1× or 2× (whichever is nearer),
  so it appears somewhat smaller or larger than a fractionally-scaled native app.
  This is an accepted trade-off for crisp pixel art.
- The scale preset is a user-facing control in the settings surface specified in
  [08-ui-views.md](../08-ui-views.md).
- Window *resizing* remains out of scope (ADR-0007); only discrete integer scale
  presets are offered.
- Each engine reports `devicePixelRatio` differently
  ([ADR-0015](0015-webview-backend-support.md)); the nearest-integer rule is
  applied uniformly on top of whatever the backend reports.

## Alternatives considered

- **Fractional scaling to match the OS exactly** — rejected: non-integer scale
  factors blur the pixel art and defeat the entire aesthetic
  ([ADR-0001](0001-juce8-webview-ui.md), [ADR-0007](0007-fixed-window-size.md)).
- **Leave it deferred** — rejected: the cross-platform review flagged undefined
  behaviour on mainstream fractional-DPI Windows displays as a real gap that must
  be closed before implementation.
