# ADR-0028: Wire up CLAP as a shipped build target

- **Status:** Accepted
- **Date:** 2026-05-28
- **Supersedes:** [ADR-0008](0008-clap-post-mvp.md)
- **Related:** [06-build-system.md](../06-build-system.md), [ADR-0001](0001-juce8-webview-ui.md), [ADR-0003](0003-gpl-v3-license.md), [ADR-0005](0005-filesystem-patch-delivery.md)

## Context

[ADR-0008](0008-clap-post-mvp.md) deferred CLAP to post-MVP but required the
build to stay structured so CLAP could be added without restructuring. The MVP
has shipped (VST3 + AU + Standalone). CLAP host adoption keeps growing (Reaper,
Bitwig, FL Studio), and the addition is now worth making.

JUCE has no native CLAP exporter. `clap-juce-extensions` (free-audio / Surge
Synth Team, MIT) wraps the existing JUCE `AudioProcessor` to emit a CLAP artifact
alongside the JUCE formats, reusing the same parameters, state, and WebView
editor.

## Decision

CLAP is a **shipped build target**, built by CI on Windows, macOS, and Linux and
included in every installer.

- **Integration:** `clap-juce-extensions` is fetched via CMake `FetchContent`,
  pinned to the **`main` commit `e8de9e8`** (2026-04-24) — *not* a release tag.
  The latest tag (`0.26.0`) predates JUCE's `getPosition()` `AudioPlayHead` API
  and leaves the wrapper abstract under JUCE 8 (it does not compile). The pinned
  commit implements `getPosition()` (`JUCE_VERSION`-guarded) and also carries the
  Windows embedded-WebView keyboard-input fix (PR #175), which our WebView editor
  needs. It carries the CLAP SDK and `clap-helpers` as nested git submodules, so
  the fetch is recursive (`GIT_SUBMODULES_RECURSE TRUE`) and **not** shallow — a
  shallow parent clone cannot resolve the submodules' pinned commits. A single
  `clap_juce_extensions_plugin(TARGET GenVst …)` call produces the `GenVst_CLAP`
  target.
- **Identity:** `CLAP_ID` is `com.genvst.genvst` (matching `BUNDLE_ID`) and must
  stay stable forever — hosts key state and preset discovery on it.
  `CLAP_FEATURES` is `instrument synthesizer stereo`.
- **Factory patches:** the macOS `.clap` is a bundle and carries its patches in
  `Contents/Resources/patches/` like the other formats (added to the POST_BUILD
  staging loop). The Windows/Linux `.clap` is a **single file** with no bundle to
  walk, so `resolveFactoryRoot` ([ADR-0005](0005-filesystem-patch-delivery.md))
  gains a fallback to `GENVST_STANDALONE_PATCH_DIR` — the user-data directory the
  installers already populate. The fallback is safe by construction: every bundle
  format satisfies the upward walk and returns first, so it only ever triggers
  for the single-file CLAP.
- **Install locations:** Windows `C:\Program Files\Common Files\CLAP`; macOS
  `/Library/Audio/Plug-Ins/CLAP`; Linux `~/.clap` (via the tarball `install.sh`).
- **Validation:** CI runs `clap-validator` as a soft gate (pluginval does not
  support CLAP).

## Consequences

- A fourth artifact is built and packaged on every platform; CI gains a
  `clap-juce-extensions` fetch (plus its submodules) and a `clap-validator` step.
- `clap-juce-extensions` (MIT) and the CLAP SDK (MIT) are compatible with the
  project's GPL v3 license ([ADR-0003](0003-gpl-v3-license.md)).
- This CLAP mirrors the VST3 behaviour. CLAP-native features — per-note
  expression, polyphonic parameter modulation — are **not** gained
  automatically; they remain a separate, larger effort that would warrant its
  own ADR.
- The WebView editor ([ADR-0001](0001-juce8-webview-ui.md)) is unaffected —
  `clap-juce-extensions` supports custom JUCE editors.
- The pin is an unreleased `main` commit (no JUCE-8 tag exists upstream yet). It
  is reproducible via the full SHA; revisit and move to a tag once upstream cuts
  a JUCE-8-compatible release.
- Linking `clap_juce_extensions` into the shared `GenVst` target changed the
  include graph so `PluginEditor.cpp`'s Win32 DPI helpers no longer received
  `<windows.h>` transitively (via the WebView2 SDK headers). Fixed by including
  `<windows.h>` explicitly under `#if JUCE_WINDOWS` — a robustness improvement
  that benefits every format, not just CLAP.

## Alternatives considered

- **Keep CLAP deferred (the ADR-0008 status quo)** — rejected: host adoption is
  now broad enough that the low incremental cost is worth paying.
- **Vendor `clap-juce-extensions` as a git submodule** instead of FetchContent —
  viable and the upstream-documented path; kept as the fallback if a CI runner's
  CMake fails to fetch the nested submodules, but FetchContent matches the
  existing JUCE pattern and keeps the dependency out of the repo tree.
- **Build CLAP on Windows/macOS only** — rejected: CLAP has strong Linux host
  support (Bitwig, Reaper), and shipping it in the Linux tarball is nearly free.
