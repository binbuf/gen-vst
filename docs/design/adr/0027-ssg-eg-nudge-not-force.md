# ADR-0027: SSG-EG looping shapes — visual nudge, not forced AR=31

- **Status:** Accepted
- **Date:** 2026-05-25
- **Related:** [ADR-0010](0010-ymfm-instance-model.md), `docs/design/02-fm-synthesis.md`, `docs/design/08-ui-views.md`

## Context

The YM2612's SSG-EG (Software-controlled Sound Generator Envelope Generator)
register `0x90` encodes 9 audible states:

- `0` — SSG-EG off (along with hardware-invalid 1–7, collapsed by `clampSsg`)
- `8, 10, 12, 14` — looping shapes (saw-down repeat, alternate triangle,
  saw-up repeat, alternate-up triangle)
- `9, 11, 13, 15` — one-shot and hold shapes

The looping shapes only sound "correct" — i.e. produce the documented
repeating envelope — when **AR = 31** on the same operator. With AR < 31,
the SSG-EG generator still runs but the attack ramp becomes audible and
the loop point drifts away from the cycle. This is the source of the
"AR must be 31" guidance in the Yamaha YM2612 application manual and in
`02-fm-synthesis.md` *Envelope Generator*.

A natural reaction is to **force AR = 31** whenever a user selects a
looping SSG-EG shape. This was considered and rejected.

## Decision

When `ssg[op] ∈ {8, 10, 12, 14}` (looping shapes) **and** `ar[op] < 31`,
the FM panel paints the operator row's AR knob with an amber outline /
glow and surfaces a tooltip:

> *SSG-EG loop needs AR=31 to sound as labelled.*

No audio path override. No automatic mutation of the AR parameter. No
patch-file mutation on load. The user is informed; the user decides.

The condition is recomputed reactively whenever either of the two
parameters changes. The nudge clears as soon as AR is raised to 31, or
the user selects a non-looping SSG-EG shape, or SSG-EG is turned off.

## Rationale

1. **Preserve TFI / VGI / DMP / Y12 / OPM round-trip.** Patch files in
   the wild carry arbitrary (AR, SSG-EG) combinations. Forcing AR=31 on
   load would silently mutate every imported patch with a looping
   SSG-EG and AR < 31 — `loadTFI(p) → exportTFI` would no longer be
   identity. The community-format compatibility guarantee from
   `04-patch-system.md` would break for those files.

2. **AR < 31 with SSG-EG is a documented chiptune technique.** Composers
   on Genesis and OPN-family chips deliberately combine slow attacks
   with SSG-EG shapes to produce evolving textural sounds. Forcing AR=31
   removes that affordance.

3. **The "loop" condition is shape-specific.** Only 4 of the 8 shape
   values (`8, 10, 12, 14`) loop and depend on AR=31. The one-shot and
   hold shapes (`9, 11, 13, 15`) work correctly at any AR value —
   forcing AR=31 across all 8 shapes would over-apply the rule.

4. **DAW automation conflicts.** If a user automates AR independently
   of SSG-EG, a forced override would fight the automation lane,
   producing surprising audio jumps and host-state desync. A visual
   warning has no such side effect.

5. **Entanglement on disable.** If the force flipped AR to 31 on SSG-EG
   enable, what should happen when the user later disables SSG-EG?
   Restoring the pre-force AR requires per-op shadow state and a
   "remember the user's previous value" policy. Either choice
   (snap-back vs hold-at-31) is wrong for some workflow. The nudge
   sidesteps the entire question.

6. **Hardware-strict already exists as the escape hatch.** Users who
   want the YM2612 to behave the way the manual prescribes can enable
   the global `hardware_strict` toggle (Settings, view 6). The
   `hardware_strict` umbrella is the right home for any future
   "force-correct" enforcement — keeping it scoped to one opt-in flag
   instead of scattering paternalistic snaps across the panel.

## Consequences

- `ui/src/views/fm-view.js` watches each operator's `ssg_op{N}` +
  `ar_op{N}` pair and toggles a `.ssg-ar-mismatch` CSS class on the AR
  knob host. The CSS is scoped under `.fm-panel` so other AR knobs
  (none exist today) aren't affected.
- `ui/src/widgets/tooltip-content.js` carries a dedicated tooltip
  variant the AR cell switches to while the nudge is active.
- `02-fm-synthesis.md` *Envelope Generator* gains a *UI nudge — SSG-EG
  loop vs AR* subsection documenting the condition.
- `08-ui-views.md` view 2 SSG-EG row notes the amber warning state on
  the paired AR knob.
- `hardware_strict` is **not** extended to force AR=31; the strict
  toggle's existing scope (poly_voices ≤ 6, single FLOAT_MUL voice,
  Filter + Ladder locked on) is sufficient. If a future Task wants to
  add "strict SSG-EG = force AR=31 on loop shapes", that addition would
  ride under `hardware_strict` rather than become a free-standing flag.

## Alternatives considered

- **Force AR=31 unconditionally on looping SSG-EG.** Rejected — see
  rationale (1)–(5).
- **Force only when hardware_strict is on.** Defensible, but adds a
  second behaviour path through the audio thread for a feature most
  users won't enable. Deferred — the visual nudge alone is sufficient
  for v2 MVP; a future ADR can revisit if user feedback requests it.
- **Snap AR with a confirmation modal.** Adds a modal to a high-frequency
  control (every SSG-EG flip would interrupt the user). Rejected.
- **Add a `[Snap AR to 31]` button next to the SSG-EG stepper.** Useful,
  but redundant — the user can already double-click the AR knob (resets
  to default — happens to be 0) or drag it. The visual nudge surfaces
  the relationship; we trust the user to act on it. Deferred.
- **Auto-snap on save / export only.** Hidden mutation that breaks
  round-trip in a different way. Rejected.
