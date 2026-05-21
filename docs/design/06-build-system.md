# Build System

## Repository Layout

```
gen-vst/
├── CMakeLists.txt           ← root: project, FetchContent JUCE, add_subdirectory
├── CMakePresets.json        ← developer presets (Debug/Release per platform)
├── .gitmodules              ← ymfm + SN76489 lib as submodules
├── third_party/
│   ├── ymfm/                ← git submodule: aaronsgiles/ymfm
│   └── sn76489/             ← git submodule: chosen PSG library (TBD)
├── src/
│   ├── CMakeLists.txt       ← juce_add_plugin, sources, link libraries
│   ├── PluginProcessor.h/cpp
│   ├── PluginEditor.h/cpp
│   ├── VoiceAllocator.h/cpp
│   ├── SN76489Engine.h/cpp
│   ├── DACPlayer.h/cpp
│   ├── PatchSystem.h/cpp
│   └── GenVstLookAndFeel.h/cpp
├── resources/
│   ├── patches/             ← bundled TFI/VGI patch banks
│   └── fonts/
│       └── PressStart2P-Regular.ttf
├── tests/
│   ├── CMakeLists.txt
│   └── *.cpp
└── docs/
    └── design/
```

---

## Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(GenVst VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build unit tests" ON)
option(COPY_PLUGIN_AFTER_BUILD "Copy plugin to system install directory" OFF)

# JUCE via FetchContent — pin to known-good tag
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# ymfm as git submodule — require it to be initialized
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src/ymfm.h")
    message(FATAL_ERROR
        "ymfm submodule not found. Run:\n"
        "  git submodule update --init --recursive")
endif()

add_subdirectory(third_party)
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

## src/CMakeLists.txt — juce_add_plugin

```cmake
# Collect patch files for binary embedding
file(GLOB_RECURSE PATCH_FILES
    "${CMAKE_SOURCE_DIR}/resources/patches/*.tfi"
    "${CMAKE_SOURCE_DIR}/resources/patches/*.vgi")

juce_add_binary_data(GenVstBinaryData
    SOURCES
        "${CMAKE_SOURCE_DIR}/resources/fonts/PressStart2P-Regular.ttf"
        ${PATCH_FILES})

juce_add_plugin(GenVst
    COMPANY_NAME                "YourStudio"
    COMPANY_WEBSITE             "https://example.com"
    PLUGIN_MANUFACTURER_CODE    GnVs     # 4 chars; AU requires first=uppercase, rest=lowercase
    PLUGIN_CODE                 Genv     # 4 chars unique per plugin
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Gen VST"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE             TRUE
    COPY_PLUGIN_AFTER_BUILD     ${COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES             "Instrument|Synth"
    DESCRIPTION                 "Sega Genesis YM2612+SN76489 emulation"
    BUNDLE_ID                   "com.yourstudio.genvst"
    VERSION                     "0.1.0"
)

target_sources(GenVst PRIVATE
    PluginProcessor.cpp  PluginProcessor.h
    PluginEditor.cpp     PluginEditor.h
    VoiceAllocator.cpp   VoiceAllocator.h
    SN76489Engine.cpp    SN76489Engine.h
    DACPlayer.cpp        DACPlayer.h
    PatchSystem.cpp      PatchSystem.h
    GenVstLookAndFeel.cpp GenVstLookAndFeel.h
)

# ymfm sources — inline into plugin target, NOT a separate static lib
# (avoids LTO boundary issues and simplifies the build graph)
target_sources(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_opn.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src/ymfm_misc.cpp"
)
target_include_directories(GenVst PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/ymfm/src")

# SN76489 library sources (adjust path when library is chosen)
# target_sources(GenVst PRIVATE "${CMAKE_SOURCE_DIR}/third_party/sn76489/sn76489.c")
# target_include_directories(GenVst PRIVATE "${CMAKE_SOURCE_DIR}/third_party/sn76489")

target_compile_definitions(GenVst PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(GenVst PRIVATE
    GenVstBinaryData
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
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
```

**AU PLUGIN_MANUFACTURER_CODE note:** GarageBand 10.3+ requires exactly one uppercase letter followed by three lowercase letters (e.g., `GnVs`). Failing this will cause the AU to fail validation in Logic Pro.

---

## Platform Targets

### Windows

- Format: VST3 only (AU not supported on Windows)
- Compiler: MSVC 2019+, Clang-cl also works
- Extra flags: `/std:c++17`, `/W3`
- Install path (system-wide): `%CommonProgramFiles%\VST3\`
- Install path (user): `%LOCALAPPDATA%\Programs\Common\VST3\`
- During development: set `COPY_PLUGIN_AFTER_BUILD OFF` to avoid UAC elevation prompts

### macOS

- Formats: VST3 + AU
- Compiler: AppleClang 14+ or Clang 15+
- Min deployment target: macOS 10.15 (Catalina) — required for AU v2/v3 host compatibility
- Universal binary: set in root CMakeLists.txt:
  ```cmake
  if(APPLE)
      set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
      set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
  endif()
  ```
- VST3 install: `~/Library/Audio/Plug-Ins/VST3/`
- AU install: `~/Library/Audio/Plug-Ins/Components/`
- AU validation after build:
  ```bash
  auval -v aumu GnVs YStu
  ```
  Replace `YStu` with your 4-character manufacturer code.

### Linux

- Format: VST3 only
- Compiler: GCC 11+ or Clang 14+
- Required flags: `-std=c++17 -fPIC` (mandatory for shared library)
- System packages required: `libasound2-dev` (ALSA), `libx11-dev`, `libxcursor-dev`, `libxrandr-dev`, `libxinerama-dev`, `libfreetype6-dev`
- Install path (user): `~/.vst3/`
- Install path (system): `/usr/lib/vst3/`

---

## CMakePresets.json

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "windows-debug",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "windows-release",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "macos-release",
      "generator": "Xcode",
      "binaryDir": "${sourceDir}/build/macos-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "linux-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ]
}
```

---

## GitHub Actions CI

Three independent jobs in `.github/workflows/build.yml`:

```yaml
jobs:
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - run: cmake --preset windows-release
      - run: cmake --build build/windows-release --config Release
      - uses: actions/upload-artifact@v4
        with:
          name: GenVst-Windows-VST3
          path: build/windows-release/GenVst_artefacts/Release/VST3/

  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - run: cmake --preset macos-release
      - run: cmake --build build/macos-release --config Release
      # auval validation (no-display CI, may need DISPLAY workaround)

  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - run: sudo apt-get install -y libasound2-dev libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libfreetype6-dev libgl-dev
      - run: cmake --preset linux-release
      - run: cmake --build build/linux-release
```

Release artifacts (VST3 bundles) are uploaded on tag push via an additional `on: push: tags: ['v*']` condition.

macOS code signing requires an Apple Developer certificate stored in GitHub Secrets. Defer until pre-release.

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
| `PatchLoaderTests.cpp` | TFI/VGI/DMP parse, round-trip, malformed input rejection |
| `VoiceAllocatorTests.cpp` | Note-on/off, LRU stealing, sustain pedal hold, all-notes-off |
| `FrequencyCalcTests.cpp` | MIDI note → F-number + BLK, pitch bend recalc, octave boundary |
| `RegisterWriteTests.cpp` | Correct ymfm register sequence for a known patch (compare against expected register log) |

---

## Submodule Setup

```bash
# Add ymfm as a submodule (run once):
git submodule add https://github.com/aaronsgiles/ymfm.git third_party/ymfm

# Add SN76489 library (once decided):
git submodule add <url> third_party/sn76489

# Initialize on fresh clone:
git submodule update --init --recursive
```
