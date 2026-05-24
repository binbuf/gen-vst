# Task 08 — Header, Settings & About modals

> **Milestone:** Header + Settings — mode selector, patch-name LCD,
> output-character toggles, VOL knob, NOTE ON LED + visible text label,
> stacked L/R output meters, settings gear in the header; Settings +
> About modals open from the gear icon and reach the Hardware Strict /
> Unison Detune / Aftertouch / UI Scale / Velocity→TL / Tooltips /
> Reset All controls.
> **Depends on:** Tasks 05, 06, 07.
> **Design references:** `docs/design/08-ui-views.md` views 1 (header),
> 6 (Settings), 7 (About) — primary; `docs/design/07-feature-spec.md`
> (HARDWARE STRICT, AFTERTOUCH, VELOCITY→TL behaviour),
> `docs/design/09-visual-spec.md` (palette / typography for the
> wordmark, the LCD, the `note-on` cluster, the `level-meter-mini`
> variant), ADR-0024 (Filter + Ladder toggles), ADR-0017 (UI scale),
> ADR-0023 (region heights — header is now ~88 px, no bottom status
> bar).

## Objective

Build the persistent **header** (the only persistent region — the v2
first-pass bottom status bar was removed during the post-mockup review;
its L/R meters moved into the header, its version string moved into the
About modal) and the two global modals (**Settings**, **About**). Wire
every control to its apvts param. Implement the **HARDWARE STRICT**
enforcement semantics that touch FM voice allocation and the global
filter / ladder toggles.

After this task, every part of the chassis other than the preset
browser modal (Task 09) is alive: the user can flip between modes from
the header, see the active patch name in the LCD, toggle the two
output-character switches, ride the master volume, watch live L/R
output levels in the header meter cell, open Settings to flip Hardware
Strict or set the aftertouch routing, and read About to verify the GPL
attributions and the version string.

## Context & key constraints

- **Header layout** (`08-ui-views.md` view 1, ~88 px tall):
  - **`note-on` cluster** at the far left — a 16 px `.note-on-led`
    paired with a stacked, visible `NOTE ON` text caption. Mirrors the
    RYM2612 reference. Bound to the `noteOn` telemetry boolean.
  - `GEN VST` wordmark (IBM Plex Mono Bold 22 px, `--brand-mark`
    color). Clicking opens the About modal (view 7).
  - 3-segment mode selector pill (`FM` / `SQ` / `D`) bound to
    `mode_select`. Switching segments **silently loads a sensible
    default preset** for the new mode (ADR-0021 — the default-preset
    table is in Task 09; for this task, the apvts flips and the panel
    mounts; the patch path goes empty and the existing
    parameter values stay in place).
  - Patch-name LCD (`patch-name-lcd` widget) showing the active patch
    name (initially blank). Flanked by ◀ / ▶ buttons (prev / next within
    the active mode's sorted patch list — Task 09 supplies the list;
    this task wires the buttons to a `patchNav(direction)` native
    function the editor implements once Task 09's PatchSystem extension
    exists; for now, the buttons can be stubs / disabled).
  - 📂 button opens the preset browser modal (Task 09 — stub for this
    task; the button exists and is mounted with a click handler that
    no-ops or opens an "Under construction" toast).
  - **D-mode greying of the patch cluster** — when `mode_select == D`
    the entire patch cluster (`.hdr-patch` container holding the LCD
    + ◀ + ▶ + 📂) toggles `.is-disabled` and becomes non-interactive.
    The LCD displays a static `AUDIO FX` placeholder instead of a
    patch name. D mode has no preset format ([ADR-0025](../../design/adr/0025-tagged-preset-browser.md))
    so the cluster has nothing to do. This task wires the toggle
    (subscribing to `mode_select` apvts changes); Task 09 implements
    the actual `patchNav` / browser-open behaviour for FM/SQ.
  - Output Filter toggle — 2-position physical switch (`LEGACY` /
    `CRYSTAL CLEAR`) bound to `output_filter`. `LEGACY = output_filter
    on`. Disabled & forced-on when HARDWARE STRICT is on.
  - Ladder Effect toggle — single on/off LED rocker bound to
    `ladder_effect`. Greyed out in SQ mode (`mode_select == SQ`).
    Disabled & forced-on when HARDWARE STRICT is on.
  - **Stacked L/R output meters** — `level-meter` widgets in the
    `level-meter-mini` variant (two thin horizontal rows, L on top, R
    below; ~24 px tall together). Subscribed to `peakL` / `peakR`
    telemetry. Replaces the v2 first-pass bottom status bar.
  - **DAC PRESCALER** knob — a `knob-sm` paired with a `DAC PRESC`
    caption. Sits next to VOL per the RYM2612 reference. **Mode-aware
    binding** per `08-ui-views.md` view 1:
    - FM mode → binds `fm_dac_prescaler` (per-FM-patch param).
    - D mode → binds `prescaler` (D-mode DSP global param). The D panel
      itself has no prescaler control; this header widget is the sole
      surface for both prescalers.
    - SQ mode → greyed (`.is-disabled`); PSG bypasses the DAC and the
      widget has nothing meaningful to bind.

    Implementation: a small `bindSliderByMode({ FM: 'fm_dac_prescaler',
    D: 'prescaler' })` helper (or inline equivalent) that subscribes
    to `mode_select`, unbinds + rebinds + reloads the displayed value
    on each mode change, and applies `.is-disabled` when the active
    mode has no entry in the map. The two underlying params are
    **distinct** — `fm_dac_prescaler` travels with an FM patch;
    `prescaler` is a plain D-mode apvts param owned by the host (no
    preset format — see [ADR-0025](../../design/adr/0025-tagged-preset-browser.md)).
    The helper must not copy values between them on mode switch;
    each param keeps its own last-set value.
  - VOL knob bound to `master_volume` (small `knob`).
  - **TIPS toggle** — small `toggle` with a `TIPS` text caption, bound
    to `tooltips_enabled`. Mirrors the Settings `TOOLTIPS` row above
    so users can flip tooltips on while learning the layout (and off
    once they know it) without leaving the chassis. Default on.
  - ⚙ gear icon opens Settings (view 6).
