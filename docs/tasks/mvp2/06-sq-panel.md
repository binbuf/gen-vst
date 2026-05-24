# Task 06 — SQ panel

> **Milestone:** SQ mode plays — MIDI notes drive the 3 tone + 1 noise
> SN76489 panel UI; every per-channel envelope / volume / pan / detune
> control is bound; noise type / rate parameters work.
> **Depends on:** Task 04.
> **Design references:** `docs/design/08-ui-views.md` view 3 (primary —
> per-channel-strip layout), `docs/design/03-psg-synthesis.md`
> (SN76489 register protocol, allocation rules),
> `docs/design/07-feature-spec.md` (*SQ Features*), ADR-0009, ADR-0021.

## Objective

Assemble the SQ mode panel from Task 04's widget library and wire every
control to the apvts so the SN76489 engine — which already exists from
the v1 chain (Tasks 07 + 23) — drives sound from MIDI input through the
v2 layout.

The SN76489 wrappers, the SN76489Engine voice allocation (round-robin
LRU across the three tone channels + last-note priority on noise), and
the per-channel software ADSR envelope all carry forward unchanged. This
task replaces the v1 SQ view with the v2 layout per view 3 and binds
every control through the v2 widget library.

After this task: setting `mode_select = SQ` and playing MIDI through
the host produces SN76489 tones / noise that audibly respond to every
SQ panel control.

## Context & key constraints

- **Layout** (`08-ui-views.md` view 3): four vertical strips — three
  tone-channel strips and one noise strip. Each tone strip has an
  envelope-curve thumbnail at the top + the 5 envelope knobs
  (ATK/DR1/SUS/DR2/RR) + DETUNE + VOL + PAN. The noise strip drops
  DETUNE, gains TYPE (white/periodic) and RATE (low/mid/high/ch2)
  selectors.
- **SN76489 engine carries forward** from v1 — `SN76489Engine`,
  `SN76489Wrapper`, and the per-channel software-ADSR code from
  v1/Task 23 are still in `src/`. No engine rewrite; this task is the
  UI + apvts wiring.
- **MIDI routing** (`03-psg-synthesis.md` *MIDI Routing in v2*): every
  MIDI event on the host channel reaches the SQ engine; the engine's
  internal allocation rules pick which tone channel gets a note-on.
  No per-tone-channel MIDI bindings (the v1 `psg_midi_ch*` params are
  gone — Task 02 removed them).
- **Per-channel envelope** (`03-psg-synthesis.md` *PSG Voice Allocation*
  + Task 23 from v1): software ADSR overlays the SN76489's instant
  attack — `ATK / DR1 / SUS / DR2 / RR` are envelope rates / level in
  the same shape as FM (the envelope-curve widget can be reused).
- **Noise channel UX**: TYPE = white / periodic; RATE = low / mid /
  high / ch2 — both bound directly to apvts (the v1 "auto from MIDI
  note" option is an apvts toggle but the UI doesn't surface it; it
  lives in `psg_noise_auto`).
- **PSG mixing** (`03-psg-synthesis.md` *Mixing the four PSG channels*):
  every channel has a `psg_vol[ch]` (0..1) + a `psg_pan[ch]` (-1..+1)
  apvts param; the engine already mixes per-channel via these.
