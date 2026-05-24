# Task 01 — Static UI mockup (HTML/CSS, throwaway)

> **Milestone:** Visual lockdown — the v2 look is approved against the
> RYM2612 + PCM2612 references *before* any C++ churn lands.
> **Depends on:** nothing.
> **Design references:** `docs/design/09-visual-spec.md` (primary —
> palette tokens, typography table, per-widget CSS recipes),
> `docs/design/05-ui-ux.md` (binding visual principles),
> `docs/design/08-ui-views.md` (per-mode layouts), ADR-0022, ADR-0023.
> **Reference images:** `docs/design/reference/RYM2612-panelfront.jpg`,
> `docs/design/reference/pcm2612-VST.jpg`,
> `docs/design/reference/RYM2612-User-Manual.pdf`.

## Objective

Produce a **static HTML/CSS mockup** of the v2 plugin chassis at 1200×560
so the visual identity (palette, typography, chassis treatment, knob /
LCD / toggle / button recipes, per-mode layouts) can be locked side-by-side
against the RYM2612 + PCM2612 references *before* the C++ refactor begins.

The mockup is **throwaway scaffolding for visual derisking only**. The
`design-system.css` token file produced here survives — every later task
imports it. The mockup HTML pages and any mockup-only CSS are deleted in
Task 04 when the real widget library lands.

## Context & key constraints

- **No JS state, no JUCE binding, no DSP, no fake interactivity** —
  this is HTML + CSS only. Where a knob would normally rotate on drag, the
  mockup just renders a single resting indicator position. No animations
  beyond pure CSS `:hover` / `:active`. Resist the urge to wire mock data
  — it is not pulling its weight on a throwaway mockup.
- **Use the `09-visual-spec.md` recipes verbatim.** Palette tokens, font
  stack, gradient stops, shadow stacks, border radii — copy them, do not
  invent new ones. Every hex code lives as a CSS custom property; no
  hand-typed hex inside a component.
- **Window is fixed 1200×560** (ADR-0023). Author each mockup page at that
  exact viewport. Inner regions: header (~88 px) and mode panel
  (~470 px) — no bottom status bar (the v2 first-pass status surface
  was removed during the post-mockup review; L/R meters live in the
  header cluster instead).
- **Top-left light source** (ADR-0022 principle 1). Every shadow, bevel,
  and gradient honors this. No per-widget variation in light direction.
- **Antialiasing on** (principle 6). Browser default everywhere.
- **IBM Plex Mono** as the single font family (09-visual-spec *Typography*).
  Add `extern/fonts/ibm-plex-mono/IBM-Plex-Mono.woff2` (subset to Latin
  Basic + Latin Extended-A, Regular + Medium + Bold) and load it via
  `@font-face` in `design-system.css`. The v1 fonts under
  `extern/fonts/press-start-2p/` and `extern/fonts/7-segment/` stay on
  disk (ADR-0022 *Consequences*) but are not referenced by the v2 CSS.
- **Reachable via Vite dev server.** Add a multi-page entry per mockup
  HTML file to `ui/vite.config.js`; `npm run dev` should serve each at
  `http://localhost:5173/mockup-<name>.html`.
- **Reference parity is functional, not pixel.** ADR-0015 — match the
  look, the layout structure, the proportions; do not pixel-trace.

## Scope

Four mockup HTML pages, each independent (no shared header partial —
keep the mockup honest by repeating markup):

1. **`mockup-chassis.html`** — base chassis frame: 1200×560 window,
   ~88 px header skeleton (NOTE ON LED + visible `NOTE ON` text label,
   wordmark, mode pill, patch LCD nav, OUTPUT FILTER 2-way switch,
   LADDER toggle, stacked L/R `level-meter-mini` cell, VOL knob, gear),
   empty mode panel filling the rest of the canvas. No bottom status
   bar — the L/R meters live in the header cluster; the version string
   lives in the About modal. Establishes the chassis + inset +
   outer-bezel + level-meter-mini + note-on recipes for every other page.
2. **`mockup-fm.html`** — full FM mode panel per `08-ui-views.md` view 2,
   inside the chassis from page 1. Header populated with the FM-specific
   patch name. Operator grid (4 rows × 14 columns: `TL` knob as
   leftmost-column anchor, then `[N]` AM AR DR SL SR RR RS SSG-EG MUL
   FREQ FIX DT, then a single per-op `VEL` knob in the right-margin
   block). LFO/global block (LFO/RATE/PMS/AMS knobs + POLY/
   RANGE steppers + LEGATO/RETRIG switch + **global** PB and MW
   horizontal `level-meter`s). Envelope-curve LCD with segment labels
   (`AR / DR / SL / SR / RR`) and `KEY ON` / `KEY OFF` dashed-line
   markers. FREQ CTRL MODE pill, RETRIG RATE LCD, OP1 FB knob, **DAC
   PRESCALER knob (new — FM mode YM2612 prescaler control)** stacked in
   the misc column. **8-button algorithm picker (`algo-grid`, 2 × 4
   visible buttons)** plus a separately-sized **~112 px topology
   diagram tile** showing the selected algorithm. No bottom status bar.
