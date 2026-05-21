# ADR-0004: Ship only the Furnace tfilib factory bank

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [04-patch-system.md](../04-patch-system.md), [ADR-0003](0003-gpl-v3-license.md), [ADR-0005](0005-filesystem-patch-delivery.md)

## Context

Two patch collections are available during development:

1. The Furnace `tfilib` set — ~39 `.tfi` instruments with generic timbre names
   (`bass.tfi`, `piano.tfi`, `marimba.tfi`, …), from `tildearrow/furnace` at
   `instruments/OPN/tfilib/`. Furnace is GPL.
2. A large game-derived `.tfi` collection (~30k files) whose file and directory
   names encode game, publisher, and level titles — i.e. trademark and
   copyright exposure.

## Decision

The **only** bank bundled with the plugin is the **Furnace `tfilib` factory
bank**. Its top-level `.tfi` files are committed directly in `extern/patches/`.

The game-derived collection is **developer test material only**: kept in
`extern/patches/extra/`, gitignored, never committed and never copied into any
build artifact.

## Consequences

- The build must enumerate factory patches with a **non-recursive** glob of
  `extern/patches/*.tfi` — a recursive scan would pull the gitignored `extra/`
  set into the bundle. (See [ADR-0005](0005-filesystem-patch-delivery.md).)
- Furnace attribution must be included in the project.
- The project carries no game-audio redistribution exposure.
- Developers exercise the loader and browser against the large `extra/` set
  ad hoc through the patch browser's Import / folder-drop path.
- The `07-feature-spec.md` open question on "MDDC patch licensing" is resolved
  by this ADR and should be removed from that doc's open-question list.

## Alternatives considered

- **Ship the game-derived bank** — rejected: trademark/copyright exposure.
- **Ship no factory bank at all** — rejected: a plugin with zero presets is a
  poor first-run experience.
