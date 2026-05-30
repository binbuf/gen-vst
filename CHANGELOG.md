# Changelog

All notable changes to Gen VST are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.3.4] — 2026-05-30

### Added

- **Preset browser reveals the currently-loaded patch on open.** Reopening the
  preset popup used to reset the folder tree to its bare default (each root
  expanded one level, nothing selected), losing the relative path of wherever
  the loaded patch lived. The browser now asks the processor for the active
  patch path (new `getActivePatchPath` native function, per current mode),
  expands the folder chain down to it, selects the containing folder, highlights
  the patch row, and scrolls both into view — without re-loading the patch.
  Works for Factory, Saved, Imported, and custom roots alike. The reveal runs
  only on open, so Add Folder / Import / Delete no longer snap the selection
  back.

### Fixed

- **`build.sh --release` failing on macOS.** Two independent breakages on the
  current toolchain (Apple Silicon, Xcode 26.x):
  - The `GenVstTests` binary shipped unsigned, and macOS SIGKILLs an unsigned
    binary at `exec`, so CMake's post-build `gtest_discover_tests` step (which
    runs the binary to enumerate tests) was killed and failed the build. The
    test target now gets an ad-hoc `codesign` post-build step, ordered before
    discovery.
  - The universal CLAP failed to link its x86_64 slice because
    `libclap_juce_extensions.a` built arm64-only: `CMAKE_OSX_ARCHITECTURES` was
    set *after* `FetchContent_MakeAvailable(clap-juce-extensions)`, and a target
    captures the architecture list at creation time. The arch/deployment
    defaults are now set before that fetch. (CI was unaffected — it builds
    single-arch per architecture.)

## [0.3.3] — 2026-05-29

### Fixed

- **Patch / parameter state not restored on project reload.** When a project
  was saved and reopened (most visibly in Ableton Live), an instance could come
  back on the factory-default patch instead of the configured one. The cold-start
  default-patch load raced the host's state restore: in the
  `prepareToPlay`-before-`setStateInformation` instantiation order that Live
  uses, the queued default-load `AsyncUpdater` fired *after* the saved `apvts`
  was restored and overwrote it with the factory default. `setStateInformation`
  now treats a restore as authoritative — it cancels any pending default-load,
  re-anchors the mode tracker, and drains the pending restore immediately when
  the patch browser is already live (so custom roots and per-mode patch labels
  also survive even if the host never calls `prepareToPlay` again). The
  mode-switch handler additionally bails while a restore is in flight. Added a
  headless integration test covering both host instantiation orderings plus a
  second `prepareToPlay`.

### Changed

- **Gallery widget-development scratch parameters are no longer exposed in
  release builds.** The `gallery_*` apvts parameters (bound only by the
  dev-only `gallery.html`) were appearing as `"GALLERY …"` entries in the host's
  automatable-parameter list. They are now gated behind `GENVST_DEV_SERVER`.

## [0.3.2] — 2026-05-28

### Fixed

- **Modal centering under Ableton Auto-Scale** (`modal-host.js`,
  `preset-browser.js`). Follow-up to the 0.2.2 chassis-pinning work. The modal
  host still used `inset: 0`, so on hosts that enlarge the WebView beyond the
  1200×660 chassis (Ableton Auto-Scale, etc.) modals centered on the full
  WebView viewport instead of the chassis region — they drifted right/down off
  the visible UI. The host is now pinned to the top-left 1200×660 region
  (collapsing to viewport via `max-width`/`max-height` in no-keyboard mode),
  and the preset browser switches its height from `95vh` (WebView viewport) to
  `95%` (chassis-sized parent) so the panel scales with the chassis rather
  than the host-stretched canvas.

## [0.3.1] — 2026-05-28

### Fixed

- **macOS CI cross-arch link failure.** The root `CMakeLists.txt` forced
  `CMAKE_OSX_ARCHITECTURES` to `"arm64;x86_64"` with a plain `set()`, which
  shadowed the per-job `-D` flag the release workflow passes. Both macOS jobs
  ended up trying a universal build and failed linking the cross arch (first
  surfaced at the `GenVst_CLAP` Ld step). The override is now guarded so CI's
  `-D` wins and local dev still defaults to universal.

## [0.3.0] — 2026-05-28

### Added

- **CLAP plug-in format.** Gen VST now builds and ships a CLAP alongside VST3 /
  AU / Standalone, via `clap-juce-extensions` ([ADR-0028]). It reuses the same
  `AudioProcessor`, parameters, state, and WebView editor. Install locations:
  `C:\Program Files\Common Files\CLAP` (Windows), `/Library/Audio/Plug-Ins/CLAP`
  (macOS), and `~/.clap` (Linux, via the tarball `install.sh`). `CLAP_ID` is
  `com.genvst.genvst`; `CLAP_FEATURES` is `instrument synthesizer stereo`.
  CLAP-native per-note expression / polyphonic modulation are not wired up yet —
  this build mirrors the VST3.
- **CLAP in every installer.** The Windows `.exe`, the macOS `.pkg` (a fourth
  `com.genvst.clap` component → `/Library/Audio/Plug-Ins/CLAP`), and the Linux
  `.tar.gz` now carry the CLAP.
