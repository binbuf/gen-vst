# Gen VST — Task chains

Implementation tasks are split into two sequential chains:

- **[`mvp/`](mvp/README.md)** — the **v1 MVP chain** (tasks `01`–`34`),
  all shipped. Historical record of the original Genny-parity build. The
  engine code these tasks produced is the v2 baseline.

- **[`mvp2/`](mvp2/README.md)** — the **v2 chain** (tasks `01`–`10`).
  v2 rebuilds the UI against a three-mode single-engine architecture
  (FM/SQ/D) modelled on Inphonik's RYM2612 and PCM2612. See ADRs
  0021–0025 for the design decisions and
  `docs/design/archive/README.md` for the v1→v2 pivot summary.

The convention for both chains is the same — every task is a
self-contained work order with concrete verification steps. See
[`mvp/README.md`](mvp/README.md) for the conventions doc that applies
to both chains.
