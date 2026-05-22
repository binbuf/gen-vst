# Task 01 — Repo skeleton & buildable empty plugin

> **Milestone:** E2E #1 — the plugin builds and loads.
> **Depends on:** nothing.
> **Design references:** `docs/design/06-build-system.md` (primary),
> `docs/design/01-architecture.md`, ADR-0001, ADR-0002, ADR-0003, ADR-0005,
> ADR-0007, ADR-0009.

## Objective

Stand the repository up so that one `cmake --build` produces a **VST3** and a
**Standalone** artifact that load in a DAW and pass `pluginval`. There is no
audio and no real UI yet. This task exists solely to retire the
build/packaging integration risk before any feature work.

## Context & key constraints

- The repo currently contains only `LICENSE` (GPL v3), `docs/`, and `extern/`
  (39 committed factory `.tfi` files + two fonts). There is **no**
  `CMakeLists.txt`, `src/`, `ui/`, or `third_party/`.
- JUCE **8.0.4** via `FetchContent`, pinned by git tag.
- C++20, `CMAKE_CXX_EXTENSIONS OFF`.
- The plugin is an instrument: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`.
- Window size is fixed **960×560** (ADR-0007).
- The editor in this task is a **native placeholder** `juce::Component`. The
  WebView editor is Task 03 — do not build the `ui/` project yet.
- `third_party/ymfm` and `third_party/libvgm` submodules are **added now** (the
  root CMake checks both exist), but their sources are compiled into the plugin
  in Task 02 (ymfm) and Task 07 (libvgm) — not here.
- Wire **factory-patch delivery** now (ADR-0005) so every later task can load
  patches at runtime. The factory bank is the **top-level** `extern/patches/*.tfi`
  files only — a non-recursive glob (ADR-0004); the gitignored
  `extra/` dev test set must never enter a build artifact.

## Scope

- `.gitmodules` + the two submodules.
- Root `CMakeLists.txt` — exactly the structure in `06-build-system.md`:
  project/languages, C++20, the three `option(...)` switches, JUCE
  `FetchContent` (tag `8.0.4`), submodule existence `FATAL_ERROR` checks,
  the `APPLE` arch/deployment-target block, `add_subdirectory(src)`, tests.
- `CMakePresets.json` — the four presets in `06-build-system.md`.
- `src/CMakeLists.txt` — `juce_add_plugin` with the company/codes/flags from
  `06-build-system.md` (`FORMATS VST3 AU Standalone`, `IS_SYNTH`,
  `NEEDS_MIDI_INPUT`, `EDITOR_WANTS_KEYBOARD_FOCUS TRUE`, `NEEDS_WEBVIEW2 TRUE`,
  AU type/codes, etc.); the `GENVST_STANDALONE_PATCH_DIR` per-platform block;
  the factory-patch staging + `juce_add_bundle_resources_directory` + standalone
  `install()` rule; `target_sources` for the minimal source set;
  `target_compile_definitions`; `target_link_libraries`.
- `src/PluginProcessor.h/.cpp` — minimal `juce::AudioProcessor`: constructor,
  `processBlock` that clears the buffer, stub `getStateInformation` /
  `setStateInformation`, `createEditor`, the boilerplate overrides.
- `src/PluginEditor.h/.cpp` — a native `juce::Component`, fixed 960×560, that
  paints the chassis-black background and "GEN VST" centered.
- `tests/CMakeLists.txt` — per `06-build-system.md`, plus one trivial passing
  test file so `ctest` is wired end to end.
- Root `.gitignore` — `build*/`, `ui/node_modules/`, `ui/dist/`, IDE dirs.

## Out of scope

- Any audio or ymfm code → Task 02. libvgm/PSG → Task 07.
- The `ui/` Vite project, the WebView, `juce_add_binary_data` for the web
  bundle, `GENVST_DEV_SERVER` → Task 03.
- `apvts` / parameters → Task 02 adds the first parameter.
- Real unit tests → Task 04.

## Implementation steps

1. Add the submodules:
   `git submodule add https://github.com/aaronsgiles/ymfm.git third_party/ymfm`
   and `git submodule add https://github.com/ValleyBell/libvgm.git third_party/libvgm`,
   then `git submodule update --init --recursive`.
2. Write the root `CMakeLists.txt` from `06-build-system.md`. The design shows
   `add_subdirectory(third_party)`; ymfm/libvgm are compiled **inline** into the
   plugin target (ADR-0002), so either omit that line or create an empty
   `third_party/CMakeLists.txt` — do not build them as separate libraries.
3. Write `CMakePresets.json` (four presets).
4. Write `src/CMakeLists.txt`. For this task only, **omit** the `GenVstWebData`
   target and the `GenVstWebBundle` dependency (Task 03 adds them) and **omit**
   the ymfm and libvgm `target_sources`/`include_directories` (Tasks 02/07 add
   them). Keep the factory-patch staging block — it is needed now. Keep the JUCE
   link list; `GenVstWebData` is the only entry to drop until Task 03.
5. Write the minimal `PluginProcessor` and `PluginEditor`.
6. Write `tests/CMakeLists.txt` and a `SmokeTest.cpp` with one `EXPECT_TRUE(true)`
   test so the GoogleTest + `ctest` wiring is proven.
7. Write `.gitignore`.

## Deliverables

`.gitmodules`, `third_party/ymfm`, `third_party/libvgm`, `CMakeLists.txt`,
`CMakePresets.json`, `.gitignore`, `src/CMakeLists.txt`,
`src/PluginProcessor.{h,cpp}`, `src/PluginEditor.{h,cpp}`,
`tests/CMakeLists.txt`, `tests/SmokeTest.cpp`.

## Verification

1. `cmake --preset windows-debug` — configures with no `FATAL_ERROR` (submodule
   checks pass; JUCE downloads).
2. `cmake --build build/windows-debug` — builds clean.
3. Artifacts exist under `build/windows-debug/.../GenVst_artefacts/`: a
   `VST3/Gen VST.vst3` and a `Standalone/Gen VST.exe`.
4. Launch the Standalone — a fixed 960×560 window opens reading "GEN VST"; no
   crash; closing it exits cleanly.
5. `pluginval --strictness-level 8 --validate "<path>/Gen VST.vst3"` — **passes**.
6. Load the VST3 in Reaper (or any VST3 host) — it instantiates as an
   instrument, the placeholder editor opens, no crash on add/remove.
7. `ctest --test-dir build/windows-debug --output-on-failure` — the smoke test
   passes.
8. Inspect `build/windows-debug/factory-patches/` — it contains exactly the 39
   top-level `.tfi` files and **no** `extra/` subfolder.

## Done when

- [ ] Both submodules added and initialized.
- [ ] Configure + build are clean on `windows-debug`.
- [ ] VST3 + Standalone artifacts are produced.
- [ ] `pluginval` passes at strictness level 8.
- [ ] The VST3 loads in a DAW without crashing.
- [ ] `ctest` runs and the smoke test is green.
- [ ] Factory patches are staged correctly (39 files, non-recursive — no `extra/`).
