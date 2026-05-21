# ADR-0008: Support CLAP as a post-MVP build target

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [06-build-system.md](../06-build-system.md), [ADR-0001](0001-juce8-webview-ui.md), [ADR-0003](0003-gpl-v3-license.md)

## Context

CLAP (CLever Audio Plugin) is an open, MIT-licensed plugin format with growing
host support (Reaper, Bitwig, FL Studio). JUCE has **no native CLAP exporter**,
but `clap-juce-extensions` (free-audio / Surge Synth Team, MIT-licensed) wraps
an existing JUCE `AudioProcessor` to emit a CLAP artifact alongside VST3/AU.
Surge XT ships its CLAP build this way.

Adding CLAP as a build target with this extension is small — roughly a
CMake `clap_juce_extensions_plugin()` call plus a CLAP plugin ID, on the order
of half a day, with the existing WebView editor working unchanged. However, the
distinctive CLAP capabilities — per-note expression, polyphonic parameter
modulation, non-destructive parameter mod — are **not** gained automatically;
they require real work in the voice allocator. A CLAP built with no extra effort
simply behaves like the VST3.

## Decision

CLAP is a **planned post-MVP build target**, added via `clap-juce-extensions`.
The MVP ships **VST3, AU, and Standalone only**. The build system is designed so
that adding CLAP later is a non-disruptive addition, not a restructuring.

`06-build-system.md` should carry a short "Post-MVP: CLAP via
clap-juce-extensions" note so the build is not accidentally structured in a way
that boxes CLAP out.

## Consequences

- MVP scope and CI surface stay at three formats; CLAP adds a fourth artifact
  and per-platform CI/test cost when it lands.
- `clap-juce-extensions` (MIT) and the CLAP SDK (MIT) are compatible with the
  project's GPL v3 license (see [ADR-0003](0003-gpl-v3-license.md)).
- A basic CLAP build mirrors VST3 behaviour. Leveraging CLAP-native features
  (per-note expression, poly modulation) is a separate, larger effort and would
  warrant its own ADR.
- The WebView editor ([ADR-0001](0001-juce8-webview-ui.md)) is unaffected —
  `clap-juce-extensions` supports custom JUCE editors.

## Alternatives considered

- **CLAP in the MVP** — rejected: adds dependency, a fourth artifact, and CI
  burden for no MVP user-facing benefit, since a no-effort CLAP just duplicates
  the VST3.
- **Never support CLAP** — rejected: format adoption is growing and the
  incremental cost via `clap-juce-extensions` is low; deliberately keeping the
  door open is cheap insurance.
