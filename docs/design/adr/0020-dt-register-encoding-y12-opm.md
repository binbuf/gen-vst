# ADR-0020: DT register encoding conversions for Y12 and OPM loaders

- **Status:** Accepted
- **Date:** 2026-05-23
- **Related:** [04-patch-system.md](../04-patch-system.md), [ADR-0012](0012-dmp-version-scope.md), [ADR-0019](0019-additional-patch-formats-in-mvp.md)

## Context

`Patch::dt[op]` stores detune in **TFI encoding (0–6)**: 0–3 are the positive
detunes (+0..+3), 4–6 are the negative detunes (−1..−3). The YM2612 / YM2151
hardware register stores detune in a different 3-bit encoding (0–7) where the
fourth value (HW 4) is a redundant "no detune" alias of HW 0. The asymmetry
exists because `FmRegisterMap::detuneToRegister` shifts TFI 4–6 to HW 5–7 to
skip the redundant HW 4 slot when writing back to the chip; the patch model
is therefore TFI-shaped on purpose.

Two of the formats added by [ADR-0019](0019-additional-patch-formats-in-mvp.md)
expose this asymmetry:

- **Y12** is a literal YM2612 register dump (the per-operator block layout
  mirrors hardware registers 0x30/0x40/.../0x90), so the DT field arrives in
  HW 0–7 encoding.
- **OPM** stores `DT1` as the YM2151's 3-bit detune register value, also in
  HW 0–7 encoding.

Implementing these loaders surfaced two specific decisions:

1. **Y12** — Furnace's `DivEngine::loadY12` (the only available reference
   loader, cited per ADR-0012's "verify against Furnace" pattern) reads DT as
   `(3 + (tmp >> 4)) & 0x7` and marks it `// ???` in the source. The
   transformation does not match the project's TFI/HW conversion semantics:
   it would map HW 0 (no detune) to TFI 3 (+3 detune), HW 5 (−1) to TFI 0
   (no detune), etc.
2. **OPM** — `04-patch-system.md`'s OPM field-mapping table said *"the loader
   takes the 0–7 source value directly"* for `DT1`, implying no conversion.
   Storing the raw HW value would break the patch model's invariant: a stored
   `Patch::dt[op] = 7` then pushed through `FmRegisterMap::detuneToRegister`
   would produce register value 8, overflowing the 3-bit DT field and
   corrupting the register 0x30 write.

Both decisions affect the same conversion, so they belong in one ADR.

## Decision

**Both `loadY12` and `loadOPM` convert HW DT (0–7) → TFI DT (0–6) via a
shared `registerToDetune` helper** that is the inverse of
`FmRegisterMap::detuneToRegister`:

| HW DT | TFI DT | Meaning |
|-------|--------|---------|
| 0 | 0 | no detune |
| 1, 2, 3 | 1, 2, 3 | positive detune (+1, +2, +3) |
| 4 | 0 | hardware "second zero" — collapses to TFI 0 |
| 5, 6, 7 | 4, 5, 6 | negative detune (−1, −2, −3) |

Furnace's `(3 + hw) & 7` Y12 DT transform is **not replicated**; the `// ???`
marker indicates uncertainty, and the transformation does not preserve detune
semantics under the project's TFI convention. The operator-block byte ordering
(field positions and bit packings) *is* taken from Furnace's loader — only the
DT decoding diverges. The verified byte layout is recorded as a code comment
in `src/PatchSystem.cpp`'s `loadY12`.

For Y12, **only the operator-block layout and the ALG/FB bytes are verified**
against Furnace's loader (which is all Furnace reads). The channel-level
AMS/PMS/LFO offsets at 0x42/0x43/0x45 follow `04-patch-system.md`'s spec
table; if a real-world Y12 file leaves these as garbage padding, the loader's
existing `clampTo` saturates the values into the hardware ranges, so the
worst-case outcome is a benign 0 default. The legacy 0x44 "AMON packed" byte
is **not** read — the per-operator AMON bit at byte +3 bit 7 of each operator
block (matching the YM2612 hardware register 0x60+off) is the authoritative
source.

## Consequences

- `Patch::dt[op]` invariant (always in 0–6 TFI encoding) is preserved across
  all five loaders. `FmRegisterMap::detuneToRegister` round-trips correctly.
- `04-patch-system.md`'s OPM section is corrected: the `DT1` row now reads
  *"converted to TFI 0–6 via the inverse of `FmRegisterMap::detuneToRegister`"*
  instead of *"the loader takes the 0–7 source value directly"*. The design
  doc was the imprecise side of the inconsistency.
- `04-patch-system.md`'s Y12 section now records the verified byte layout
  (citing Furnace's `DivEngine::loadY12`) rather than carrying the open
  "verified once implementation lands" note from ADR-0019.
- Y12 loader test `PatchLoaderY12.NegativeDetuneRegisterIsConvertedToTfiEncoding`
  guards the HW 4–7 → TFI 0/4/5/6 conversion specifically, so an accidental
  switch to Furnace's `(3 + hw) & 7` transform would fail the test.

## Alternatives considered

- **Store raw HW 0–7 in `Patch::dt[op]`** — rejected. Breaks the patch-model
  invariant and corrupts the eventual register write (HW 7 → register byte
  with `dt << 4 = 0x70`, then `detuneToRegister` doubles the shift to 0x80,
  overflowing the field).
- **Replicate Furnace's `(3 + hw) & 7` Y12 DT transform for reference
  fidelity** — rejected. Furnace's own source marks it `// ???`; following an
  acknowledged-uncertain transform across two formats (Y12 + OPM) would lock
  in a bug.
- **Widen `Patch::dt` to support HW encoding directly** — rejected. Would
  ripple through `FmRegisterMap`, every loader, every exporter, and the
  WebView parameter range, just to avoid one shared inverse-function helper.
