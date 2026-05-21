# ADR-0003: License the project under GPL v3

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [04-patch-system.md](../04-patch-system.md), [06-build-system.md](../06-build-system.md)

## Context

Gen VST is an open-source project built on JUCE. JUCE's free tier requires that
distributed software using it be released under the GPL (v3). A commercial JUCE
license would lift that requirement but carries a recurring cost.

## Decision

The project is licensed under **GPL v3**, using the **JUCE free tier**.

## Consequences

- Every bundled dependency must be GPL-v3-compatible:
  - ymfm — BSD-3-Clause — compatible.
  - Furnace `tfilib` factory bank — GPL — compatible.
  - SN76489 library — license must be verified before commit
    (see [ADR-0009](0009-sn76489-library.md)).
  - `clap-juce-extensions` and the CLAP SDK — MIT — compatible
    (see [ADR-0008](0008-clap-post-mvp.md)).
- Any factory patch bank or asset added later must carry a clear,
  GPL-compatible license before it can be committed or shipped.
- The plugin binary, including the embedded web UI bundle, is distributed under
  GPL v3.

## Alternatives considered

- **JUCE commercial / paid license** — rejected: recurring cost with no benefit
  for an open-source project that is content to be GPL.
