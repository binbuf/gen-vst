# Build System

## Repository Layout

```
gen-vst/
├── CMakeLists.txt           ← root: project, FetchContent JUCE, add_subdirectory
├── CMakePresets.json        ← developer presets (Debug/Release per platform)
├── CHANGELOG.md             ← version history
├── LICENSE                  ← GPL v3 (ADR-0003)
├── update-version.sh        ← release helper: rewrites all pinned version strings
├── .gitmodules              ← ymfm + libvgm submodules
├── .github/workflows/
│   └── release.yml          ← CI/CD: build + auto-publish on v* tag push
├── packaging/
│   └── macos/build-pkg.sh   ← .pkg assembly script (pkgbuild + productbuild)
├── third_party/             ← code submodules
│   ├── ymfm/                ← git submodule: aaronsgiles/ymfm
│   └── libvgm/              ← git submodule: ValleyBell/libvgm (SN76489 core only — ADR-0009)
├── src/
│   ├── CMakeLists.txt       ← juce_add_plugin, sources, link libraries
│   ├── PluginProcessor.h/cpp
│   ├── PluginEditor.h/cpp   ← hosts juce::WebBrowserComponent + keyboard strip resize logic
│   ├── VoiceAllocator.h/cpp
│   ├── Voice.h/cpp          ← single YM2612 voice (auto-idle silence detection)
│   ├── SN76489Engine.h/cpp
│   ├── DspDecimator.h/cpp
│   ├── LadderEffect.h/cpp
│   ├── OutputFilter.h/cpp
│   ├── PatchSystem.h/cpp
│   ├── Telemetry.h          ← C++ → JS telemetry push (VU, noteOn, activeNotes bitmask)
│   └── PluginState.h        ← DAW state XML helpers
├── ui/                      ← Vite web UI project (HTML/CSS/JS + Canvas — ADR-0001)
│   ├── package.json
│   ├── package-lock.json
│   ├── vite.config.js
│   ├── index.html
│   └── src/
│       └── widgets/
│           └── keyboard.js  ← on-screen piano roll keyboard strip
├── extern/                  ← data assets (not code)
│   ├── fonts/               ← bitmap/segment fonts, consumed by the ui/ build
│   │   ├── press-start-2p/
│   │   └── 7-segment/
│   └── patches/
│       ├── *.tfi            ← factory bank (committed, shipped — top level only)
│       └── extra/           ← game-derived test set (gitignored, dev-only)
├── tests/
│   ├── CMakeLists.txt
│   └── *.cpp
└── docs/
    └── design/
```

`third_party/` holds code submodules; `extern/` holds data assets (fonts,
patches). There is no native `LookAndFeel` source and no `resources/` directory —
the UI is the web app under `ui/` ([ADR-0001](adr/0001-juce8-webview-ui.md)).

---

## Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(GenVst VERSION 0.2.0 LANGUAGES C CXX)   # C: libvgm sn764xx.c

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit tests" ON)
option(COPY_PLUGIN_AFTER_BUILD "Copy plugin to system install directory" OFF)
option(GENVST_DEV_SERVER "Load the UI from the Vite dev server, not the embedded bundle" OFF)

# JUCE via FetchContent — pin to known-good tag
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# Submodules — require them to be initialized
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src/ymfm.h")
    message(FATAL_ERROR "ymfm submodule not found. Run:\n  git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvgm/emu/cores/sn764xx.c")
    message(FATAL_ERROR "libvgm submodule not found. Run:\n  git submodule update --init --recursive")
endif()

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")   # ADR-0015 — verify vs JUCE 8 WebView
endif()

add_subdirectory(third_party)
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

C++20 is used (JUCE 8 supports it). The patch loader uses `std::optional<Patch>` +
a separate error string rather than C++23's `std::expected` — see
[04-patch-system.md](04-patch-system.md).

---

## Web UI Build

The plugin UI is an HTML/CSS/JS app rendered in a WebView
([ADR-0001](adr/0001-juce8-webview-ui.md), [05-ui-ux.md](05-ui-ux.md)). The `ui/`
directory is a **Vite** project — vanilla JS + Canvas, no framework. The package
manager is **npm**, with `ui/package-lock.json` committed.

CMake drives the web build so a single `cmake --build` produces everything:

