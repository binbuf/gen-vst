# ADR-0001: Use JUCE 8 WebView for the plugin UI

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [05-ui-ux.md](../05-ui-ux.md), [06-build-system.md](../06-build-system.md), [ADR-0007](0007-fixed-window-size.md)

## Context

The target aesthetic (`docs/genny-ui.md`) is a pixel-art skeuomorphic late-80s
rackmount FM synth: custom knobs, green-LCD panels, segment displays, and
live-drawn envelope/algorithm graphics. Building and iterating that in C++
`juce::Graphics` with a native `LookAndFeel_V4` is slow — every visual tweak
needs a recompile.

JUCE 8.0.4 ships a production-ready `juce::WebBrowserComponent` with first-class
two-way binding between HTML controls and the `AudioProcessorValueTreeState`,
plus a C++-to-JS event channel.

## Decision

The Gen VST interface is an **HTML/CSS/JS application rendered in a WebView**
hosted by `juce::WebBrowserComponent`. The earlier native `LookAndFeel_V4`
design is **abandoned and superseded**.

Frontend stack: vanilla JavaScript + Canvas 2D, bundled by Vite. No UI
framework — the interface is mostly custom canvas-drawn widgets.

## Consequences

- Build system must be updated to match (see [06-build-system.md](../06-build-system.md),
  which still reflects the abandoned native design):
  - `JUCE_WEB_BROWSER=1` (currently `0`)
  - link `juce::juce_gui_extra`
  - `NEEDS_WEBVIEW2 TRUE`
  - `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` (currently `FALSE` — the HTML UI has
    text/numeric inputs)
  - remove `GenVstLookAndFeel.h/cpp` from the source list
  - `juce_add_binary_data` embeds the zipped Vite production bundle
- New runtime dependencies: WebView2 runtime on Windows, WebKitGTK on Linux.
- Larger plugin bundle (embedded web assets).
- UI and audio code live in two languages.
- Hot-reload dev workflow: the UI can be edited live against a running plugin
  via the Vite dev server.

These tradeoffs are accepted for the iteration speed and visual ceiling.

## Alternatives considered

- **Native `LookAndFeel_V4`** — rejected: slow iteration, no hot reload, and
  the canvas-heavy pixel-art widgets are far more work in `juce::Graphics`.
- **Foleys GUI Magic** — rejected: still a native-rendering path; does not solve
  the iteration-speed problem.
