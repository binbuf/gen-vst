# ADR-0012: DMP format version scope

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [04-patch-system.md](../04-patch-system.md), [05-ui-ux.md](../05-ui-ux.md), [07-feature-spec.md](../07-feature-spec.md)

## Context

The DefleMask preset (`.dmp`) format has a variable-length, version-dependent
layout. Versions 0–8 use a range of legacy layouts that differ per version;
version 11 (`0x0B`) is the modern format.

The design docs disagreed on scope: `07-feature-spec.md` listed "DMP patch import
(DefleMask format, version 8 and 11)," while `04-patch-system.md` targets version
11 and its `loadDMP` sketch effectively rejects anything that is not 11.
Supporting the legacy v8 layout means a second distinct parser path.

## Decision

For the MVP, **support DMP version 11 only**. Files of any other version are
**rejected with a clear, user-visible error message** — surfaced via the UI
notification toast (see `05-ui-ux.md`) — rather than parsed best-effort. Legacy
v8 support is deferred to post-MVP and only if there is real demand.

This resolves the contradiction in favour of `04-patch-system.md`;
`07-feature-spec.md` is updated to say "version 11" instead of "version 8 and 11."

## Consequences

- One DMP parser path to write, test and verify — not two.
- DMP remains a secondary format; TFI is primary and VGI secondary, both
  unaffected by this ADR.
- The v11 byte offsets must still be verified against the Furnace source
  (`src/format/dmp.cpp`) during implementation — an implementation task, not part
  of this decision.

## Alternatives considered

- **Support v8 + v11** — rejected for the MVP: a second distinct parser path for
  a secondary format. Cheap to widen later if v8 demand appears.
