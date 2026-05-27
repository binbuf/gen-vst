# Changelog

All notable changes to Gen VST are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.2.1] — 2026-05-27

### Added

- **On-screen piano roll keyboard strip** — a Canvas-rendered 7-octave keyboard
  (C1–B7, MIDI 24–107) docked below the mode panel. Active MIDI notes light up
  in real time (fed from the C++→JS telemetry `activeNotes` bitmask). Clicking
  keys injects synthetic `noteOn` / `noteOff` events into the audio thread via
  a lock-free FIFO queue. Toggled on/off via a `keyboard_visible` apvts param
  (surfaced in Settings); the editor window resizes between 1200×560 (keyboard
  hidden) and 1200×660 (keyboard visible). See `08-ui-views.md` view 11.
- **`update-version.sh`** release helper — single-shot script that rewrites every
  pinned version string (CMakeLists.txt, juce_add_plugin, ui/package.json,
  ui/package-lock.json, Vite define, About modal fallback, release.yml
  dev-build fallback) so a release is one command plus a tag push.

### Fixed

- **FM mode audio — per-voice auto-idle silence detection** (`Voice.cpp`). A
  released YM2612 voice now monitors its own chip output and transitions to
  `Idle` state after 256 consecutive native-rate samples below amplitude 512
  (~4.8 ms). This eliminates the low-level residue hiss that ymfm's internal
  phase accumulators produce even after envelope decay — residue that was being
  amplified by the Ladder 8-bit quantizer into audible background noise between
  notes. Threshold constants (`kAutoIdleThreshold`, `kAutoIdleAmplitude`) are
  documented with derivation comments in `Voice.cpp`.
- **Preset browser modal layout** — the popup now holds a fixed-height layout
  with internally-scrolling tree/list panes. `openModal()` in `modal-host.js`
  now accepts an optional `panelClass`, which the preset browser passes as
  `preset-browser-panel`; previously that class was referenced only in CSS and
  never attached to any element, so the override block (95vh height,
  `overflow: hidden`, 720px width) was dead. Without it the panel inherited
  the base `max-height: 480px; overflow: auto` and the patch list re-flowed
  on every folder selection, overflowing the popup. Also reworked the inner
  grid template (`grid-template-rows: auto auto 1fr auto`) so the panes row
  — not the chip row — receives the flex `1fr`, giving `.pb-tree` and
  `.pb-list` a defined containing block for their `overflow-y: auto`.

### Changed

- **Factory patches reorganized into category subfolders** (`extern/patches/fm/<category>/*`,
  `extern/patches/sq/`) — the flat `extern/patches/*.tfi|.vgi|.dmp|.y12|.opm|.psg`
  layout is replaced by per-instrument-family folders (bass, brass, drums,
  keys, lead, pad, …). `PatchBrowser`, the patch-loader tests, and the
  `build.ps1` / `build.sh` patch-install steps were updated to walk the new
  tree recursively.
- macOS x86_64 CI runner migrated from the retired `macos-13` to `macos-15-intel`.

---

## [0.1.4] — 2026-05-26

### Changed

- CI: release workflow now **publishes GitHub Releases automatically** on `v*`
  tag push. Artifacts are attached directly to the release rather than uploaded
  to workflow artifact storage.
- macOS packages are now split into two separate installers:
  **Apple Silicon** (arm64) and **Apple Intel** (x86_64), replacing the single
  combined universal binary package.

---

## [0.1.3] — 2026-05-26

### Fixed

- Linux CI: added `JUCE_USE_CURL=0` to the test-runner `target_compile_definitions`
  to prevent link failures against libcurl on Ubuntu CI runners.

---

## [0.1.2] — 2026-05-26

### Fixed

- Linux CI: fixed test target CMake configuration (missing compile definitions
  carried over from the plugin target).

---

## [0.1.1] — 2026-05-26

### Fixed

- Linux CI: added `NEEDS_WEB_BROWSER TRUE` to `juce_add_plugin` in
  `src/CMakeLists.txt` so JUCE correctly links `webkit2gtk-4.1` on Linux.
  Without this flag the linker dropped the WebKitGTK symbol export and the
  WebView failed to initialise at runtime.

---

## [0.1.0] — 2026-05-26

Initial versioned release. This version ships the complete MVP2 feature set
designed and implemented against ADRs 0001–0027.

### Core engine

- **Three-mode single-engine instrument** ([ADR-0021]): one plugin instance
  runs exactly one of FM / SQ / D — selected via `mode_select` apvts param and
  persisted with the DAW project. Multiple Genesis timbres = multiple instances.
- **FM mode** — 16-voice `ymfm::ym2612` pool, single patch, hardware-authentic
  register sequencing. Per-operator controls (TL, MUL, DT, AR, DR, SR, RR, SL,
  KS, SSG-EG, AMON), per-channel controls (ALG, FB, AMS, PMS, L/R), global LFO.
  Polyphony 1–16 voices; LRU voice stealing; Mono (RETRIG/LEGATO) mode; glide time.
- **SQ mode** — libvgm `sn764xx` PSG, 3 tone + 1 noise channels. Per-channel
  software ADSR envelope, velocity→attenuation, pitch bend, soft pan, round-robin
  tone allocation with last-note priority on noise.