```cmake
set(UI_DIR  "${CMAKE_SOURCE_DIR}/ui")
set(UI_DIST "${UI_DIR}/dist")
set(UI_ZIP  "${CMAKE_BINARY_DIR}/genvst-ui.zip")

# Resolve npm explicitly — on Windows the launcher is npm.cmd, which a bare
# `npm` in a custom command may not find.
find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)

# Rebuild the bundle when any tracked UI source changes.
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    "${UI_DIR}/src/*" "${UI_DIR}/index.html"
    "${UI_DIR}/package.json" "${UI_DIR}/vite.config.js")

add_custom_command(
    OUTPUT  "${UI_ZIP}"
    COMMAND "${NPM_EXECUTABLE}" --prefix "${UI_DIR}" ci
    COMMAND "${NPM_EXECUTABLE}" --prefix "${UI_DIR}" run build          # vite build -> ui/dist/
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${UI_ZIP}" --format=zip .
    WORKING_DIRECTORY "${UI_DIST}"
    DEPENDS ${UI_SOURCES}
    VERBATIM)

add_custom_target(GenVstWebBundle DEPENDS "${UI_ZIP}")
```

`genvst-ui.zip` is embedded via `juce_add_binary_data` (see below); the editor's
WebView resource provider serves files out of the zip in release builds
([05-ui-ux.md](05-ui-ux.md)). The Vite project pulls the bitmap/segment fonts from
`extern/fonts/` into `ui/dist/`, so they ride **inside the same zip** — there is
no separate font binary-data target.

**Development:** configure with `-DGENVST_DEV_SERVER=ON`. The editor then loads
`http://localhost:5173` (the Vite dev server, `npm run dev` in `ui/`) for hot
reload, and the embedded bundle is not used. This is passed to C++ as the
`GENVST_DEV_SERVER` compile definition.

The exact `juce_add_binary_data` + resource-provider wiring follows the JUCE 8
`WebViewPluginDemo` example — that demo is the reference for relays, native
functions and the resource provider.

---

## src/CMakeLists.txt — juce_add_plugin

```cmake
# Embed the zipped Vite web bundle built by the GenVstWebBundle target.
juce_add_binary_data(GenVstWebData SOURCES "${UI_ZIP}")
add_dependencies(GenVstWebData GenVstWebBundle)

juce_add_plugin(GenVst
    COMPANY_NAME                "Gen VST"
    COMPANY_WEBSITE             "https://github.com/binbuf/gen-vst"
    PLUGIN_MANUFACTURER_CODE    GnVs     # AU: first char uppercase, rest lowercase
    PLUGIN_CODE                 Genv     # 4 chars unique per plugin
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Gen VST"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE      # HTML UI has text/numeric inputs (ADR-0001)
    NEEDS_WEBVIEW2              TRUE      # Windows WebView2 backend (ADR-0001)
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE             TRUE
    COPY_PLUGIN_AFTER_BUILD     ${COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES             "Instrument|Synth"
    DESCRIPTION                 "Sega Genesis YM2612+SN76489 emulation"
    BUNDLE_ID                   "com.genvst.genvst"
    VERSION                     "0.2.0"
)

# --- Standalone patch directory (runtime data dir, per platform) --------------
if(WIN32)
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{LOCALAPPDATA}/GenVst/patches")
elseif(APPLE)
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{HOME}/Library/Application Support/GenVst/patches")
else()
    set(GENVST_STANDALONE_PATCH_DIR "$ENV{HOME}/.local/share/GenVst/patches")
endif()

# --- Patch delivery -----------------------------------------------------------
# Factory patches ship as loose .tfi files, not embedded binary data (ADR-0005).
# Enumerate ONLY the top-level .tfi in extern/patches/ — a recursive glob would
# pull in the gitignored extra/ developer test set (ADR-0004).
file(GLOB FACTORY_PATCHES "${CMAKE_SOURCE_DIR}/extern/patches/*.tfi")

# Stage into a clean dir, then copy that tree into each plugin bundle's Resources.
set(FACTORY_STAGE "${CMAKE_BINARY_DIR}/factory-patches")
file(MAKE_DIRECTORY "${FACTORY_STAGE}")
file(COPY ${FACTORY_PATCHES} DESTINATION "${FACTORY_STAGE}")
juce_add_bundle_resources_directory(GenVst "${FACTORY_STAGE}")

# Standalone has no bundle — install the factory patches to the data directory.
install(FILES ${FACTORY_PATCHES} DESTINATION "${GENVST_STANDALONE_PATCH_DIR}")
# ------------------------------------------------------------------------------

target_sources(GenVst PRIVATE
    PluginProcessor.cpp  PluginProcessor.h
    PluginEditor.cpp     PluginEditor.h
    VoiceAllocator.cpp   VoiceAllocator.h
    Voice.cpp            Voice.h
    SN76489Engine.cpp    SN76489Engine.h
    DspDecimator.cpp     DspDecimator.h
    LadderEffect.cpp     LadderEffect.h
    OutputFilter.cpp     OutputFilter.h
    PatchSystem.cpp      PatchSystem.h
    Telemetry.h
    PluginState.h
)

# ymfm sources — inline into the plugin target, NOT a separate static lib
# (avoids LTO boundary issues and simplifies the build graph).
target_sources(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp"
)
target_include_directories(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src")

# libvgm — compile ONLY the SN76489 core, not the whole library (ADR-0009).
# The exact source/header subset is pinned when the submodule is added.
target_sources(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/libvgm/emu/cores/sn764xx.c")
target_include_directories(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/libvgm")

target_compile_definitions(GenVst PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<BOOL:${GENVST_DEV_SERVER}>:GENVST_DEV_SERVER=1>
)

target_link_libraries(GenVst PRIVATE
    GenVstWebData
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra          # juce::WebBrowserComponent lives here
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
```

