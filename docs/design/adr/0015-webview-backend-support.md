# ADR-0015: WebView backend support matrix & minimum versions

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [05-ui-ux.md](../05-ui-ux.md), [06-build-system.md](../06-build-system.md), [ADR-0001](0001-juce8-webview-ui.md), [ADR-0007](0007-fixed-window-size.md), [ADR-0016](0016-webview2-runtime-distribution.md), [ADR-0017](0017-hidpi-display-scaling.md)

## Context

[ADR-0001](0001-juce8-webview-ui.md) makes the UI an HTML/CSS/JS app rendered in
`juce::WebBrowserComponent`. That component is not one engine — JUCE 8 backs it
with a **different native browser engine on each platform**:

| Platform | Engine | Family |
|----------|--------|--------|
| Windows  | WebView2                    | Chromium / Edge |
| macOS    | WKWebView                   | WebKit |
| Linux    | WebKitGTK (`webkit2gtk-4.1`)| WebKit |

The design docs so far refer to "the WebView" as if it were uniform. A
cross-platform review raised the consequences this hides: there is no stated
minimum runtime version per platform, no position on whether the pixel-art UI
must render identically across one Chromium engine and two distinct WebKit
builds, the macOS deployment target (10.15 in
[06-build-system.md](../06-build-system.md)) is unverified against JUCE 8's
WebView integration, and Linux display-server behaviour (X11 vs Wayland) is
unaddressed.

## Decision

Adopt an explicit **WebView backend support matrix**.

**1. Functional parity is required; visual pixel-parity is a non-goal.**
On every supported platform the UI must be fully functional — every control,
relay, native function, resource-provider asset and telemetry event works — and
visually faithful to the Genny aesthetic ([genny-ui.md](../genny-ui.md)). The UI
is **not** required to be pixel-identical between engines. Minor differences in
canvas rasterisation, sub-pixel snapping, and font hinting between Chromium and
the two WebKit builds are accepted and are not treated as defects.

**2. Minimum runtime versions.**

| Platform | Minimum | Notes |
|----------|---------|-------|
| Windows | WebView2 **Evergreen** runtime | Auto-updating channel; presence is guaranteed by the installer — see [ADR-0016](0016-webview2-runtime-distribution.md). |
| macOS | Deployment target **10.15** (Catalina), pending verification | The `WKURLSchemeHandler`-based resource provider and `WKScriptMessageHandler`-based native integration JUCE 8 uses are available at 10.15; an implementation-time check on a Catalina target confirms this. Raise the target if any JUCE 8 WebView feature proves unavailable. |
| Linux | WebKitGTK on the **`webkit2gtk-4.1`** API line (libsoup3) | The exact minimum point release is pinned during implementation against the oldest targeted distro (Ubuntu 22.04 LTS / Debian 12). |

**3. Linux display server.** X11 is the supported baseline. Under Wayland the
plugin runs through XWayland (JUCE plugin-window embedding is X11-based); this is
documented as **best-effort** and is not separately QA'd for the MVP.

## Consequences

- A per-engine UI smoke test joins the implementation test plan: the UI is loaded
  and exercised on all three backends. The unit tests still cover only audio/patch
  logic (see [06-build-system.md](../06-build-system.md)).
- QA compares each platform's UI against the Genny aesthetic, **not** against the
  other platforms' output; cross-engine screenshot-diffing is explicitly not a
  release gate.
- The pixel-art rules in [05-ui-ux.md](../05-ui-ux.md) (`image-rendering: pixelated`,
  `imageSmoothingEnabled = false`) are verified to behave acceptably on each
  engine; where a WebKit build differs from Chromium, the per-engine result is
  judged on its own.
- The macOS deployment target carries an explicit verification item; it is not
  assumed correct.
- Wayland is a known best-effort area; a future ADR may revisit it if JUCE gains
  native Wayland plugin embedding.
- HiDPI/scaling, which interacts with each engine's `devicePixelRatio`, is
  decided separately in [ADR-0017](0017-hidpi-display-scaling.md).

## Alternatives considered

- **Bundle one consistent engine on all three platforms** (e.g. Chromium Embedded
  Framework) — rejected: a very large binary, abandons JUCE 8's native WebView
  integration (relays, resource provider, native functions), and a far heavier
  maintenance burden than the rendering differences it would erase.
- **Require pixel-perfect parity across engines** — rejected by project decision:
  the engines genuinely differ, and chasing identical rasterisation is
  disproportionate QA cost for a self-imposed pixel-art style that only needs to
  *look* right on each platform.