- **`build.ps1` / `build.sh` deploy the CLAP.** The dev build/deploy scripts copy
  `Gen VST.clap` into the per-user (or `-System` / `--system`) CLAP folder, with
  matching `-Uninstall` / `--uninstall` cleanup.
- **CI `clap-validator` soft gate.** Each platform job validates the built
  `.clap` with `clap-validator` (pluginval cannot validate CLAP).

### Changed

- **`clap-juce-extensions` pinned to a JUCE-8-compatible commit.** No upstream
  release tag supports JUCE 8's `getPosition()` `AudioPlayHead` API, so the build
  pins `main` commit `e8de9e8`, which also carries the Windows embedded-WebView
  keyboard-input fix the editor needs.
- **Factory-patch resolution falls back to the user data dir.**
  `resolveFactoryRoot` now falls back to `GENVST_STANDALONE_PATCH_DIR` for plug-in
  formats when the bundle walk fails, so the single-file Windows/Linux `.clap`
  (which has no `Resources/` dir) finds the factory bank the installers already
  drop there. Bundle formats (VST3, AU, the macOS `.clap`) are unaffected — they
  satisfy the walk first.

### Fixed

- **`PluginEditor.cpp` Win32 header.** The high-DPI helpers now include
  `<windows.h>` explicitly instead of relying on it arriving transitively via the
  WebView2 SDK headers — linking `clap-juce-extensions` perturbed that chain. The
  explicit include hardens every format's build, not just CLAP.

## [0.2.2] — 2026-05-28

### Added

- **Sustain pedal (CC 64) for FM mode** — a new `sustainPedalDown` atomic
  tracks pedal state; `handleNoteOff` holds released voices while the pedal is
  down and releases them on pedal-up. SQ mode has no sustain hook (documented).
- **PSG noise note-range split** — a new `noise_split_note` apvts param
  (default MIDI 47 = B2). Notes at or below the split route to the SN76489
  noise channel; notes above route to a tone channel. The SQ panel gains a
  `SPLIT` stepper.
- **Program Change support** — `dispatchMidi` now handles Program Change by
  posting to the message thread; `loadProgramChangePatch` walks all patch roots
  and loads the Nth tagged patch of the current mode. D mode ignores PC.
- **CC 121 (Reset All Controllers)** — zeroes the mod-wheel / pitch-bend
  mirrors, channel pressure, and sustain pedal, releases sustained voices, and
  zeroes bend on active voices. apvts patch params are left untouched.
- **`build.ps1` / `build.sh` uninstall** — `-Uninstall` (`--uninstall`) removes
  a dev deploy (per-user plugin + patches); composes with `-System` /
  `--system` and `-Clean` / `--clean`. Useful before running the real installer.
- **`-DGENVST_DIAG` build option** — opt-in instrumentation that writes Win32 /
  JUCE / WebView measurements to `~/Documents/GenVst-diag.log` for host-DPI
  debugging. Compiled out completely by default.

### Fixed

- **Ableton / Windows high-DPI whitespace** (`PluginEditor.cpp`,
  `design-system.css`). When Ableton's DPI-unaware VST3 message thread queried
  the editor HWND on a >100% display, Windows returned virtualized (down-scaled)
  client coordinates, so the WebView was laid out smaller than the host canvas
  and a band of empty chassis showed to the right and bottom. The editor now
  temporarily switches the thread to PerMonitorV2
  (`SetThreadDpiAwarenessContext`) to read true physical pixels and resizes the
  WebView to match (`syncToHostSize`, called from `resized`,
  `parentSizeChanged`, and the telemetry timer as a fallback). The CSS side
  drops the hard-coded `width: 1200px` / `viewport width=1200` in favour of
  `100vw` / `100vh` with the chassis clamped to its native 1200×660 via
  `max-width` / `max-height`, anchored top-left so `chassis-bg-mid` fills any
  surplus. Tooltip edge-clamping now reads `window.innerWidth/innerHeight`
  instead of the hard-coded 1200×560.
- **Ladder Effect recalibrated to ymfm's `dac_discontinuity`** — the FM-mode
  ladder is now applied per-voice inside ymfm: each `Voice` holds a `ym3438`
  (clean ASIC, no discontinuity) and `renderAdd` dispatches to
  `ym2612::generate` (toggle on, +4/−3 per-channel DAC bias) vs
  `ym3438::generate` (toggle off), matching the real hardware's per-channel DAC
  + analog summing. D mode's `LadderEffect` lookup table was rebuilt to mirror
  the same curve exactly — `(code − 3)/256` for negative codes, `(code + 4)/256`
  for non-negative — giving the documented 8× zero-crossing gap. The previous
  approximation applied the ladder to the post-sum signal with a hand-fitted
  ~8.2× gap.
- **CC 7 / CC 10 documentation drift** — the MIDI map now correctly marks CC 10
  (pan) as a no-op in v2.

### Changed

- **Output Filter framing** — documentation no longer describes the hand-tuned
  coefficients as a "calibration follow-up"; they are the intentional v2
  Model-1 approximation. No DSP change.
- **Build scripts overhauled** (`build.ps1`, `build.sh`, README) — PowerShell
  switches moved from `--flag` to idiomatic `-Flag` form, expanded help text,
  and the new uninstall paths documented.

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
[ADR-0028]: docs/design/adr/0028-clap-format-wired-up.md