3. **`mockup-sq.html`** — full SQ mode panel per view 3. Three tone-channel
   strips + one noise strip. Envelope thumbnails are static SVG-free
   canvas-placeholder rectangles with a hand-drawn `<path>`-free curve in
   the LCD (just a flat dim background and a centered "ADSR" word is
   acceptable — the real curve is Task 04). Same header as the chassis
   page; no bottom status bar.
4. **`mockup-d.html`** — full D mode panel per view 4. Centered large
   decimator knob, stereo level meters band (drawn as a row of `<div>`
   blocks, not live), MONO toggle, DRY/WET knob, optional centered
   `RETRO DECIMATOR` wordmark in the empty band above the knob. Same
   header as the chassis page; no bottom status bar.

Plus the persistent design-system file:

- **`ui/src/styles/design-system.css`** — palette CSS custom properties,
  `@font-face` for IBM Plex Mono, typography utility classes, the chassis
  / inset / knob / button / toggle / LCD / level-meter / op-badge CSS
  recipes verbatim from `09-visual-spec.md`. This is the only file from
  this task that survives.

Plus the Vite multi-page entries in `ui/vite.config.js`.

## Out of scope

- Any JavaScript state, knob-drag handling, relay binding, or
  C++ ↔ JS interop. Each mockup page is opened, looked at, closed.
- Modals (preset browser, settings, about, toast). Those are visually
  simpler and ship with their owning real task (Task 08 / 09).
- Real Canvas widgets — Task 04 builds those against the design system.
- Mockup interactivity beyond CSS `:hover` / `:active`.
- Any C++ change.

## Implementation steps

1. Add the IBM Plex Mono woff2 to `extern/fonts/ibm-plex-mono/`. If
   pulling from the SIL OFL distribution, subset locally with a tool such
   as `pyftsubset` (Latin Basic + Latin Extended-A, weights 400/500/700);
   commit a single woff2.
2. Write `ui/src/styles/design-system.css`:
   - `:root { --chassis-bg-top: #c8ccd0; ... }` for the full palette
     from `09-visual-spec.md` *Palette*.
   - `@font-face` rule(s) for IBM Plex Mono pointing at
     `/ibm-plex-mono/...` (Vite serves `extern/fonts/` as `publicDir`
     per the existing `vite.config.js`).
   - Typography utility classes / custom properties matching the
     *Typography* table.
   - The CSS recipes from `09-visual-spec.md` *CSS recipes per widget*:
     `.chassis`, `.inset`, `.knob` (+ `.decimator-knob` modifier),
     `.btn` (+ `.is-active` state), `.toggle` (+ `.is-on` state),
     `.op-badge` (+ `.is-active` state). Use the canonical recipes
     verbatim.
3. Write each `mockup-*.html` page:
   - `<head>` loads `design-system.css` and any page-only mockup CSS.
   - Outer chassis div sized to 1200×560 (`width: 1200px; height: 560px;`),
     with the `.chassis` class.
   - Header skeleton: wordmark, mode selector pill, patch-name LCD,
     output-character toggles, VOL knob, gear icon. Use real text
     ("GADGET BASS", "v0.2.0", etc.).
   - Mode-specific body per `08-ui-views.md`. Repeat markup rather than
     extracting into shared components — the mockup is throwaway.
   - LCD readouts: a `<div>` with the `.lcd-readout-bg` styling and a
     glowing-text span — no `<canvas>` (Task 04 introduces canvas).
   - Operator grid (FM page): a 4-row HTML table or CSS grid. Each cell
     is a small static `.knob` or `.toggle` or a `<div>` with an LCD
     readout class. The column structure must match view 2's table.
4. Add an entry to `ui/vite.config.js` `rollupOptions.input` for each
   mockup page so they're reachable in dev and dist.
5. Place the reference images side-by-side with the live mockup and
   iterate. The mockup is **done when it reads as the same family of
   instrument** as the references, not when it traces them pixel-for-pixel.

## Deliverables

- `extern/fonts/ibm-plex-mono/IBM-Plex-Mono.woff2` (+ a README noting the
  SIL OFL license and the subset configuration).
