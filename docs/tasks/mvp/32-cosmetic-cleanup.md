# Task 32 — Cosmetic cleanup (Genny-polish leftovers)

> **Depends on:** Task 26 (Genny visual polish).
> **Design references:** `docs/genny-ui.md` Color Palette, the Genny
> screenshots `Screenshot 2026-05-23 094*.png` referenced from Task 26.
> **Note:** Stamped by Task 26 as the catch-all for the small visual
> deltas that surfaced during the algorithm audit / palette pass /
> bracket-glyphs / DAC restyle but did not fit cleanly into that task.
> Slot 32 because 29 (VGM logging), 30 (Scala tuning), and 31 (DAC
> multi-sample) were already taken.

## Objective

Close the remaining pixel-level deltas between the running plugin and
the Genny reference screenshots. None of these block functionality —
they are the items the Task 26 side-by-side flagged as "noticeable but
not load-bearing." Bundle them so the diff against the reference reads
as zero-delta at a glance.

## Items

1. **Knob specular highlight var.** `ui/src/widgets/knob.js` uses
   hard-coded `#ffffff` for the 2x2 specular and `#7aa0e0` for the lit
   bevel tint (lines 110 + 164). Add `--knob-spec` and
   `--knob-bevel-light` to `design-system.css` and switch the widget to
   the vars. Mirror the new vars in `widgets/pixel.js` PALETTE_VARS so
   `palette()` exposes them.
2. **Badge bevel light var.** `.badge` in `ui/src/styles/chassis.css`
   uses `#6a98e8` for its light bevel edges. Same treatment — add the
   tint to `design-system.css` (e.g., `--badge-bevel-light`) so the
   `.badge` rule binds to the palette instead of an inline hex.
3. **Toast warn-level background.** `ui/src/widgets/notification-toast.js`
   uses `#2a2208` as the warn-level toast background — a darkened gold
   with no palette slot. Add `--logo-dark` (a `--logo` tint) and use it.
4. **Genny screenshot side-by-side.** Open the running standalone next
   to `Screenshot 2026-05-23 094523.png`, `094629.png`, `094947.png`
   and `094958.png`. Note every visual delta wider than ~2 px and add
   it to the items list above before working through them.
5. **Algorithm diagram readability at the smallest UI scale.** Task 26
   fixed the routing for ALG 3 (canonical YM2612 vs. the prior
   plutiedev mis-read) but the L-junction dots can still look pinched
   at 1x UI scale. Audit the `_junctionDot` size + the 2px line stroke
   against `Screenshot 2026-05-23 094947.png` and bump to 3px stroke if
   the diagram still reads as thin.

## Scope

- `ui/src/styles/design-system.css` — new palette entries.
- `ui/src/widgets/pixel.js` — extend `PALETTE_VARS`.
- `ui/src/widgets/knob.js`, `ui/src/widgets/notification-toast.js`,
  `ui/src/styles/chassis.css` — bind to the new vars.
- `ui/src/widgets/algo-diagram.js` — line / junction sizing tweak only;
  the routing tables stay frozen.

## Out of scope

- Anything that changes a routing table, parameter, or audio behaviour.
- New widgets.
- Anything that requires recompiling the C++ side.

## Verification

1. `npm run build` (`ui/`) — Vite build succeeds with no console
   warnings about unknown CSS variables.
2. Standalone — every formerly hard-coded colour now uses the palette
   var, verified by `git grep "#[0-9a-fA-F]" ui/src/widgets ui/src/styles`
   returning only the entries in `design-system.css`.
3. Side-by-side screenshot vs the Genny references — no delta wider
   than ~2 px.
4. `pluginval --strictness-level 8` — SUCCESS (no engine change, so a
   regression check, not a real risk).

## Done when

- [ ] Specular / bevel / toast colours come from `--knob-spec` /
      `--knob-bevel-light` / `--badge-bevel-light` / `--logo-dark` vars.
- [ ] Algorithm diagram lines + junction dots read crisply at 1x UI scale.
- [ ] Genny side-by-side reads as zero-delta at a glance.
- [ ] `pluginval` SUCCESS.
