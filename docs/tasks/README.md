# Gen VST — Implementation Task Plan

This directory holds the sequential implementation tasks for Gen VST. Each
`NN-*.md` file is a **self-contained work order**: it can be executed by a
developer or an LLM with no further instruction beyond the file itself and the
design documents it references.

## How to use a task file

1. Read the task file top to bottom.
2. Read every document listed under **Design references** — those docs hold the
   authoritative detail (register tables, byte layouts, layout specs). Task
   files deliberately do **not** duplicate them; they reference them and call
   out the gotchas.
3. Implement the **Implementation steps** in order.
4. Run the **Verification** section. Every task ends with concrete, runnable
   checks. If verification fails, the task is not done — do a second pass.
5. Only then move to the next-numbered task.

Tasks are strictly sequential. A task may assume everything in all
lower-numbered tasks is complete and verified.

## Design source of truth

- `docs/design/01-architecture.md` … `08-ui-views.md` — the design docs.
- `docs/design/adr/0001`…`0019` — Architecture Decision Records (all Accepted; 0018 superseded by 0019).
- `docs/genny-ui.md` — authoritative visual-style spec for the main window.

If a task file and a design doc ever disagree, the design doc + ADRs win —
stop and flag the discrepancy rather than guessing.

## Granularity

Tasks are coarse on purpose: each one is a complete, independently verifiable
increment, sized so it can be implemented correctly in a single focused pass.
The FM register/voice code, the voice allocator, and each UI layer are kept as
separate tasks because merging them further would create work orders too large
to get right in one go.

## Dev environment & tooling

Set this up once (see Task 01 for the canonical CMake configuration):

| Need | Tool | Notes |
|------|------|-------|
| Build | CMake ≥ 3.22 + C++20 compiler | MSVC 2022 on Windows (primary dev OS) |
| Web UI build | Node.js ≥ 20 (`npm`) | CMake drives the Vite build; `npm` must be on `PATH` |
| Unit tests | GoogleTest via `ctest` | Fetched by CMake; see `docs/design/06-build-system.md` |
| Plugin validation | `pluginval` (Tracktion) | `pluginval --strictness-level 8 --validate <plugin.vst3>` |
| Manual audio test | The **Standalone** build, or any VST3 host | Reaper recommended on Windows for fast iteration |
| UI hot-reload | Vite dev server | Configure `-DGENVST_DEV_SERVER=ON`, run `npm run dev` in `ui/` |

Canonical build loop (Windows, Debug):

```
cmake --preset windows-debug
cmake --build build/windows-debug
ctest --test-dir build/windows-debug --output-on-failure
```

The **Standalone** artifact is the fastest manual-test surface — it needs no
DAW and, with `GENVST_DEV_SERVER=ON`, hot-reloads the UI. Use a DAW (Reaper)
when a check specifically needs host automation, project save/reload, or
plugin-format validation.

## End-to-end-first sequencing

The plan is ordered so the three highest-risk integration surfaces — the
plugin build/format, ymfm audio correctness, and the WebView C++↔JS contract —
are each proven by a runnable plugin **within the first three tasks**. After
that, every task is verified by building, loading, and listening/clicking in a
real host. There is no long stretch of unverifiable work.

### Milestone map

| After task | Milestone | What you can do |
|-----------|-----------|-----------------|
| 01 | **E2E #1 — it builds** | Plugin builds as VST3 + Standalone, loads in a DAW, passes `pluginval` |
| 02 | **E2E #2 — it sounds** | A MIDI note produces FM sound in the Standalone |
| 03 | **E2E #3 — it has a UI** | The WebView renders the Genny chassis; a knob bound to a real parameter audibly works |
| 06 | **Playable FM instrument** | 6-part multitimbral, 16-voice polyphonic FM, MIDI-driven, DAW-automatable |
| 07 | **All three sound chips** | FM + SN76489 PSG + DAC sample channel all produce sound |
| 14 | **Full UI** | Every view, section, and modal is built and wired |
| 16 | **Feature-complete MVP** | Polyphony modes + full DAW state save/restore |
| 19 | **Release-ready** | Cross-platform, installer, CI, profiled, parity-checked |

## Task index

