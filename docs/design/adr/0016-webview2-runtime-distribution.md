# ADR-0016: WebView2 runtime distribution & Windows installer

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [05-ui-ux.md](../05-ui-ux.md), [06-build-system.md](../06-build-system.md), [08-ui-views.md](../08-ui-views.md), [ADR-0001](0001-juce8-webview-ui.md), [ADR-0015](0015-webview-backend-support.md)

## Context

The Windows WebView backend ([ADR-0015](0015-webview-backend-support.md)) requires
the **WebView2 Runtime** to be installed on the machine. The auto-updating
"Evergreen" runtime ships with Windows 11 and recent Windows 10 builds, but its
presence is **not universal** — clean or managed Windows installs may lack it.
Without it, `juce::WebBrowserComponent` cannot initialise and the plugin shows no
UI. [05-ui-ux.md](../05-ui-ux.md) flagged this as an unresolved open question.

The project currently has **no installer** — [06-build-system.md](../06-build-system.md)
describes only raw VST3/AU/Standalone bundle artifacts that a user copies into
place. There is therefore no existing mechanism to ensure the runtime is present.

macOS needs no equivalent: WKWebView is part of the OS. Linux's WebKitGTK is a
system library installed via the distribution package manager.

## Decision

**Windows: ship an installer that guarantees the WebView2 runtime.**

The Windows distribution is an **installer**, not a loose bundle. It bundles the
**WebView2 Evergreen Bootstrapper** (`MicrosoftEdgeWebView2Setup.exe`, ~2 MB) and
runs it silently during installation. The bootstrapper detects an existing
runtime and installs or updates it only if needed; the installer then places the
VST3 (and the Standalone) into their correct locations. A fresh install therefore
always has a working WebView2 runtime.

**macOS: no runtime action.** WKWebView is part of macOS. macOS continues to ship
as raw VST3/AU bundles for the MVP; a signed `.pkg` installer is post-MVP.

**Linux: declare a package dependency.** WebKitGTK is a system library; the Linux
distribution declares it as a dependency rather than bundling it. Linux continues
to ship as raw VST3 bundles for the MVP; a `.deb`/AppImage that declares the
`libwebkit2gtk-4.1-0` runtime dependency is post-MVP.

**Runtime fallback (all platforms).** If the WebView still fails to initialise at
runtime — corrupt runtime, locked-down enterprise policy, unsupported OS — the
editor displays a **native fallback panel** (a plain `juce::Component`, not the
WebView) with a short explanatory message and the WebView2 runtime download URL.
The plugin never presents a blank window. The panel is specified in
[08-ui-views.md](../08-ui-views.md).

## Consequences

- Installer/packaging becomes a build-system concern — captured in a new
  *Distribution & Installers* section of [06-build-system.md](../06-build-system.md).
  The specific installer tool (e.g. WiX, Inno Setup) is an implementation choice
  left to that work.
- The Evergreen Bootstrapper needs network access at install time (it downloads
  the runtime). Offline installs are served by the Fixed Version runtime —
  documented as the fallback, not the default.
- Release CI gains a Windows installer artifact — a post-MVP refinement of the
  GitHub Actions workflow in [06-build-system.md](../06-build-system.md).
- The native fallback panel is a small additional editor code path, independent
  of the embedded web bundle.
- Resolves the WebView2-runtime open question in [05-ui-ux.md](../05-ui-ux.md).

## Alternatives considered

- **Fixed Version runtime** — bundle the full (~150 MB+) WebView2 runtime in the
  installer. No network needed at install time, but large, and it puts runtime
  updates on us. Kept as the documented option for offline installers; not the
  default.
- **No installer; document the runtime as a prerequisite** — rejected: a
  blank-UI plugin on any machine lacking the runtime is an unacceptable
  out-of-box experience.
- **Install the runtime from inside the plugin on first run** — rejected: a
  plugin must not launch a system installer from the host process; that belongs
  in an installer.
