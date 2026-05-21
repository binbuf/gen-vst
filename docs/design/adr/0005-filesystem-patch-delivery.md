# ADR-0005: Deliver factory patches via install-time filesystem copy

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [04-patch-system.md](../04-patch-system.md), [06-build-system.md](../06-build-system.md), [ADR-0004](0004-furnace-only-factory-bank.md)

## Context

The factory bank (see [ADR-0004](0004-furnace-only-factory-bank.md)) must reach
a location the plugin can read at runtime. Two delivery mechanisms are
available: embedding the data into the binary with `juce_add_binary_data`, or
copying loose files to a runtime location at build/install time.

Each `.tfi` file is tiny (~42 bytes), but `juce_add_binary_data` generates
per-file C++, scales poorly as the patch set grows, and forces a recompile
whenever the patch set changes.

## Decision

Factory patches are **copied to a runtime location at build/install time** and
loaded from the filesystem. They are **not** embedded via `juce_add_binary_data`.

- **VST3 / AU:** CMake stages the top-level factory `.tfi` files into a clean
  build folder, then `juce_add_bundle_resources_directory` copies that tree into
  the bundle's `Contents/Resources/patches/`.
- **Standalone:** a CMake `install()` rule copies the factory patches into a
  platform data directory.

## Consequences

- A `GENVST_STANDALONE_PATCH_DIR` CMake variable must be defined per platform
  (it is referenced in `06-build-system.md` but not yet defined there — this is
  an implementation gap to close).
- The plugin performs filesystem reads at startup to load the factory root.
- Changing the patch set does not trigger a C++ recompile.
- Staging through a clean directory is required because
  `juce_add_bundle_resources_directory` copies a whole directory tree — staging
  guarantees only factory files (not the gitignored `extra/` set) are bundled.
- The embedded web UI bundle is still delivered via `juce_add_binary_data`; this
  ADR concerns patch data only.

## Alternatives considered

- **`juce_add_binary_data` embedding** — rejected: per-file generated C++,
  poor scaling, and a forced recompile on every patch-set change.