- `ui/src/styles/design-system.css` (new, will survive into Task 04+).
- `ui/mockup-chassis.html`, `ui/mockup-fm.html`, `ui/mockup-sq.html`,
  `ui/mockup-d.html` (throwaway, deleted in Task 04).
- Optional per-page CSS files under `ui/mockup/` (throwaway).
- `ui/vite.config.js` updated to add the mockup pages as multi-page entries.

## Verification

1. `npm --prefix ui ci && npm --prefix ui run dev` starts the Vite dev
   server with no errors.
2. Open each `http://localhost:5173/mockup-*.html` in a Chromium-based
   browser at exactly 1200×560 (DevTools device toolbar set to that size).
   The chassis fills the viewport with no scrollbars.
3. Place each mockup side-by-side with the corresponding reference image
   (RYM2612 panel for chassis/FM, PCM2612 panel for D, the SQ panel is
   the v2-original layout — judge against the v2 visual principles, not
   a hardware reference). The mockup reads as the same family of
   instrument: same chassis treatment, same knob style, same LCD glow,
   same toggle physicality.
4. `npm --prefix ui run build` produces a clean `dist/` that contains
   each `mockup-*.html`.
5. Visual checklist (each must pass before the task closes):
   - [ ] Top-left light source on every shadow and bevel.
   - [ ] No `border-radius: 0` on buttons / pills / chassis where the
         visual spec calls for soft corners; no `border-radius` on LCD
         insets where the spec calls for square corners.
   - [ ] All labels are uppercase IBM Plex Mono with letter-spacing in
         the 0.10–0.20 em range per the typography table.
   - [ ] LCD text glows (the recipe is two-pass text with `shadow-blur`).
   - [ ] Op-badge squares are filled `--op-badge-bg` blue.
   - [ ] Knob bodies use the dark-navy gradient, indicator is a thin
         white line at the resting 7 o'clock position.
   - [ ] Toggle "on" state lights with an outer glow.
   - [ ] **No bottom status bar.** L/R output meters live in the header
         as a stacked `level-meter-mini` cell; the chassis bottom edge
         is the mode-panel bottom edge.
   - [ ] **NOTE ON LED + visible `NOTE ON` text label** at the far left
         of the header. LED is at least 16 px round.
   - [ ] **Per-operator TL knob is the leftmost column** of the operator
         grid, before the `[N]` op-badge. Slightly larger / darker than
         the rest of the row's knobs.
   - [ ] **No per-operator `MW` column.** MW shows up only as a *global*
         horizontal `level-meter` in the LFO/global block.
   - [ ] **8-button algorithm picker** (visible all the time, 2 × 4
         grid). No popover. A separate ~112 px topology diagram tile
         sits next to it.
   - [ ] **DAC PRESCALER knob lives in the persistent header** next to
         VOL (per the RYM2612 reference). Active in FM mode, greyed in
         SQ + D. The D-mode panel still has its own loud central
         `DAC PRESCALER` knob (separate apvts param `prescaler` —
         distinct from the header's `fm_dac_prescaler`).
   - [ ] **`CH VOL` master TL knob** sits above the operator-grid TL
         column with fan-out connector lines drawn down into each of
         the 4 operator rows' TL knobs (decorative SVG overlay).
   - [ ] **PB and MW are vertical `midi-wheel` widgets** (analog-style
         hardware wheels with thumb position indicators) in a dedicated
         `GLOBAL IN` block to the left of the operator grid — not LED
         meters, not buried in the LFO block.
   - [ ] **RETRIG RATE has a `stepper-readout`** (LCD + ▲/▼ buttons),
         not a bare LCD.
   - [ ] **OP1 FB carries a short vertical connector line** beneath the
         knob, visually anchoring it to operator 1 in the grid below.
   - [ ] **Envelope curve carries `AR`/`DR`/`SL`/`SR`/`RR` segment
         labels** and dashed `KEY ON` / `KEY OFF` vertical markers in
         the LCD.

## Done when

- [ ] `design-system.css` exists with the full v2 palette + typography +
      widget recipes from `09-visual-spec.md` (including `algo-grid`,
      `level-meter-mini`, and the labelled `.note-on` LED cluster).
- [ ] IBM Plex Mono loads correctly via `@font-face`.
- [ ] Four mockup pages render at 1200×560 with no JS errors.
- [ ] Each mockup matches its reference family-of-look (subjective sign-off
      against the visual checklist above).
- [ ] Vite dev server + Vite production build both work; mockup pages are
      reachable in dev and present in `dist/`.
- [ ] No JS state, no JUCE binding, no fake interactivity beyond CSS
      `:hover` / `:active`.
