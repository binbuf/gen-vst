# ADR-0021: Three-mode single-engine instrument

- **Status:** Accepted
- **Date:** 2026-05-24
- **Supersedes:** [ADR-0013](0013-multitimbral-voice-model.md) (multitimbral parts model); the per-channel-routing portions of [ADR-0014](0014-special-channel-features.md)
- **Related:** [01-architecture.md](../01-architecture.md), [ADR-0022](0022-modern-vst-aesthetic.md), [ADR-0025](0025-tagged-preset-browser.md)

## Context

The v1 architecture (ADR-0013) modelled the plugin as a six-part multitimbral
Genesis-in-a-box: one plugin instance hosted 6 FM parts + 3 PSG tones + 1 PSG
noise + 1 DAC, each with its own MIDI channel, routed via an internal matrix.
This mirrored hardware authoring but is **out of step with modern DAW
workflow**, where producers expect one track per timbre, per-track FX,
per-track automation, and freeze/render at the track level. The v1 UI ended up
spending most of its 960×640 canvas on the rack list + routing strip + section
tabs rather than on the synth controls themselves.

The reference instrument for the v2 direction is Inphonik's **RYM2612**:
single FM patch per instance, ~16-voice polyphony, every control on one panel,
no internal routing. The user explicitly chose this paradigm during the
2026-05-24 design pivot, extended to cover Gen VST's three Genesis chip
engines via a per-instance **mode**.

## Decision

Gen VST is a **single-engine instrument** with a per-instance **mode** of
**FM**, **SQ** (SN76489 PSG), or **D** (8-bit PCM DAC). Each plugin instance
runs exactly one of these engines at a time. To play multiple Genesis timbres
in a project, the user instantiates the plugin once per timbre — one track per
sound, native DAW workflow.

- **Mode** is a top-level enum on the audio processor (`mode_select` apvts
  param: `0=FM, 1=SQ, 2=D`).
- The active engine is selected per-instance and persists with the project.
- Mode is switched in two ways:
  - **Auto** — loading a tagged preset switches the instance to that
    preset's mode (see [ADR-0025](0025-tagged-preset-browser.md)).
  - **Manual** — a mode selector in the UI header.
- On manual mode switch, the instance **silently loads a sensible default
  preset for the new mode** (idiomatic modern multi-engine synth behaviour —
  cf. UVI Falcon, Plogue Chipsounds, Aly James Lab). The pre-switch patch
  remains saved on disk; nothing is lost.
- The MIDI input is a single channel per instance (the plugin's host channel).
  Per-channel routing tables, MIDI routing modals, per-part transpose/range/
  detune/balance, and per-part polyphony modes are removed from the
  user-facing surface.

**Voice model per mode:**

- **FM mode** — single FM patch + up to 16-voice polyphony, drawn from
  the shared `ymfm::ym2612` voice pool ([ADR-0010](0010-ymfm-instance-model.md)
  retained). All active voices play the one active patch; the active
  voice count is user-selectable via the `POLY` stepper on the FM panel
  (1–16) with a `HARDWARE STRICT` Settings toggle that clamps to 6 to
  match the real YM2612.
- **SQ mode** — the SN76489 PSG ([ADR-0009](0009-sn76489-library.md))
  with its native 3 tone + 1 noise channels exposed as four envelope strips.
  Voice allocation is round-robin LRU across the three tone channels (as
  shipped in Task 07); no part-level binding.
- **D mode** — an **audio FX**, not an instrument. Modelled on Inphonik's
  PCM2612 "Retro Decimator Unit": the plugin's audio input bus is run
  through a sample-rate decimator + 8-bit quantizer + MONO collapse + DRY/WET
  mix, then through the global Ladder + Output Filter stages
  ([ADR-0024](0024-hardware-filter-toggles.md)). MIDI input is ignored in
  D mode; WAV loading and sample playback do **not** exist. The v1
  `DACPlayer` (ymfm DAC instance + WAV decoder) and Task 31's `DACKit`
  multi-sample grid are both **deleted**, replaced by ~80 lines of pure DSP
  in a new `src/DspDecimator.{h,cpp}`.

**Audio input bus.** Because D mode processes audio, the plugin declares
an **always-present audio input bus** in its JUCE bus configuration. The
bus is silent / unread in FM and SQ modes (the engine still emits via the
output bus); in D mode the input bus is the source. Most hosts (Reaper,
Bitwig, Logic, Cubase) treat "instrument with audio input" natively;
host-specific quirks are flagged for the v2/02 task verification.

**Engine code is reused.** The ymfm voice pool, SN76489 wrappers, patch
loaders, tuning system, and VGM tooling from v1 stay; only their wiring
collapses to one active engine per instance, and the DAC ymfm instance is
removed since D mode no longer uses ymfm at all.

## Consequences

- `PartManager` is **deleted**. The six-part skeleton is not retained
  "in case" — keeping inert structure for hypothetical future use is the kind
  of speculative design the project discipline explicitly avoids. The single
  active patch lives directly on the processor.
- The apvts collapses from ~300 FM params (6 parts × ~50) to **~50 FM params**
  (one patch's worth) + the SQ params + the D-mode DSP params (`prescaler`,
  `mono`, `dry_wet`) + the global params (mode, output filter, ladder, master
  volume). All three modes' parameters coexist in the apvts even though only
  one is audible at a time — this lets a project save/restore preserve any
  incidental tweaks the user made before switching modes.
- `MidiRouter` is reduced to a thin shim: there is no destination table; the
  active engine receives every MIDI event on the host channel.
- The v1 instrument rack (Task 22), per-instrument routing strip, MIDI
  routing modal, voice-activity LED bank, and `FM/SQ/D` section tabs are
  removed from the UI. Voice activity is a single `NOTE ON` indicator like
  RYM2612.
- VGM logging ([Task 29](../../tasks/mvp/29-vgm-logging.md)) captures the active
  engine's register writes. A cross-instance VGM that interleaves several
  instances' writes into one file is out of v2 MVP scope.
- ADR-0007 (fixed 960×640 window) is superseded by
  [ADR-0023](0023-fixed-window-1200x560.md) — the v2 layout needs a wider,
  shallower canvas.
- The 16-voice pool of one-channel `ymfm::ym2612` instances ([ADR-0010])
  remains correct for FM mode and is unchanged.

## Alternatives considered

- **Keep PartManager + collapse UI only** — would let the engine retain
  multitimbral capability for a hypothetical "v3 multi" mode. Rejected: the
  speculative future doesn't justify the carrying cost (300 apvts params,
  MIDI router complexity, per-part state) when the v2 paradigm is decisively
  single-instance per timbre.
- **Modes as separate plugin binaries** (`GenVst-FM.vst3`, `GenVst-SQ.vst3`,
  `GenVst-D.vst3`) — true single-engine purity, but triples installer
  complexity, breaks the unified preset browser (ADR-0025), and prevents
  in-place mode switching for sound design. Rejected.
- **Keep the multitimbral rack as an optional "advanced" mode** — adds
  exactly the UI complexity v2 was meant to remove. Rejected.
