# ADR-0006: Folder-tree patch browser instead of flat banks

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [04-patch-system.md](../04-patch-system.md), [07-feature-spec.md](../07-feature-spec.md)

## Context

The patch browser must serve both the small factory bank (~39 files) and very
large user collections — a single custom folder can be a deeply nested tree of
tens of thousands of `.tfi` files. A flat bank/patch list does not scale to
that, and an eager full scan at startup would stall the UI.

## Decision

The patch browser is **folder-tree based**, organised around **patch roots**:

- **Factory root** — bundled patches; read-only, always present, auto-loaded.
- **User root** — `<userAppData>/GenVst/patches/`; writable.
- **Custom roots** — any number of user-registered folders, paths persisted in
  plugin state and re-scanned on next launch.

Each root's full subdirectory structure is preserved and navigable. Scanning is
**lazy** (a folder's contents are read only when its node is first expanded);
the name-search index is built on a **background thread** after startup.

## Consequences

- `07-feature-spec.md` must be updated: its parity checklist item "Patch browser
  with bank/patch list" and its state-XML example using `bankName` / `patchIndex`
  reflect the older flat-bank model and should be revised to the roots model.
- State persistence stores the list of custom root paths, plus enough
  information to re-select the active patch by path on project reload.
- File enumeration and parsing always run on the message thread, never the
  audio thread.

## Alternatives considered

- **Flat bank list** — rejected: does not scale to tens of thousands of files
  and loses the directory structure users rely on to tell game/level variants
  apart.