| # | Task | Delivers | Depends on |
|---|------|----------|-----------|
| 01 | [Repo skeleton & buildable empty plugin](01-repo-skeleton.md) | E2E #1 | — |
| 02 | [ymfm single-voice audio & render pipeline](02-ymfm-single-voice-audio.md) | E2E #2 | 01 |
| 03 | [WebView shell, design system & static chassis](03-webview-shell.md) | E2E #3 | 01, 02 |
| 04 | [Patch model, TFI loader & FM register mapping](04-patch-model-tfi-register-mapping.md) | Real patches play | 02 |
| 05 | [Voice allocator & parameter system](05-voice-allocator-parameter-system.md) | 16-voice polyphony | 04 |
| 06 | [MIDI pipeline](06-midi-pipeline.md) | **Playable FM instrument** | 05 |
| 07 | [SN76489 PSG & DAC engine](07-psg-and-dac-engine.md) | **All three sound chips** | 06 |
| 08 | [VGI & DMP loaders, patch export](08-vgi-dmp-loaders-export.md) | All patch formats | 04 |
| 09 | [Patch browser backend](09-patch-browser-backend.md) | Folder-tree patch model | 05, 08 |
| 10 | [Core interactive widget library](10-core-widget-library.md) | Reusable UI widgets | 03 |
| 11 | [Main FM view & channel paging](11-main-fm-view.md) | The FM editing screen | 05, 10 |
| 12 | [Telemetry — oscilloscope, VU, LEDs](12-telemetry.md) | Live meters | 06, 11 |
| 13 | [SQ & D section views, modals](13-sq-d-views-and-modals.md) | PSG/DAC UI + modals | 07, 11 |
| 14 | [Patch browser UI, import/export, drag-drop](14-patch-browser-ui.md) | **Full UI** | 09, 13 |
| 15 | [Polyphony modes & voice count](15-polyphony-modes.md) | Poly/Mono/Unison | 05, 11 |
| 16 | [State persistence](16-state-persistence.md) | **Feature-complete MVP** | 07, 09 |
| 17 | [WebView fallback panel & HiDPI scaling](17-webview-fallback-hidpi.md) | Editor robustness | 03 |
| 18 | [Cross-platform bring-up, installer & CI](18-cross-platform-installer-ci.md) | macOS/Linux + packaging | 01 |
| 19 | [CPU profiling, resampler quality & parity QA](19-profiling-and-qa.md) | **Release-ready** | 07 |
| 20 | [Y12 + OPM patch loaders](20-y12-opm-loaders.md) | Two more patch formats | 08 |
| 21 | [VGM bank import (Import Bank)](21-vgm-bank-import.md) | Game-original FM patches via .vgm/.vgz | 09, 20 |
| 22 | [Genny instrument rack & per-instrument routing](22-genny-instrument-rack.md) | User-curated rack; per-row MIDI/transpose/range/detune/balance | 16 |
| 23 | [PSG envelope panel & software ADSR](23-psg-envelope-panel.md) | SQ section matches FM envelope layout; per-PSG-channel ADSR | 22 |
| 24 | [IMPORT tab full action stack](24-import-tab-actions.md) | 8-button stack: Import/Export Bank+Instrument, Load/Save State, Log VGM, Import Tuning | 22 |
| 25 | [Header polish: TRUE STEREO toggle](25-header-true-stereo.md) | TRUE STEREO toggle + mono-sum branch | 22 |
| 26 | [Genny visual polish & algorithm-diagram audit](26-genny-visual-polish.md) | Algorithm correctness, palette, bracket glyphs, DAC restyle | 22, 23, 24, 25 |
| 27 | [LFO waveform selector](27-lfo-waveform-selector.md) | Saw/Square/Triangle/Noise waveform per part; YM2612 reg 0x22 bits 0–1 | 22 |
| 28 | [Glide time per-instrument routing (DEL)](28-glide-time-routing-param.md) | DEL slider in routing strip; linear pitch-slide for FM + PSG tone in Mono/Legato mode | 15, 22 |
| 29 | [VGM logging (Log VGM button)](29-vgm-logging.md) | Capture every YM2612 + SN76489 register write to a `.vgm` file | 07, 24 |
| 30 | [Scala tuning import (Import Tuning button)](30-scala-tuning.md) | `.scl` parser → per-MIDI-note frequency lookup for FM + PSG | 06, 24 |
| 31 | [Multi-sample DAC (note-mapped sample kit)](31-dac-multisample.md) | Per-cell WAV load for the 4×5 note grid; one sample per cell | 07, 26 |
| 32 | [Cosmetic cleanup (Genny-polish leftovers)](32-cosmetic-cleanup.md) | Palette-var moves for specular/bevel hex; algo-diagram line tweak | 26 |

## Task file structure

Every task file follows the same shape:

- **Header** — milestone tag, dependencies, design references.
- **Objective** — what this task delivers and why.
- **Context & key constraints** — gotchas and decisions pulled from the design.
- **Scope** / **Out of scope** — what is in, and what is deferred (and to which task).
- **Implementation steps** — concrete, ordered.
- **Deliverables** — files created or changed.
- **Verification** — runnable checks with explicit pass criteria.
- **Done when** — a final checklist.

## Open design questions

The design docs list a handful of implementation-time verification items
(mono retrigger-vs-legato default, unison spread default, aftertouch routing
default). Each is owned by the task that implements that feature; the task
instructs the implementer to verify against the cited reference and records
the design's proposed default. The *VGI TL range* and *DMP v11 byte offsets*
items were resolved during Task 08 — see
[07-feature-spec.md](../design/07-feature-spec.md) under *Resolved during
Task 08* and the updated DMP table in
[04-patch-system.md](../design/04-patch-system.md).

## Post-MVP backlog (not scheduled here)

These are explicitly deferred by the design/ADRs and have **no task file**:

- CLAP build target via `clap-juce-extensions` (ADR-0008).
- Channel 3 special mode — per-operator independent pitch (ADR-0014).
- Chord mode — note-range zones triggering part configurations (07-feature-spec).
- Resizable window (ADR-0007 — currently fixed 960×640).
- Legacy DMP version 8 import (ADR-0012 — v11 only for MVP).
- Multi-instrument OPM bank import (Task 20 loads the first `@:` block only).
- VGM extraction with timeline scrub / per-channel preview (Task 21 is one-click bank import only).
- Per-part voice reservations / caps (ADR-0013).
- PSG Option B auto-layer refinements; bank-select for user/custom roots.
- Signed/notarized macOS `.pkg`; `.deb`/AppImage for Linux (ADR-0016).

Picking these up later is a follow-up planning exercise; do not implement them
as part of the MVP sequence.