- **D mode** — PCM2612-style audio FX on the plugin's audio input bus:
  sample-rate decimation (`prescaler` 0–1) + 8-bit quantization + MONO collapse +
  DRY/WET blend. No voice model, no MIDI handling, no preset format.

### Signal processing

- **FM idle-silence clamp** — `renderFmBlock` skips ymfm rendering when no
  voice is keyed-on or in release tail, preventing Ladder quantizer
  amplification of ymfm's idle-state LSB residue.
- **Output Filter** (all modes) — Model-1 RC lowpass + amp coloration on the mix
  bus ([ADR-0024]).
- **Ladder Effect** (FM + D) — YM2612 stepwise nonlinearity ([ADR-0024]).
- FM native-rate mix bus resampled to host rate in a single pass via
  `juce::LagrangeInterpolator` ([ADR-0011]).
- SSG-EG loop shapes exposed as named labels (Repeat, Hold, Alternate, Inv.
  Repeat, etc.) per [ADR-0027].

### MIDI

- Note-on / note-off, velocity → TL scaling (configurable), pitch bend ±1–±12
  semitones, sustain pedal (CC 64), All Sound Off (CC 120), All Notes Off
  (CC 123), Aftertouch → LFO PMS.
- Full CC automation map (CCs 1–89) for all FM and SQ parameters.
- Program Change selects the Nth tagged preset of the current mode.

### Patch formats

- Load: TFI, VGI, DMP (v11, FM + PSG via [ADR-0026]), Y12, OPM, VGM bank
  extraction (FM patches from `.vgm`/`.vgz` streams).
- Save: VGI export.
- Drag-and-drop: single patch files and bulk folder import.
- Factory bank: Furnace `tfilib` library patches committed under `extern/patches/`
  and delivered as filesystem copies at install time ([ADR-0004], [ADR-0005]).

### Preset browser

- Folder-tree browser ([ADR-0006]) with unified `All / FM / SQ` filter chips
  ([ADR-0025]). Tagged presets (`.tfi`/`.vgi`/`.dmp` → FM; `.psg` → SQ) auto-switch
  mode on load. Custom roots registered by dropping a folder onto the browser.

### UI / UX

- Fixed 1200×560 WebView canvas ([ADR-0023]) with modern hardware-VST aesthetic
  ([ADR-0022]).
- Persistent header with mode selector, patch-name LCD, output-character toggles,
  mode-aware DAC PRESCALER knob, L/R output meters, master VOL, and TIPS toggle.
- FM panel (RYM2612-inspired): operator grid, ADSR curve previews, live algorithm
  diagram, FREQ CTRL MODE (INT_MUL / FLOAT_MUL / AUTO_RETRIG), POLY / NOTE MODE /
  RANGE steppers, GLOBAL IN block (PB + MW wheels), CH VOL master.
- SQ panel: per-channel ADSR + tuning + pan, GLOBAL IN block (PB wheel), noise
  channel selector.
- D panel: DRY/WET knob, MONO toggle, L/R full-fat level meters.
- Settings modal: HARDWARE STRICT toggle, tooltips, UI scale (1× / 2× / 3×).
- Hover tooltips on all interactive controls; toggled via `TIPS` header or Settings
  `TOOLTIPS` row.
- Scala `.scl` microtuning import (12-degree scales; shared FM + SQ table; path
  persisted in DAW project).

### State

- Full DAW state round-trip (`getStateInformation` / `setStateInformation`).
  All `apvts` parameters (mode + FM + SQ + D + globals) + active patch path +
  registered custom patch roots persisted in DAW project XML.

### Build / CI

- Cross-platform CI/CD: Windows VST3 + Standalone (MSVC), macOS VST3 + AU +
  Standalone (AppleClang, universal binary arm64+x86_64), Linux VST3 (GCC/Clang).
- GitHub Actions `release.yml`: build on every push; publish installers on `v*` tag.
- Windows: `.exe` installer (unsigned; includes WebView2 Evergreen Bootstrapper).
- macOS: `.pkg` installer, separate arm64 and x86_64 packages (unsigned).
- Linux: `.tar.gz` VST3 bundle; `libwebkit2gtk-4.1-0` is a runtime dependency.

[ADR-0004]: docs/design/adr/0004-furnace-only-factory-bank.md
[ADR-0005]: docs/design/adr/0005-filesystem-patch-delivery.md
[ADR-0006]: docs/design/adr/0006-folder-tree-patch-browser.md
[ADR-0011]: docs/design/adr/0011-resampling-strategy.md
[ADR-0021]: docs/design/adr/0021-three-mode-single-engine-ui.md
[ADR-0022]: docs/design/adr/0022-modern-vst-aesthetic.md
[ADR-0023]: docs/design/adr/0023-fixed-window-1200x560.md
[ADR-0024]: docs/design/adr/0024-hardware-filter-toggles.md
[ADR-0025]: docs/design/adr/0025-tagged-preset-browser.md
[ADR-0026]: docs/design/adr/0026-dmp-psg-import.md
[ADR-0027]: docs/design/adr/0027-ssg-eg-nudge-not-force.md
