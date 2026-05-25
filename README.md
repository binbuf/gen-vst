![Gen VST](docs/screenshot.png)

# Gen VST

Sega Genesis sound chip emulator — YM2612 FM synthesis, SN76489 PSG, and 8-bit DAC — as a modern VST3/AU VST instrument plugin.

## Features

- **Six-part multitimbral FM** (YM2612/OPN2) + **3-channel PSG square wave** (SN76489) + **8-bit DAC**
- Shared 16-voice pool with LRU voice stealing across all active parts
- Poly / Mono (legato + glide) / Unison modes per part
- Per-part MIDI channel, transpose, detune, note range, and L/R balance
- Patch browser with factory bank; drag-and-drop folders or files
- VGM / VGZ / DMP patch import (Furnace and DefleMask formats)
- 960×640 skeuomorphic pixel-art UI rendered via JUCE 8 WebView
- VST3 on Windows, macOS, and Linux — AU on macOS — Standalone on all platforms

## Prerequisites

| Tool | Notes |
|------|-------|
| CMake 3.22+ | Bundled with Visual Studio, or [cmake.org](https://cmake.org/download/) |
| C++20 compiler | MSVC on Windows; Clang on macOS/Linux |
| Node.js + npm | Vite bundles the web UI — [nodejs.org](https://nodejs.org/) |
| Ninja | Linux only — `apt install ninja-build` |
| Git | Submodules are required |

## Quick Start

**Clone**
```sh
git clone --recurse-submodules https://github.com/binbuf/gen-vst.git
cd gen-vst
```

**Windows**
```powershell
.\build.ps1            # debug build, deploy to per-user VST3 folder
.\build.ps1 --release  # release build
.\build.ps1 --run      # build + launch Standalone immediately

# If PowerShell script execution is blocked on your machine:
pwsh -ExecutionPolicy Bypass -File .\build.ps1
```

**macOS / Linux**
```sh
./build.sh             # debug build, deploy to per-user plug-in folder
./build.sh --release   # release build
./build.sh --run       # build + launch Standalone immediately
```

The script configures CMake, compiles the Vite web UI bundle, installs factory patches to your per-user data directory, and copies the plugin to a folder your DAW will scan on its next rescan.

## Running Tests

```sh
cmake --build build/<preset> --target RUN_TESTS
# e.g. cmake --build build/windows-debug --target RUN_TESTS
```

## Factory Patches

The plugin ships with ~39 factory patches sourced from the Furnace `tfilib` set (see [Legal](#legal--attribution) below). After a successful build they are installed to:

| Platform | Location |
|----------|----------|
| Windows | `%LOCALAPPDATA%\GenVst\patches` |
| macOS | `~/Library/Application Support/GenVst/patches` |
| Linux | `~/.local/share/GenVst/patches` |

Additional patches can be loaded through the plugin's Import tab or by dragging a folder into the plugin window.

### Factory patches

The bundled factory patches (`extern/patches/*.tfi`) are sourced exclusively from the [`tfilib` instrument set](https://github.com/tildearrow/furnace/tree/master/instruments/OPN/tfilib) included with [Furnace](https://github.com/tildearrow/furnace), published under the GPL. These patches use generic timbre names (`bass.tfi`, `piano.tfi`, `marimba.tfi`, etc.) and contain **no game titles, publisher names, or other trademarked identifiers**. No game-derived patch collections are distributed with this project.

### Third-party libraries

| Library | License | Role |
|---------|---------|------|
| [JUCE 8](https://juce.com/) | GPL v3 (free tier) | Audio plugin framework |
| [ymfm](https://github.com/aaronsgiles/ymfm) | BSD-3-Clause | YM2612 OPN2 emulation |
| [libvgm](https://github.com/ValleyBell/libvgm) | LGPL / BSD | SN76489 PSG emulation |
| [Furnace](https://github.com/tildearrow/furnace) | GPL v3 | VGM/DMP extraction; tfilib factory patches |
| [WebView2 SDK](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) | Microsoft proprietary | WebView host on Windows |
| [GoogleTest](https://github.com/google/googletest) | BSD-3-Clause | Unit tests |

### Project license

Gen VST is released under the **GNU General Public License v3.0**. See [`LICENSE`](LICENSE).
