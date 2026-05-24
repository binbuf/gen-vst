# Task 13 — SQ & D section views, modals

> **Depends on:** Task 07, Task 11.
> **Design references:** `docs/design/08-ui-views.md` (primary — views 2, 3, 5,
> 6, 7, 8, and *Modal behaviour (shared)*), `docs/design/05-ui-ux.md` (*C++ → JS
> notifications*, *Component Inventory*), `docs/design/03-psg-synthesis.md`,
> `docs/design/07-feature-spec.md`, ADR-0014, ADR-0016, ADR-0017.

## Objective

Build every remaining non-browser UI surface: the **SQ (PSG)** and **D (DAC)**
section views, the **Settings**, **MIDI routing** and **About** modals, the
**notification toast**, and the shared modal framework. After this task the
plugin's whole UI exists except the patch browser modal (Task 14).

## Context & key constraints

- **One OS window.** Every modal is an **in-WebView overlay layer** (a DOM layer
  on the same canvas), not a separate OS window (`08-ui-views.md` *Conventions*).
- **Shared modal behaviour** (`08-ui-views.md` *Modal behaviour (shared)*): open
  over a dimmed main UI; one modal at a time; dismiss by `Close` / `[X]` / `Esc`;
  clicks outside the panel do not reach the main UI; the toast may still appear
  above a modal. Views 5 and 7 open *from* view 6 and replace it.
- Each surface is fully specified in `08-ui-views.md` — build to those specs.
  All controls are `apvts` parameters bound through the Task 10 widgets unless
  noted.

### SQ (PSG) section — view 2

Shown in the bottom region when the `SQ` pill is selected (the `selectSection`
scaffold exists from Task 11). Section-header band (`PSG MIX` slider, `LAYER`
toggle) + **3 tone panels + 1 noise panel**, mirroring the FM four-panel rhythm.
Tone panel: activity LED, `MIDI` step-field, `VOL` knob, `PAN` slider, `BEND`
toggle, read-only `note` readout. Noise panel: same plus `TYPE`
(periodic/white), `RATE` (HIGH/MID/LOW/CH2), `AUTO` toggle. These bind to the
PSG parameters created in Task 07.

### D (DAC) section — view 3

Shown when the `D` pill is selected. Section header (`ENABLE` toggle, `MIDI`
step-field) + a SAMPLE strip (`LOAD WAV…` → native file chooser, filename, a
green-LCD **waveform display** of the loaded sample, length + bit-depth,
`CLEAR`) + a PLAYBACK group (`RATE` 8000/11025/22050, `MODE` one-shot/loop,
`LEVEL` knob). Empty state: `— no sample —`, blank waveform, `CLEAR` disabled.
New widget: `waveform-display`. `LOAD WAV…` triggers the Task 07 WAV loader via
a native `juce::FileChooser` (`08-ui-views.md` view 11).

### Settings modal — view 6

Opened from the header gear icon. Controls: `VOICE COUNT` (8/12/16),
`PITCH BEND RANGE` (±1/±2/±7/±12), `UI SCALE` (1×/2×/3×), `VELOCITY → TL`
toggle, `AFTERTOUCH` (Off / LFO depth / Carrier TL), and buttons opening the
MIDI routing and About modals. Bend range, velocity→TL and aftertouch
parameters exist from Task 06; declare `voiceCount` and `uiScale` parameters
here if absent. `VOICE COUNT` becomes functional in Task 15; `UI SCALE` becomes
functional in Task 17 — wire the controls to their settings now so those tasks
only add the consuming behaviour.

### MIDI routing editor — view 5

A modal with one row per destination (6 FM parts, 3 PSG tone slots, PSG noise,
DAC), each with a MIDI-channel selector (1–16 or `Off`). **Conflict
highlighting:** if two destinations share a channel, flag both rows and show a
warning line — sharing is permitted but must be visible. `Reset to defaults`
restores FM 1–6 / PSG 11–14 / DAC 16. The table is the same routing data the
inline `MIDI` step-fields in views 1/2/3 edit.

