# Gen VST — mvp2 Implementation Task Plan

The **v2 implementation chain.** Sequential tasks that move the codebase
from its v1 (Genny-parity) state to the v2 design described in
`docs/design/` (ADRs 0021–0025 + the v2 docs `01`–`09`).

The v1 task chain (`docs/tasks/mvp/`) is the historical record of the
engine and infrastructure code these tasks build on. The mvp2 chain is
narrower — it rewrites the UI against the modern hardware-VST aesthetic,
collapses the C++ from six-part multitimbral to single-engine three-mode,
adds the D-mode audio FX, and lands the tagged unified preset browser.

## How to use a task file

Same conventions as `mvp/README.md`:

1. Read the file top to bottom.
2. Read every doc under **Design references** — those carry the authoritative
   detail (palette tokens, register tables, schema layouts). Tasks point at
   them rather than duplicating them.
3. Run the **Implementation steps** in order.
4. Run the **Verification** section. Every task ends with concrete runnable
   checks. If verification fails, the task is not done — do another pass.
5. Only then move on. Tasks are strictly sequential; a task assumes every
   lower-numbered task is complete and verified.

## Design source of truth

- `docs/design/01-architecture.md` … `09-visual-spec.md` — v2 design docs.
- `docs/design/adr/0001`…`0025` — ADRs (0007/0013/0014 superseded by
  0021/0023; 0021–0025 are the v2 decisions, all *Accepted 2026-05-24*).
- `docs/design/reference/` — RYM2612 + PCM2612 visual references.

If a task and a design doc disagree, the design doc + ADRs win — stop and
flag the discrepancy rather than guessing.

## Granularity

Tasks are coarse on purpose: each one is a complete, independently
verifiable increment sized so it can be implemented correctly in a single
focused pass. The mockup phase is a deliberate exception — it produces
throwaway HTML/CSS scaffolding so the v2 visual identity is locked **before**
any C++ churn lands, then exits before it grows into dead-end interactivity.

## End-to-end-first sequencing

After the mockup phase exits, every implementation slice is verified by
building, loading, and listening/clicking in a real host. There is no long
stretch of unverifiable work.

### Milestone map

| After task | Milestone | What you can do |
|-----------|-----------|-----------------|
| 01 | **Visual lockdown** | Open the static mockup pages via `npm run dev`; side-by-side compare each panel against `RYM2612-panelfront.jpg` / `pcm2612-VST.jpg`; visual identity is locked before any C++ churn |
| 02 | **Clean baseline** | v1 multitimbral C++ + v1 UI gone; apvts collapsed to single-engine; plugin still builds, loads, and renders a placeholder editor; existing FM/SQ engines still pass audio when driven directly |
| 03 | **Three modes audible** | D mode passes audio input through the decimator; mode dispatch + audio input bus wired; OutputFilter + LadderEffect DSP stages exist |
| 04 | **Widget library** | Every v2 widget exists as a real JS module bound through the relay layer; gallery page exercises each |
| 05 | **FM mode plays** | FM mode plays MIDI through the full RYM2612-style FM panel UI; FREQ CTRL MODE INT/FLOAT/AUTO RETRIG paths work |
| 06 | **SQ mode plays** | SQ mode plays MIDI through the 3 tone + 1 noise SN76489 panel UI |
| 07 | **D mode plays** | D mode processes audio input through the spartan D panel UI (DRY/WET + MONO); the header's mode-aware DAC PRESCALER drives decimation |
| 08 | **Header + Settings** | Header mode selector / patch LCD / Output Filter + Ladder Effect toggles / VOL / NOTE ON LED; Settings + About modals open from the gear icon |
| 09 | **Tagged preset browser** | One unified browser shows FM and SQ presets; loading auto-switches mode; `.psg` format works; 3 smoke-test SQ presets in factory. D mode has no preset format (per ADR-0025) — header patch chrome is greyed in D |
| 10 | **SQ preset ecosystem** | DMP PSG instruments import as SQ presets (ADR-0026); full 12-preset factory SQ library tuned by ear |
| 11 | **Release-ready** | DAW state save/restore works; v2 parity audit clean; pluginval passes cross-platform |

## Task index

| # | Task | Delivers | Depends on |
|---|------|----------|-----------|
| 01 | [Static UI mockup (HTML/CSS, throwaway)](01-static-ui-mockup.md) | Visual lockdown | — |
| 02 | [Strip v1 multitimbral C++ + v1 UI](02-strip-v1.md) | Clean baseline | 01 |
| 03 | [DSP foundations, mode dispatch & audio input bus](03-dsp-foundations.md) | Three modes audible | 02 |
| 04 | [v2 widget library & gallery](04-widget-library.md) | Widget library | 03 |
| 05 | [FM panel (+ FREQ CTRL MODE register paths)](05-fm-panel.md) | FM mode plays | 04 |
| 06 | [SQ panel](06-sq-panel.md) | SQ mode plays | 04 |
| 07 | [D panel](07-d-panel.md) | D mode plays | 04 |
| 08 | [Header, Settings & About modals](08-header-and-modals.md) | Header + Settings | 05, 06, 07 |
| 09 | [Tagged preset browser & .psg format](09-preset-browser.md) | Tagged preset browser | 08 |
| 10 | [SQ preset ecosystem: DMP PSG import + factory library](10-sq-preset-library.md) | SQ preset ecosystem | 09, 06 |
| 11 | [State persistence & v2 parity audit](11-state-and-qa.md) | Release-ready | 10 |

