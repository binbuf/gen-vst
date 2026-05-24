# Task 18 — Cross-platform bring-up, installer & CI

> **Depends on:** Task 01. *Recommended position:* after Task 17 — bring the
> plugin up on the other platforms once it is feature-complete.
> **Design references:** `docs/design/06-build-system.md` (primary —
> *Platform Targets*, *Distribution & Installers*, *GitHub Actions CI*,
> *CMakePresets.json*), ADR-0015, ADR-0016.

## Objective

Bring Gen VST up on **macOS and Linux**, add a **Windows installer** that
guarantees the WebView2 runtime, and stand up **CI** so all three platforms
build automatically.

## Context & key constraints

### macOS

- Formats **VST3 + AU**. Universal binary `arm64;x86_64` (set in the root
  CMake in Task 01). WebView backend is **WKWebView** — no extra dependency.
- Deployment target **10.15**: ADR-0015 makes this an **explicit verification
  item** — confirm JUCE 8's WebView native integration (the
  `WKURLSchemeHandler` resource provider and `WKScriptMessageHandler` native
  functions) actually works at 10.15; **raise the target** if any feature is
  unavailable.
- AU validation: `auval -v aumu Genv GnVs` must pass (subtype `Genv`,
  manufacturer `GnVs`). The AU `PLUGIN_MANUFACTURER_CODE` must be exactly one
  uppercase + three lowercase letters or Logic rejects it.

### Linux

- Format **VST3**. GCC 11+ / Clang 14+, `-fPIC`. WebView backend is
  **WebKitGTK** on the `webkit2gtk-4.1` API line.
- System `-dev` packages required (`06-build-system.md` *Linux*): `libasound2-dev`,
  `libx11-dev`, `libxcursor-dev`, `libxrandr-dev`, `libxinerama-dev`,
  `libfreetype6-dev`, `libgl-dev`, and **`libwebkit2gtk-4.1-dev`**.
- X11 is the supported baseline; Wayland runs via XWayland — **best-effort**,
  not separately QA'd (ADR-0015).

### Per-engine WebView smoke test

ADR-0015: **functional parity is required, visual pixel-parity is not.** On each
backend (WebView2 / WKWebView / WebKitGTK) load the UI and exercise every
control, relay, native function, resource-provider asset, and telemetry event.
The resource provider must serve correct `Content-Type` for all assets — the
WebKit backends reject `@font-face` files with a wrong MIME type where Chromium
is lenient, so verify fonts specifically on macOS and Linux.

### Windows installer

ADR-0016: Windows ships an **installer**, not loose bundles. It places the VST3
and Standalone, and bundles the **WebView2 Evergreen Bootstrapper**
(`MicrosoftEdgeWebView2Setup.exe`, ~2 MB) and runs it silently so a fresh
install always has a working WebView2 runtime. The installer tool (WiX, Inno
Setup, …) is an implementation choice. For offline installers the Fixed Version
runtime is the documented alternative, not the default.

### User data on install / uninstall

The plugin auto-creates the writable user roots
`<userAppData>/GenVst/patches/saved/` and `…/patches/imported/` on first
launch via idempotent `fs::create_directories` (see Task 14). The installer
**must not** pre-seed these — the runtime owns them. The installer **must
not** delete `<userAppData>/GenVst/` on uninstall: those folders hold user
content (saved patches, imported patches, persisted settings) and any
post-install custom roots may live under the same tree.

### CI

`.github/workflows/build.yml` with three platform jobs per `06-build-system.md`
*GitHub Actions CI*: each checks out submodules **recursively**, installs
**Node.js 20** (CMake drives the `ui/` build), configures with the platform
preset, builds, and uploads the plugin artifact. Release artifacts upload on
tag push (`on: push: tags: ['v*']`). macOS code signing needs an Apple
Developer certificate — defer to pre-release.

## Scope

- macOS build (VST3 + AU), deployment-target verification, `auval`.
- Linux build (VST3), system dependency documentation.
- Per-engine WebView functional smoke test on all three backends.
- The Windows installer bundling the WebView2 Evergreen Bootstrapper.
- The GitHub Actions 3-platform CI workflow + tag-based release.

## Out of scope

- A signed/notarized macOS `.pkg`; a Linux `.deb`/AppImage — post-MVP
  (ADR-0016).
- CLAP — post-MVP (ADR-0008).
- Native Wayland plugin embedding — best-effort only for the MVP (ADR-0015).

## Implementation steps

1. Configure and build on macOS; verify the 10.15 deployment target against
   JUCE 8 WebView; run `auval`.
2. Configure and build on Linux with the documented `-dev` packages.
3. Run the per-engine WebView smoke test on all three backends; fix any
   MIME-type/font issues surfaced on the WebKit backends.
4. Create the Windows installer (WiX or Inno Setup) bundling and silently
   running the WebView2 Evergreen Bootstrapper.
5. Add `.github/workflows/build.yml` with the three jobs + tag-release.

## Deliverables

`.github/workflows/build.yml`, the Windows installer project/config, updates to
`CMakeLists.txt` / `CMakePresets.json` as needed for macOS/Linux, and any
build-doc notes for the Linux system packages.

## Verification

1. The plugin **configures and builds** cleanly on Windows, macOS, and Linux.
2. macOS: `auval -v aumu Genv GnVs` passes; `pluginval --strictness-level 8`
   passes the VST3 and AU. The 10.15-target decision is verified and recorded.
3. Linux: `pluginval --strictness-level 8` passes the VST3.
4. Per-engine smoke test: on each of WebView2, WKWebView, WebKitGTK the UI
   loads, fonts render, all controls/relays/native functions/telemetry work.
   Differences are judged against the Genny aesthetic, not against each other.
5. The Windows installer installs the VST3 + Standalone on a **clean Windows VM
   that lacks the WebView2 runtime** — after install the plugin opens with a
   working WebView UI (the bootstrapper installed the runtime).
6. CI: all three jobs are green on a pushed branch; a pushed `v*` tag produces
   uploaded release artifacts.

## Done when

- [ ] Builds clean on Windows, macOS, and Linux.
- [ ] macOS AU passes `auval`; the 10.15 deployment target is verified.
- [ ] The UI passes the functional smoke test on all three WebView backends.
- [ ] The Windows installer guarantees a working WebView2 runtime on a clean VM.
- [ ] CI builds all three platforms and releases on tag.
