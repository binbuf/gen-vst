# ADR-0018: Additional patch format support is post-MVP

- **Status:** Accepted
- **Date:** 2026-05-22
- **Related:** [04-patch-system.md](../04-patch-system.md), [ADR-0004](0004-furnace-only-factory-bank.md), [ADR-0012](0012-dmp-version-scope.md)

## Context

Three additional FM patch formats are relevant to the Genesis / YM2612 ecosystem
beyond the TFI, VGI, and DMP v11 formats already targeted for MVP:

- **Y12** — 128-byte full-channel register dump used by Mega Drive ROM-hacking
  tools (SMPS editors and similar). The closest format to raw hardware state; the
  way patches are extracted from actual game ROMs.
- **OPM** — Yamaha OPM instrument format. A large public patch library exists; the
  YM2612 shares enough parameter semantics that OPM patches load meaningfully, with
  DT2 (unused on YM2612) silently ignored.
- **VGM patch extraction** — load a `.vgm`/`.vgz` register-log file, scrub to a
  point in time, and snapshot the current FM channel state as a `Patch`. The only
  path to game-original register values without touching copyrighted ROM data.

The question is whether any of these must be designed or implemented before the MVP
ships, or whether they can be deferred without creating technical debt.

## Decision

**All three formats are deferred to post-MVP.** TFI, VGI, and DMP v11 are
sufficient for the MVP feature set. None of the deferred formats require design
changes to land later:

- The `Patch` struct already captures the complete YM2612 register set (ALG, FB,
  AMS, PMS, LFO, and per-operator MUL/DT/TL/KS/AR/DR/SR/RR/SL/SSG-EG/AMON).
  Every parameter Y12, OPM, and VGM snapshots carry maps into existing fields
  without struct additions.
- The `PatchLoadResult` loader pattern is a free function per format — adding Y12
  or OPM is one new function, no plumbing changes.
- The patch browser's custom-roots model and lazy scan already handle arbitrary
  extensions. Supporting Y12 or OPM in the browser is a one-line extension-filter
  change plus the loader.
- VGM extraction requires its own UI flow (file picker, timeline scrub, snapshot
  action) and a VGM parser dependency — it is a distinct feature, not just a
  loader, and belongs in its own future ADR.

The copyright constraint from ADR-0004 also applies: game-derived patches cannot be
shipped regardless of format. Y12 and VGM support enables *users* to load their own
extracted patches; no factory content changes are implied.

## Consequences

- Task 08 (VGI & DMP loaders) is unaffected. No additional formats are in scope.
- The file import picker filter (`*.tfi;*.vgi;*.dmp`) and drag-and-drop handler
  remain as specified in `04-patch-system.md`.
- MVP ships with zero ability to directly import game-original register state. Users
  who want game patches at launch must source community DMP/TFI conversions.
- When Y12 or OPM support is added post-MVP, no existing code paths change — new
  loaders are additive only.
- VGM extraction, when scoped, will require its own ADR covering the VGM parser
  dependency, UI design, and file scrubbing model.

## Alternatives considered

- **Add Y12 to Task 08** — Y12 is mechanically simple (parse 128 bytes, map to
  `Patch`). Rejected: the MVP benefit is marginal. Y12 is a ROM-hacking format; the
  primary community formats are DMP and TFI. The effort is low but the priority is
  lower still — post-MVP remains the right slot.
- **Add OPM to Task 08** — rejected for the same reason. The mapping is
  straightforward but OPM's DT2 field requires a documented ignore decision.
  Better handled in a focused post-MVP task than grafted onto Task 08 as an
  afterthought.