## Task file structure

Every task follows the same shape:

- **Header** — milestone tag, dependencies, design references.
- **Objective** — what this task delivers and why.
- **Context & key constraints** — gotchas and decisions from the design.
- **Scope** / **Out of scope** — what is in, and what defers (to which task).
- **Implementation steps** — concrete, ordered.
- **Deliverables** — files created or changed.
- **Verification** — runnable checks with explicit pass criteria.
- **Done when** — a final checklist.

## Open design questions (v2)

Tracked in `docs/design/07-feature-spec.md` *Open Questions*. Each is owned
by the task that implements that feature; the task records the design's
proposed default and instructs the implementer to verify against the cited
reference.

1. CPU profiling pass for 16 ymfm instances at 44.1 kHz — Task 10.
2. Instrument-with-audio-input host quirks (Logic, Pro Tools) — Task 03 verification.
3. Ladder effect curve calibration vs measured YM2612 clips — Task 03.

Resolved during the post-mockup review (no longer open):
- *Mono default*: `note_mode = RETRIG`; the LEGATO/RETRIG toggle on the FM
  panel exposes both.
- *Unison*: dropped from v2 MVP (no RYM2612 reference); see *Post-MVP
  backlog* below.
- *Aftertouch routing default*: LFO PMS.

## Post-MVP backlog (not scheduled here)

- **UNISON DETUNE (FM mode)** — per-note voice fan-out with cents detune
  spread across the active poly stack. Not present on the RYM2612
  reference (`docs/design/reference/RYM2612-User-Manual.pdf`); dropped
  from v2 MVP during the post-mockup review. Bringing it back needs:
  an enable toggle on the FM panel (the v2-draft UNISON sub-mode was
  removed and not replaced), a re-think of POLY voice allocation when
  one MIDI note grabs N voices (chord behaviour, stealing rules), and
  probably its own ADR for the semantic.
- Resizable window (ADR-0023 fixes 1200×560).
- Multi-instrument OPM bank import (Task 05 ports the first `@:` block only).
- Per-channel independent tuning tables, `.kbm`, MTS Sysex (07-feature-spec
  *Microtuning*).
- CLAP build target (ADR-0008).
- Channel 3 special mode as a standalone editor surface (ADR-0014).
- v2-tagged bank format mixing FM and SQ presets into one shareable
  file (04-patch-system *retired `.gnbank`*). D mode has no preset
  format ([ADR-0025](../../design/adr/0025-tagged-preset-browser.md))
  so it has nothing to contribute to a bundled bank.
- Signed/notarized macOS `.pkg`; `.deb`/AppImage for Linux (ADR-0016).
- Cross-OS portability for custom-root paths (existing limitation,
  carried).
- **MIDI Program Change handling** (moved from `07-feature-spec.md`
  *MIDI* section by mvp2/11). `PatchBrowser::factoryPatchByIndex` is
  the source-of-truth pool, but `dispatchMidi` does not yet handle
  `juce::MidiMessage::isProgramChange`. Wiring is small but needs an
  audio-thread-safe per-mode "current pool" cache and a decision on
  how PC interacts with the v2 single-engine + auto-mode-switch model
  (PC selects the Nth patch of the active mode, never flips mode —
  ADR-0025).
- **MIDI Reset All Controllers (CC 121)** (moved from `07-feature-spec.md`
  *MIDI* section by mvp2/11). Not wired in `handleControlChange`. The
  spec calls for resetting mod-wheel mirror, pitch-bend mirror, and
  sustain-pedal state on receipt; clients today must send CC 120 (All
  Sound Off) for a clean reset.
- **Standalone `.gnvst` state file** (moved from `07-feature-spec.md`
  *State* section by mvp2/11). The DAW project envelope covers the
  in-session round-trip (mvp2/11); a freestanding file for cross-
  session / cross-machine portability needs its own ADR (filename,
  magic bytes, version handshake, captured-state subset) and a
  dedicated task.
- **CPU profiling pass on v2 final build** (carried from
  `07-feature-spec.md` *Open Questions* #1). The 16-voice + AUTO_RETRIG
  + LFO + Filter + Ladder + telemetry-active stress measurement against
  Reaper has not yet been recorded; if the figure exceeds the 30%-of-
  one-core target, optimisation (ymfm batch-render, decimator
  vectorisation) is the post-MVP follow-up.
- **Cross-platform pluginval / CI matrix verification on v2 final
  build** (carried from mvp2/11 Verification). The Windows + macOS +
  Linux GitHub-Actions matrix and the `pluginval --strictness-level 8`
  pass on each platform's artefact must be run against the v2 release
  branch and any failures fixed before shipping; this is a release-
  prep checklist item rather than a code change and is left to the
  release engineer.

Picking these up later is a follow-up planning exercise; do not implement
them as part of the mvp2 sequence.