**AU `PLUGIN_MANUFACTURER_CODE` note:** GarageBand 10.3+ requires exactly one
uppercase letter followed by three lowercase letters (e.g. `GnVs`). Failing this
causes the AU to fail validation in Logic Pro.

---

## Platform Targets

### Windows

- Formats: VST3 + CLAP (AU is macOS-only).
- Compiler: MSVC 2019+ (Clang-cl also works); `/W3`.
- **WebView2:** `NEEDS_WEBVIEW2 TRUE` makes JUCE pull the WebView2 SDK. The
  Evergreen WebView2 runtime ships with Windows 11 and recent Windows 10;
  behaviour on machines lacking it is an open question in
  [05-ui-ux.md](05-ui-ux.md).
- Node.js (for the `ui/` build) must be on `PATH`.
- Install path (system): `%CommonProgramFiles%\VST3\`; (user): `%LOCALAPPDATA%\Programs\Common\VST3\`.
- CLAP install (system): `%CommonProgramFiles%\CLAP\`.
- During development set `COPY_PLUGIN_AFTER_BUILD OFF` to avoid UAC prompts.

### macOS

- Formats: VST3 + AU + CLAP.
- Compiler: AppleClang 14+ or Clang 15+.
- Min deployment target: macOS 10.15 (Catalina) — pending the JUCE 8 WebView
  verification in [ADR-0015](adr/0015-webview-backend-support.md); raise it if a
  WebView native-integration feature proves unavailable at 10.15.
- WebView backend: `WKWebView` — no extra dependency.
- Universal binary set in the root CMakeLists.txt (`arm64;x86_64`).
- VST3 install: `~/Library/Audio/Plug-Ins/VST3/`; AU: `~/Library/Audio/Plug-Ins/Components/`; CLAP: `~/Library/Audio/Plug-Ins/CLAP/`.
- AU validation: `auval -v aumu Genv GnVs` (subtype `Genv`, manufacturer `GnVs`).
- CLAP validation: `clap-validator validate "Gen VST.clap"` (soft gate).

### Linux

- Formats: VST3 + CLAP.
- Compiler: GCC 11+ or Clang 14+; `-fPIC`.
- System packages: `libasound2-dev`, `libx11-dev`, `libxcursor-dev`,
  `libxrandr-dev`, `libxinerama-dev`, `libfreetype6-dev`, `libgl-dev`, and
  **`libwebkit2gtk-4.1-dev`** (the JUCE WebView backend on Linux).
- WebView backend: WebKitGTK on the `webkit2gtk-4.1` API line. The minimum
  runtime version is pinned against the oldest targeted distro (Ubuntu 22.04 LTS
  / Debian 12) — see [ADR-0015](adr/0015-webview-backend-support.md).
- Display server: X11 is the baseline; Wayland runs via XWayland — best-effort,
  not separately QA'd ([ADR-0015](adr/0015-webview-backend-support.md)).
- Install path (user): `~/.vst3/`; (system): `/usr/lib/vst3/`. CLAP (user): `~/.clap/`; (system): `/usr/lib/clap/`.

---

## Distribution & Installers

Build artifacts are raw plugin bundles (VST3/AU) and the Standalone. How those
reach end users — and how the Windows WebView2 runtime is guaranteed present — is
set by [ADR-0016](adr/0016-webview2-runtime-distribution.md).

### Windows — installer

Windows ships an **installer**, not loose bundles. The installer:

- places the VST3 (and the Standalone) in their install locations;
- bundles the **WebView2 Evergreen Bootstrapper** (`MicrosoftEdgeWebView2Setup.exe`,
  ~2 MB) and runs it silently — it installs the runtime only if absent, so a fresh
  install always has a working WebView ([ADR-0016](adr/0016-webview2-runtime-distribution.md)).

The installer tool (WiX, Inno Setup, …) is an implementation choice. For offline
installers the WebView2 **Fixed Version** runtime (~150 MB+) is the documented
alternative to the bootstrapper.

### macOS — `.pkg` installers

WKWebView is part of macOS, so there is no runtime to install. macOS ships as
two separate **unsigned `.pkg` installers** — one for Apple Silicon (arm64) and
one for Apple Intel (x86_64). Each installs the VST3 bundle, the AU component,
and the Standalone. Factory patches are embedded in each bundle's `Resources/`
directory via `juce_add_bundle_resources_directory`.

**Gatekeeper on macOS 15 (Tahoe)+:** since the packages are unsigned, users
must approve them via System Settings → Privacy & Security → Open Anyway (the
right-click → Open shortcut no longer suffices on Tahoe). Documented in
the README.

Code signing and notarization requires an Apple Developer certificate in GitHub
Secrets; defer to a dedicated signing task before a commercial release.

### Linux — bundles (MVP)

Linux ships as raw VST3 bundles for the MVP. WebKitGTK is a system library and is
**not** bundled — a future `.deb`/AppImage declares the `libwebkit2gtk-4.1-0`
runtime dependency.

### Runtime fallback

If the WebView still fails to initialise, the editor shows a native fallback
panel rather than a blank window — specified in
[08-ui-views.md](08-ui-views.md) (view 9).

---

## CMakePresets.json

```json
{
  "version": 3,
  "configurePresets": [
    { "name": "windows-debug",   "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "windows-release", "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "macos-release",   "generator": "Xcode",
      "binaryDir": "${sourceDir}/build/macos-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "linux-release",   "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ]
}
```

---

## GitHub Actions CI / Release

The workflow lives in `.github/workflows/release.yml`. Every job checks out
submodules **and** installs Node.js, because the CMake build drives the `ui/`
web build. On every push it runs all three platform builds and uploads
workflow artifacts. On a `v*` tag push it additionally creates a **GitHub
Release** and attaches the installer packages automatically.

```yaml
on:
  push:
    branches: [main]
    tags: ['v*']   # triggers the release-publish step

jobs:
  build-windows:
    runs-on: windows-latest
    # Builds VST3 + Standalone; packages as a .exe installer.

  build-macos-arm64:
    runs-on: macos-latest         # Apple Silicon runner
    # Builds VST3 + AU + Standalone; packages as arm64 .pkg via packaging/macos/build-pkg.sh.

  build-macos-x86_64:
    runs-on: macos-15-intel       # Intel runner (migrated from macos-13)
    # Builds VST3 + AU + Standalone; packages as x86_64 .pkg.

  build-linux:
    runs-on: ubuntu-latest
    # Installs libwebkit2gtk-4.1-dev + audio/graphics libs.
    # Adds JUCE_USE_CURL=0 to both the plugin and test targets.
    # Links webkit2gtk via NEEDS_WEB_BROWSER in src/CMakeLists.txt.
    # Packages VST3 as .tar.gz.
```

**macOS packaging note.** The two macOS jobs produce separate installers —
`GenVst-arm64.pkg` and `GenVst-x86_64.pkg` — rather than a single universal
binary package. The `build-pkg.sh` script under `packaging/macos/` drives the
`.pkg` creation with `pkgbuild` + `productbuild`; it copies the factory patches
into the bundle's `Resources/` directory as part of the package assembly.

**Factory patches in macOS bundles.** `src/CMakeLists.txt` uses
`juce_add_bundle_resources_directory` to embed the `factory-patches` staging
directory into the VST3/AU `.component` bundles. At runtime the Standalone walks
the bundle to locate the patches (via the macOS `CFBundleGetMainBundle`
path-resolution path). The Windows and Linux builds still use the
`install(FILES …)` path to drop patches into the app-data directory.

Release artifacts (VST3 bundles, installers) upload on tag push; macOS code
signing requires an Apple Developer certificate in GitHub Secrets — defer until
a dedicated signing task.

---

## Tests (tests/CMakeLists.txt)

```cmake
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(googletest)

add_executable(GenVstTests
    PatchLoaderTests.cpp
    VoiceAllocatorTests.cpp
    FrequencyCalcTests.cpp
    RegisterWriteTests.cpp
)

target_sources(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp"
)
target_include_directories(GenVstTests PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src"
    "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(GenVstTests PRIVATE
    GTest::gtest_main
    juce::juce_core
    juce::juce_audio_basics)

include(GoogleTest)
gtest_discover_tests(GenVstTests)
```

**Test categories:**

| File | Tests |
|------|-------|
| `PatchLoaderTests.cpp` | TFI/VGI/DMP parse, round-trip, malformed-input rejection |
| `VoiceAllocatorTests.cpp` | Pool note-on/off, LRU stealing, part↔channel routing, sustain hold, all-notes-off |
| `FrequencyCalcTests.cpp` | MIDI note → F-number + BLK, pitch bend recalc, octave boundary |
| `RegisterWriteTests.cpp` | Correct ymfm register sequence for a known patch (vs an expected register log) |

The unit tests cover audio/patch logic only; they do not exercise the WebView UI.

---

## Submodule Setup

```bash
# Add submodules (run once):
git submodule add https://github.com/aaronsgiles/ymfm.git third_party/ymfm
git submodule add https://github.com/ValleyBell/libvgm.git third_party/libvgm

# Initialize on a fresh clone:
git submodule update --init --recursive
```

---

## Version bumping (`update-version.sh`)

`update-version.sh` is a release helper at the repo root. It accepts a single
version argument and rewrites every pinned version string in one pass:

```bash
./update-version.sh 0.3.0
```

Files it touches:
- `CMakeLists.txt` — `project(GenVst VERSION …)`
- `src/CMakeLists.txt` — `VERSION "…"` inside `juce_add_plugin`
- `ui/package.json` + `ui/package-lock.json` — `"version": "…"`
- Vite define for `__APP_VERSION__` in `vite.config.js` (if present)
- About modal HTML fallback version string
- `release.yml` dev-build fallback version

After running the script, review the diff, commit it with a message like
`chore: bump version to vX.Y.Z`, then push and tag:

```bash
git tag vX.Y.Z && git push origin vX.Y.Z
```

The `release.yml` `on: push: tags: ['v*']` trigger picks up the tag and
publishes the release automatically.

---

## CLAP

CLAP is a shipped build target via `clap-juce-extensions`
([ADR-0028](adr/0028-clap-format-wired-up.md), superseding
[ADR-0008](adr/0008-clap-post-mvp.md)). `clap-juce-extensions` is pulled in with
`FetchContent`, pinned to a `main` commit (`e8de9e8`) rather than a release tag —
the latest tag (`0.26.0`) predates JUCE 8's `getPosition()` `AudioPlayHead` API
and won't compile, while the pinned commit adds JUCE 8 support plus the Windows
embedded-WebView keyboard fix (#175). It is fetched **recursively** (it carries
the CLAP SDK + `clap-helpers` as nested submodules) and **not** shallow — a
shallow parent clone can't resolve the submodules' pinned commits. A single
`clap_juce_extensions_plugin(TARGET GenVst CLAP_ID "com.genvst.genvst"
CLAP_FEATURES instrument synthesizer stereo)` call produces the `GenVst_CLAP`
target; the WebView editor works unchanged.

Per-OS layout and install location:

| OS | Artifact | Installs to |
|----|----------|-------------|
| Windows | single `Gen VST.clap` file | `C:\Program Files\Common Files\CLAP` |
| macOS | `Gen VST.clap` bundle | `/Library/Audio/Plug-Ins/CLAP` |
| Linux | single `Gen VST.clap` file | `~/.clap` (tarball `install.sh`) |

The macOS `.clap` is a bundle and carries factory patches in
`Contents/Resources/patches/` like the other formats. The Windows/Linux `.clap`
is a single file, so `resolveFactoryRoot` falls back to
`GENVST_STANDALONE_PATCH_DIR` (the user-data dir the installers populate) for it.
CI validates the CLAP with `clap-validator` as a soft gate (pluginval cannot
validate CLAP).