- **No `psg_mix` global** — Task 02 removed it (v1's "PSG contribution
  to the FM output" no longer exists; SQ is its own engine).
- **Glide-time** per tone channel — `psg_glide[0..2]` exists in apvts
  (Task 02 added it); the SQ panel does **not** surface it on the v2
  layout in view 3. Leave the param wired through to the engine (it's
  already there from v1 Task 28) but no UI control on this panel.

## Scope

- New `ui/src/views/sq-view.js` building the panel HTML per view 3.
- Mount + bind every control:
  - Per tone channel `0..2`: 5 envelope knobs, DETUNE, VOL, PAN, and
    the envelope-curve thumbnail (live-recomputed from the 5 envelope
    values).
  - Noise channel `3`: 5 envelope knobs (ATK/DR1/SUS/DR2/RR) + VOL +
    PAN; TYPE selector (combo: white / periodic) + RATE selector
    (combo: low / mid / high / ch2).
- `main.js` mounts `sq-view` into `#mode-panel` on
  `mode_select == SQ`.
- No new C++ work for the engine — the wiring already exists. *However*
  verify the PSG `noteOn` telemetry hook (Task 03 already pushes
  `noteOn = anyVoiceActive || anyPsgChannelGating`); the LED reads
  the same flag in FM and SQ.

## Out of scope

- Header — Task 08 (`NOTE ON` LED + label + mode selector +
  patch-name LCD + stacked L/R output meters all live in the header,
  not on this panel). The v2 first-pass status bar is gone — there is
  no separate bottom strip for this panel to coordinate with.
- `.psg` preset format + the tagged preset browser — Task 09.
- Auto noise-from-MIDI option UI — the apvts param exists but no view
  surface; revisit post-MVP if users ask for it.
- Per-tone-channel glide-time UI — apvts param exists but no view
  surface in v2 (08-ui-views view 3 does not call for it).
- HARDWARE STRICT effects — only FM is affected by it (Task 08).

## Implementation steps

1. **`ui/src/views/sq-view.js`** — export `mountSqView(host)` and
   `unmountSqView()`. Build the four-strip layout. Use the
   `design-system.css` chassis/inset/knob/lcd recipes.
2. Each tone strip:
   - Mount an `envelope-curve` widget at the top, fed by `bindSlider`
     for the 5 envelope params (`psg_atk[ch]` … `psg_rr[ch]`) — on
     any change, call `setEnvelope(atk, dr1, sus, dr2, rr)`.
   - Mount 5 knobs bound to those params.
   - Mount DETUNE knob bound to `psg_detune[ch]`.
   - Mount VOL knob bound to `psg_vol[ch]`.
   - Mount PAN slider bound to `psg_pan[ch]`.
3. Noise strip:
   - Mount the same envelope row.
   - Mount VOL + PAN knobs/slider.
   - Mount the TYPE combo bound to `psg_noise_type` (choices: white,
     periodic).
   - Mount the RATE combo bound to `psg_noise_rate` (choices: low,
     mid, high, ch2).
4. Update `main.js` mode-dispatch: on `mode_select` change, if the new
   mode is SQ, unmount the previous panel (if any) and call
   `mountSqView(modePanel)`.
5. (Optional) verify the engine still handles `psg_noise_auto`
   correctly — flip it via the host's generic editor; should drive
   the rate from MIDI note range when enabled. No UI surface change.

## Deliverables

- New `ui/src/views/sq-view.js`.
- Updated `ui/src/main.js`.
- No new C++ files (engine + apvts wiring carry forward).

## Verification

1. `cmake --build build/windows-debug && ctest --output-on-failure` —
   green.
2. Dev-server: open Standalone; set `mode_select = SQ`. The SQ panel
   renders per view 3.
3. **Per-channel envelope** — pick tone 1, set ATK to a high value
   (slow); play a note — the volume ramps in. Set RR high (slow
   release); release the note — the volume tails out. The envelope-
   curve thumbnail visually matches the played envelope shape.
4. **Voice allocation** — play four overlapping notes; the fourth
   steals tone 0 (LRU). Visible because changing tone 0's DETUNE
   audibly shifts the stolen voice.
5. **DETUNE / VOL / PAN** — each control audibly affects its channel
   in isolation.
6. **Noise** — play a noise note; toggle TYPE white→periodic — timbre
   shifts from random noise to buzzy. Switch RATE low→mid→high — pitch
   shifts up. Set RATE to ch2; change tone 2's pitch — noise pitch
   tracks.
7. **NOTE ON LED** (in the still-stub header) flickers on every PSG
   note-on (the header is laid out in Task 08; the telemetry already
   feeds the LED).
8. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` —
   passes.

## Done when

- [ ] SQ panel renders per `08-ui-views.md` view 3.
- [ ] Every control on the panel is two-way bound and audibly affects
      the SN76489 engine.
- [ ] Voice allocation rules (round-robin LRU tones, last-note noise)
      behave as before (no regression vs the v1 SN76489Engine tests).
- [ ] Mode switch FM ↔ SQ ↔ D cleanly mounts / unmounts the SQ view.
- [ ] `pluginval` strictness 8 passes; `ctest` is green.
