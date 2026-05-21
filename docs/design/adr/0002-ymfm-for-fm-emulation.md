# ADR-0002: Use ymfm for YM2612 FM emulation

- **Status:** Accepted
- **Date:** 2026-05-21
- **Related:** [02-fm-synthesis.md](../02-fm-synthesis.md), [ADR-0003](0003-gpl-v3-license.md), [ADR-0009](0009-sn76489-library.md), [ADR-0010](0010-ymfm-instance-model.md)

## Context

Gen VST emulates the Sega Genesis YM2612 (OPN2) FM sound chip. The synthesis
core must be accurate to the hardware register model, actively maintained, and
license-compatible with a GPL v3 project (see [ADR-0003](0003-gpl-v3-license.md)).

## Decision

Use **ymfm** (`aaronsgiles/ymfm`), BSD-3-Clause licensed, via the
`ymfm::ym2612` OPN2 class. ymfm is included as a git submodule at
`third_party/ymfm/` and its sources (`ymfm_opn.cpp`, `ymfm_misc.cpp`) are
compiled **inline into the plugin target** rather than as a separate static
library, to avoid LTO boundary issues and simplify the build graph.

## Consequences

- BSD-3-Clause is compatible with the project's GPL v3 license.
- ymfm does **not** include an SN76489 PSG emulator — a separate library is
  required for the PSG (see [ADR-0009](0009-sn76489-library.md)).
- The full register-level API is documented in
  [02-fm-synthesis.md](../02-fm-synthesis.md).
- The number of `ymfm::ym2612` instances and how channels map to voices is a
  separate decision (see [ADR-0010](0010-ymfm-instance-model.md)).

## Alternatives considered

- **Nuked-OPN2** — cycle-accurate but heavier CPU cost; accuracy beyond ymfm's
  is not worth the per-voice cost at 16-voice polyphony.
- **MAME / Genesis Plus GX cores** — accurate but more tightly coupled to their
  host emulators and harder to extract cleanly.