### About modal — view 7

Version + the **license attributions** (legally required — GPL v3 with bundled
third-party code). Content per `08-ui-views.md` view 7; keep the attribution
list in sync with the *Legal Notes* table in `04-patch-system.md`.

### Notification toast — view 8

The single user-visible error/status channel. Driven by the C++→JS `notify`
event `{ level, message }` (`05-ui-ux.md` *C++ → JS notifications*). Slides down
below the header; `info`/`warn`/`error` color levels; auto-dismiss ~4 s, click
to dismiss; at most two visible, the rest queue. Wire the `notify` event
end-to-end so the patch-load failures already produced by Tasks 08/09 (bad
file, rejected DMP version, missing custom root) surface here.

## Scope

- The SQ and D section views, wired into `selectSection`.
- The `waveform-display` and `notification-toast` widgets.
- The Settings, MIDI routing, and About modals + the shared modal framework.
- The `notify` C++→JS event end-to-end.
- Native `juce::FileChooser` for `LOAD WAV…`.
- `voiceCount` / `uiScale` parameters if not already declared.

## Out of scope

- The patch browser modal, Import/Export choosers, drag-and-drop → Task 14.
- Making `VOICE COUNT` functional → Task 15; making `UI SCALE` functional →
  Task 17.
- The WebView fallback panel (view 9) → Task 17.

## Implementation steps

1. Build the shared modal framework (overlay layer, dim, one-at-a-time, Esc/X).
2. Build the SQ and D section views; add `waveform-display`; wire `selectSection`
   so FM/SQ/D switch the bottom region.
3. Wire `LOAD WAV…` to a native `juce::FileChooser` → the Task 07 DAC loader.
4. Build the Settings, MIDI routing, and About modals.
5. Build the `notification-toast` widget and wire the `notify` event; route
   Task 08/09 load failures to it.

## Deliverables

`ui/src/views/sq-view.*`, `ui/src/views/d-view.*`,
`ui/src/widgets/waveform-display.*`, `ui/src/widgets/notification-toast.*`,
`ui/src/modals/*` (settings, midi-routing, about, modal framework), updates to
`src/PluginEditor.{h,cpp}` (the `notify` event, the WAV file chooser) and
`createParameterLayout()` if new parameters are needed.

## Verification

1. **SQ view:** select the `SQ` pill — the bottom region shows 3 tone + 1 noise
   panels. Editing `VOL`/`PAN`/`TYPE`/`RATE` etc. changes the PSG sound from
   Task 07 exactly as the inline parameters did.
2. **D view:** select `D` — load a WAV via `LOAD WAV…`; the waveform display
   shows the sample with its length/bit-depth; `RATE`/`MODE`/`LEVEL` affect
   playback; `CLEAR` empties it and disables itself; empty state shows
   `— no sample —`.
3. **Settings:** the gear icon opens the modal over a dimmed UI; every control
   reads/writes its setting; `MIDI ROUTING…` and `ABOUT…` open views 5/7,
   replacing the settings panel.
4. **MIDI routing:** the table lists all destinations; assigning two
   destinations the same channel flags both rows + shows the warning;
   `Reset to defaults` restores FM 1–6 / PSG 11–14 / DAC 16; changes match the
   inline `MIDI` step-fields.
5. **About:** shows the version and the attribution list.
6. **Modal behaviour:** `Esc` / `[X]` / `Close` all dismiss; clicks outside a
   modal do not reach the main UI; only one modal opens at a time.
7. **Toast:** import a corrupt patch and a non-v11 `.dmp` — a `notify` toast
   appears with the right level and message and auto-dismisses; a missing custom
   root also raises one.
8. `pluginval --strictness-level 8` passes.

## Done when

- [ ] SQ and D section views are built and switch via the FM/SQ/D pills.
- [ ] Settings, MIDI routing, and About modals work with shared modal behaviour.
- [ ] The notification toast surfaces real patch-load failures via `notify`.
- [ ] `LOAD WAV…` uses a native file chooser.
- [ ] `pluginval` passes.