- **No bottom status bar.** The v2 first-pass status bar (view 5 in an
  earlier `08-ui-views.md` draft) was deleted during the post-mockup
  review. The version string lives in the About modal only; the L/R
  meters live in the header cluster above.
- **Settings modal** (view 6) — opens from the gear icon, dismissed by
  Close / `[X]` / Esc:
  - `HARDWARE STRICT (FM)` — bound to `hardware_strict`. **On** =
    clamp `poly_voices` to 6 (the FM panel's POLY stepper UI clamps
    to 6 cap immediately; voices >6 the user had set are stolen back
    to 6); restrict `FLOAT_MUL`/`AUTO_RETRIG` voices to one at a time
    (additional voices fall back to `INT_MUL` silently); force
    `output_filter = true` and `ladder_effect = true` and lock their
    header toggles. Off by default.
  - `UNISON DETUNE (FM)` — slider 0..50 ¢ bound to
    `unison_detune_cents`. Applied to voices triggered by the same MIDI
    note (the v1 unison-spread code provides the F-number offset
    mechanism; if it was removed in Task 02, re-implement: per-note
    detune offset across the active poly stack, symmetric fan-out
    pattern per `07-feature-spec.md` *Unison*).
  - `UI SCALE` — choice 1× / 2× / 3× (ADR-0017). On change, the editor
    resizes the WebView (HTML CSS transform: `scale(N)` on the body, or
    update the WebView's host bounds to `1200*N × 560*N`).
  - `VELOCITY → TL (FM)` — bound to `velocity_to_tl`. **On** = MIDI
    velocity scales TL via the existing v1 vel→TL formula; **off** =
    velocity is ignored on TL. Independent of the per-op `vel[op]`
    depth (those still apply when this is off — they're a per-op
    velocity-into-TL depth, while this is the global velocity→TL
    scaling on the carrier).
  - `AFTERTOUCH` — choice off / LFO depth (PMS) / Carrier TL, default
    **LFO depth (PMS)**. Bound to `aftertouch_target`.
  - `TOOLTIPS` — bound to `tooltips_enabled`. The widget library reads
    this to show/hide hover tooltips (`installTooltips()` from Task 04).
    The **same** boolean is also exposed as a `TIPS` toggle in the
    header (see view 1) for quick access; both control surfaces bind
    to the same apvts param, so flipping either flips both.
  - `ABOUT / CREDITS…` — opens the About modal (view 7).
  - `RESET ALL TO DEFAULTS` — destructive red button. On click, opens
    a confirmation modal ("This will reset every parameter and clear
    the active patch path. Continue?"). On confirm, every apvts
    parameter snaps to its `juce::AudioParameter`'s configured
    default; the active patch path clears.
- **About modal** (`08-ui-views.md` view 7):
  - `GEN VST v0.2.0` heading + tagline.
  - The license attributions table from view 7 verbatim — kept in
    sync with `04-patch-system.md` *Legal Notes*.
  - Source repository link (link target taken from a `--define`
    passed by CMake, or a fixed placeholder until the repo URL is
    decided).
  - Plain `Close` button.
- **NOTE ON LED telemetry** — `noteOn` boolean already on the
  `meterData` event (Task 03). The LED widget binds to it; in D mode
  the `noteOn` flag follows the input-signal-present threshold (Task
  03), in FM/SQ it follows any-voice-gating.
- **Modal behaviour** (view *Modal behaviour (shared)*):
  - Open over a dimmed main UI; one modal at a time.
  - Dismissed by Close / `[X]` / Esc.
  - The notification toast appears above an open modal (z-index higher
    than the modal layer).
  - Modals never spawn an OS window (in-WebView overlay only).

## Scope

- New `ui/src/header.js` building the header HTML; mounts and binds
  every header control (including the stacked `level-meter-mini`
  output meters and the `note-on` cluster).
- New `ui/src/modals/settings.js` building the Settings modal.
- New `ui/src/modals/about.js` building the About modal (carries the
  version string — the only persistent surface for it now that the
  bottom status bar is gone).
- New `ui/src/modals/modal-host.js` (or extension to the toast-host
  pattern) providing the dim-overlay layer + Esc handler shared by all
  modals.
- `main.js` mounts the header (alongside the mode-dispatched panel
  from Tasks 05–07) and opens Settings on gear click. No status-bar
  module — header carries the meters.
- C++:
  - `PluginProcessor::pushPolyphonyParameters` (or wherever the voice
    allocator's pool size is updated each block) clamps `poly_voices`
    to 6 when `hardware_strict` is true.
  - Voice / VoiceAllocator: when `hardware_strict` is true, FLOAT_MUL
    and AUTO_RETRIG are only honoured for the *first* active voice
    using them (`voiceUsingChannel3 != currentVoice → fall back to
    INT_MUL silently`).
  - The header Filter / Ladder toggle disable-and-force logic happens
    on the **JS side** (the UI greys / locks the toggles based on
    `hardware_strict`), but the **C++ also enforces** the forced-on
    behaviour: when `hardware_strict` is true, the audio path treats
    the toggles as on regardless of their apvts values (so a stale
    apvts read can't bypass the strict semantics).
  - UI scale (`ui_scale`) doesn't affect the audio path; the editor
    side resizes the WebView host.
- Native function `resetAllToDefaults()` — iterates apvts parameters
  and sets each to its configured default via
  `setValueNotifyingHost(getDefaultValue())`; clears the active patch
  path. Wired through the editor and called from the JS
  Settings → RESET click handler after the user confirms.
- Native function `patchNav(direction)` — stub for this task;
  Task 09 implements the real navigation. Returns the current patch
  path unchanged.

## Out of scope

- The preset browser modal — Task 09 wires the 📂 button and the
  patch-name LCD ◀ / ▶ buttons; this task ships them as stubs.
- The `.psg` format and tag-derived mode auto-switch — Task 09.
  (D mode has no preset format per
  [ADR-0025](../../design/adr/0025-tagged-preset-browser.md) —
  see Task 09's *Out of scope* for the full rationale.)
- Custom roots — Task 09 / Task 10 (the *Add Folder* button lives in
  the preset browser).

## Implementation steps

1. **Header layout** in `ui/src/header.js`. Mount the `note-on` cluster
   (16 px LED + `NOTE ON` text caption, subscribed to `noteOn`),
   wordmark (click → About), 3-segment mode pill (bind to `mode_select`;
   `bindCombo`), patch-name LCD with placeholder text "—" and the
   ◀ / 📂 / ▶ buttons. Mount the Output Filter physical switch and the
   Ladder Effect rocker; both subscribe to `hardware_strict` and
   disable when it's on. Mount the stacked L/R `level-meter-mini` cell
   bound to `peakL` / `peakR` from the `meterData` event.
2. **Modal host** in `ui/src/modals/modal-host.js`. Exposes
   `openModal(node)` and `closeModal()`. Manages a dim overlay, Esc
   handler, click-outside-the-panel suppression of main-UI clicks.
4. **Settings modal** in `ui/src/modals/settings.js`. Mount each
   control bound to its apvts param.
   - HARDWARE STRICT toggle: on flip, the FM panel + header re-render
     to show the clamped POLY value and disabled filter/ladder
     toggles.
   - RESET ALL: open a confirmation modal; on confirm, call the
     `resetAllToDefaults` native function.
5. **About modal** in `ui/src/modals/about.js`. Static content built
   from the view 7 template; the attribution table is hard-coded
   matching the view-7 list.
5. **`main.js`** mounts `header` and (on `mode_select` change) the
   appropriate panel into `#mode-panel`. The gear icon's click handler
   opens Settings; the wordmark opens About. No status-bar mount —
   the L/R meters live inside the header now.
7. **C++** — `resetAllToDefaults` native function. `pushPolyphonyParameters`
   honours `hardware_strict`. Voice / VoiceAllocator's FLOAT_MUL /
   AUTO_RETRIG allocation honours the "first-voice only" rule.
   processBlock's filter / ladder application honours the forced-on
   semantics.
8. **UI scale** — on `ui_scale` change, the editor (C++ side) resizes
   the WebView host (`setBounds(1200*N, 560*N)` adjusts the host
   component); the page itself doesn't need to scale because the
   WebView upscales the bundle. Verify on each scale that the page
   still reads cleanly (no scrollbars; no clipped controls).

## Deliverables

- New `ui/src/header.js` (carries the persistent header *and* the L/R
  output-meter cell; there is no separate status-bar module).
- New `ui/src/modals/{modal-host,settings,about}.js`.
- Updated `ui/src/main.js` (mounts header; gear / wordmark click
  handlers).
- Updated `src/PluginProcessor.{h,cpp}` (`resetAllToDefaults` native
  fn, HARDWARE STRICT clamp + force, FLOAT_MUL/AUTO_RETRIG first-voice
  fallback).
- Updated `src/PluginEditor.{h,cpp}` (UI scale resize, `patchNav`
  stub native fn).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green.
2. **Header round-trip**:
   - Mode selector flips the panel; the previous panel unmounts, the
     new one mounts.
   - Output Filter toggle (LEGACY ↔ CRYSTAL CLEAR) audibly switches
     the filter on / off.
   - Ladder Effect rocker audibly switches the ladder on / off in
     FM and D modes; greyed out / inert in SQ.
   - VOL knob scales the output amplitude.
3. **Header output meters** — the stacked L / R `level-meter-mini`
   cell tracks the audible output across modes.
4. **NOTE ON cluster** — the 16 px LED lights when any voice is
   sounding (FM/SQ) or when input signal exceeds the threshold (D);
   the `NOTE ON` text caption beneath it is visible at all times.
5. **Settings** — gear icon opens Settings.
   - HARDWARE STRICT on → FM panel POLY stepper clamps to 6 (the
     stepper's max becomes 6; existing >6 values clamp back); Filter
     + Ladder toggles disable in the header and visually show
     "forced on" (lit, not interactable). Audio path forces both on
     regardless of any prior apvts value.
   - UNISON DETUNE — sweep 0 → 50 ¢ with `poly_voices = 8` and play
     a held note; symmetric unison spread is audible.
   - UI SCALE — pick 2×; the WebView grows to 2400×1120; everything
     scales crisply.
   - AFTERTOUCH = Off — channel pressure has no effect. = LFO depth
     — channel pressure rides PMS. = Carrier TL — channel pressure
     attenuates the carrier op(s).
   - VELOCITY → TL on → soft notes are quieter; off → soft notes are
     the same level as hard.
   - TOOLTIPS off → hover tooltips do not appear.
   - RESET ALL → confirmation modal → confirm → every apvts param
     snaps to its default; the active patch path clears.
6. **About** — wordmark click opens; attribution table is exactly the
   view-7 list.
7. **Modal behaviour** — only one modal at a time (opening About from
   Settings replaces Settings); Esc dismisses; clicks outside the
   modal panel are absorbed (main UI does not receive them); the
   notification toast can still appear over an open modal.
8. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.

## Done when

- [ ] Header renders at the top of the chassis across every mode and
      carries the stacked L/R output meter cell + `note-on` cluster.
- [ ] No bottom status bar appears anywhere — the v2 first-pass
      status-bar surface is gone; the chassis bottom edge is the mode
      panel's bottom edge.
- [ ] Every control on view 1 + view 6 + view 7 is mounted and bound;
      behaviour matches the *Behaviour* / *Controls* notes in those
      views.
- [ ] The version string appears only in the About modal (view 7).
- [ ] HARDWARE STRICT clamps poly_voices to 6, falls back FLOAT_MUL /
      AUTO_RETRIG to single voice, forces filter + ladder on.
- [ ] UI scale flips between 1× / 2× / 3× cleanly; no clipped
      controls; no scrollbars.
- [ ] RESET ALL TO DEFAULTS resets every apvts parameter and clears
      the patch path after confirmation.
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
