#!/usr/bin/env bash
# build.sh — build, deploy, and prepare Gen VST for end-to-end testing (macOS/Linux).
#
# Configures and builds the plugin via CMake, installs the factory patches into
# the per-user data directory, copies the VST3 and the CLAP (and the AU on macOS)
# into the plug-in folders a DAW will scan, and optionally launches the
# Standalone. Interim developer convenience — the shippable installer is a later task.
#
#   ./build.sh [--release] [--system] [--run] [--clean] [--uninstall] [--help]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- output helpers ---------------------------------------------------------
step() { printf '\033[36m==> %s\033[0m\n' "$1"; }
info() { printf '    %s\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

usage() {
    cat <<'EOF'
build.sh — build, deploy and prepare Gen VST for end-to-end testing.

  ./build.sh [--release] [--system] [--run] [--clean] [--uninstall] [--help]

  --release     Build/deploy the Release configuration (default: Debug).
  --system      Deploy to the system-wide plug-in folder, same location the
                installer uses (sudo is used for the copy).
                Default: the per-user plug-in folder (no sudo).
                Ignored during --uninstall, which always sweeps both per-user
                and system folders.
  --run         Launch the Standalone after a successful deploy.
  --clean       Wipe the build directory first (full reconfigure).
                Use after CMakeLists changes or weird build failures.
                When combined with --uninstall, also removes the build directory.
  --uninstall   Remove every Gen VST install without building: per-user dev copies,
                system dev copies, and the copy placed by the installer (system
                plug-in folders, /Applications on macOS, /usr/lib on Linux), plus
                factory patches. macOS also forgets the .pkg receipts. Removing a
                system/installer copy uses sudo. The AppImage on Linux is portable
                and cannot be auto-removed — delete it manually.
                Combine with --clean to also wipe the build directory.
  --help        Show this help and exit.
EOF
}

# ---- parse arguments --------------------------------------------------------
opt_release=0
opt_system=0
opt_run=0
opt_clean=0
opt_uninstall=0

while [ $# -gt 0 ]; do
    case "$1" in
        --release|-r)   opt_release=1 ;;
        --system|-s)    opt_system=1 ;;
        --run)          opt_run=1 ;;
        --clean)        opt_clean=1 ;;
        --uninstall|-u) opt_uninstall=1 ;;
        --help|-h)      usage; exit 0 ;;
        *) usage; fail "Unknown option: $1" ;;
    esac
    shift
done

# ---- platform ---------------------------------------------------------------
case "$(uname -s)" in
    Darwin) os=macos; multi_config=1                                  # Xcode — multi-config
            patch_dir="$HOME/Library/Application Support/GenVst/patches" ;;
    Linux)  os=linux; multi_config=0                                  # Ninja — single-config
            patch_dir="$HOME/.local/share/GenVst/patches" ;;
    *) fail "Unsupported OS: $(uname -s). Use build.ps1 on Windows." ;;
esac

if [ "$opt_release" -eq 1 ]; then config=Release; else config=Debug; fi
config_lc="$(printf '%s' "$config" | tr '[:upper:]' '[:lower:]')"
preset="${os}-${config_lc}"
build_dir="$SCRIPT_DIR/build/$preset"

# ---- deploy paths (needed by the deploy step) -------------------------------
if [ "$opt_system" -eq 1 ]; then SUDO=sudo; else SUDO=""; fi

if [ "$os" = macos ]; then
    if [ "$opt_system" -eq 1 ]; then
        vst3_root="/Library/Audio/Plug-Ins/VST3"
        au_root="/Library/Audio/Plug-Ins/Components"
        clap_root="/Library/Audio/Plug-Ins/CLAP"
    else
        vst3_root="$HOME/Library/Audio/Plug-Ins/VST3"
        au_root="$HOME/Library/Audio/Plug-Ins/Components"
        clap_root="$HOME/Library/Audio/Plug-Ins/CLAP"
    fi
    vst3_dest="$vst3_root/Gen VST.vst3"
    au_dest="$au_root/Gen VST.component"
    clap_dest="$clap_root/Gen VST.clap"
else
    if [ "$opt_system" -eq 1 ]; then
        vst3_root="/usr/lib/vst3"
        clap_root="/usr/lib/clap"
    else
        vst3_root="$HOME/.vst3"
        clap_root="$HOME/.clap"
    fi
    vst3_dest="$vst3_root/Gen VST.vst3"
    au_dest=""
    clap_dest="$clap_root/Gen VST.clap"
fi

# ---- uninstall --------------------------------------------------------------
if [ "$opt_uninstall" -eq 1 ]; then
    # Sweep every place a Gen VST install can live, regardless of --system: the
    # per-user dev copies, the system dev copies, and the copy placed by the
    # installer. There is no installer-run uninstaller on macOS/Linux, so the
    # installer footprint is removed by deleting its files (system plug-in folders,
    # /Applications on macOS, /usr/lib on Linux) and, on macOS, forgetting the .pkg
    # receipts. sudo is used only for system-owned paths, and only when present.
    if [ "$os" = macos ]; then
        user_paths=(
            "$HOME/Library/Audio/Plug-Ins/VST3/Gen VST.vst3"
            "$HOME/Library/Audio/Plug-Ins/Components/Gen VST.component"
            "$HOME/Library/Audio/Plug-Ins/CLAP/Gen VST.clap"
        )
        sys_paths=(
            "/Library/Audio/Plug-Ins/VST3/Gen VST.vst3"
            "/Library/Audio/Plug-Ins/Components/Gen VST.component"
            "/Library/Audio/Plug-Ins/CLAP/Gen VST.clap"
            "/Applications/Gen VST.app"
        )
        receipts=(com.genvst.vst3 com.genvst.au com.genvst.clap com.genvst.standalone)
    else
        user_paths=(
            "$HOME/.vst3/Gen VST.vst3"
            "$HOME/.clap/Gen VST.clap"
        )
        sys_paths=(
            "/usr/lib/vst3/Gen VST.vst3"
            "/usr/lib/clap/Gen VST.clap"
        )
        receipts=()
    fi

    echo
    step "Gen VST — uninstall (all installs)"
    info "Per-user:"; for p in "${user_paths[@]}"; do info "  $p"; done
    info "System:";   for p in "${sys_paths[@]}";  do info "  $p"; done
    info "Patches : $patch_dir"
    [ "$opt_clean" -eq 1 ] && info "Build   : $build_dir (will be removed)"
    echo

    # 1. Per-user copies (no sudo).
    for p in "${user_paths[@]}"; do
        if [ -e "$p" ]; then rm -rf "$p"; info "Removed: $p"; else info "Not present: $p"; fi
    done

    # 2. System + installer copies (sudo only when something is actually present).
    sys_present=()
    for p in "${sys_paths[@]}"; do [ -e "$p" ] && sys_present+=("$p"); done

    receipts_present=()
    if [ "$os" = macos ] && command -v pkgutil >/dev/null 2>&1; then
        for r in "${receipts[@]}"; do
            pkgutil --pkg-info "$r" >/dev/null 2>&1 && receipts_present+=("$r")
        done
    fi

    if [ "${#sys_present[@]}" -gt 0 ] || [ "${#receipts_present[@]}" -gt 0 ]; then
        info "System/installer copy present — sudo may prompt for your password."
        if [ "${#sys_present[@]}" -gt 0 ]; then
            for p in "${sys_present[@]}"; do sudo rm -rf "$p"; info "Removed: $p"; done
        fi
        if [ "${#receipts_present[@]}" -gt 0 ]; then
            for r in "${receipts_present[@]}"; do sudo pkgutil --forget "$r" >/dev/null; info "Forgot receipt: $r"; done
        fi
    else
        info "No system or installer copy present."
    fi

    # 3. Factory patches (no sudo — per-user).
    if [ -d "$patch_dir" ]; then rm -rf "$patch_dir"; info "Removed: $patch_dir"; else info "Not present: $patch_dir"; fi

    # 4. Optionally wipe the build directory.
    if [ "$opt_clean" -eq 1 ] && [ -d "$build_dir" ]; then
        step "Cleaning $build_dir"
        rm -rf "$build_dir"
    fi

    # A downloaded Linux AppImage is portable — it has no fixed install path to remove.
    [ "$os" = linux ] && info "Note: a downloaded AppImage is portable — delete it manually if you keep one."

    echo; step "Done."; echo
    exit 0
fi

# ---- toolchain check --------------------------------------------------------
command -v cmake >/dev/null 2>&1 || \
    fail "cmake not found on PATH. Install it (macOS: 'brew install cmake'; Linux: 'apt install cmake')."
command -v node >/dev/null 2>&1 || \
    fail "node not found on PATH. Node.js is required to build the web UI — see https://nodejs.org/."
command -v npm >/dev/null 2>&1 || \
    fail "npm not found on PATH (it ships with Node.js)."
if [ "$os" = linux ]; then
    command -v ninja >/dev/null 2>&1 || \
        fail "ninja not found on PATH (the linux preset uses the Ninja generator; 'apt install ninja-build')."
fi

# ---- banner -----------------------------------------------------------------
echo
step "Gen VST — build & deploy"
info "config : $config"
info "preset : $preset"
if [ "$opt_system" -eq 1 ]; then
    info "deploy : system plug-in folder (sudo)"
else
    info "deploy : per-user plug-in folder"
fi
echo

# ---- clean ------------------------------------------------------------------
if [ "$opt_clean" -eq 1 ] && [ -d "$build_dir" ]; then
    step "Cleaning $build_dir"
    rm -rf "$build_dir"
fi

# ---- configure --------------------------------------------------------------
# GENVST_DEV_SERVER=OFF / COPY_PLUGIN_AFTER_BUILD=OFF are set explicitly so this
# always produces the self-contained embedded-bundle plugin and never triggers
# JUCE's own post-build copy step.
step "Configuring (preset: $preset)"
cmake --preset "$preset" -DGENVST_DEV_SERVER=OFF -DCOPY_PLUGIN_AFTER_BUILD=OFF

# ---- build ------------------------------------------------------------------
step "Building $config (this also builds the Vite web UI bundle)"
if [ "$multi_config" -eq 1 ]; then
    cmake --build "$build_dir" --config "$config"
else
    cmake --build "$build_dir"
fi

# ---- install factory patches ------------------------------------------------
# Mirror the full extern/patches/ tree (fm/<category>/*, sq/*) into the per-
# user data dir so the Standalone reads the same layout the VST3 bundle has.
# `cmake --install` is intentionally NOT used: it would also run JUCE's own
# framework install rules. The gitignored extra/ developer set never reaches
# this find — it lives in a sibling directory (ADR-0004).
step "Installing factory patches"
patch_src="$SCRIPT_DIR/extern/patches"
patch_count=$(find "$patch_src" -type f \( \
    -name '*.tfi' -o -name '*.vgi' -o -name '*.dmp' \
    -o -name '*.y12' -o -name '*.opm' -o -name '*.psg' \) | wc -l)
[ "$patch_count" -gt 0 ] || fail "No factory patches found in $patch_src"
rm -rf "$patch_dir"
mkdir -p "$(dirname "$patch_dir")"
cp -R "$patch_src" "$patch_dir"
info "$patch_count patch file(s) -> $patch_dir"

# ---- deploy -----------------------------------------------------------------
artefacts="$build_dir/src/GenVst_artefacts/$config"
vst3_src="$artefacts/VST3/Gen VST.vst3"
clap_src="$artefacts/CLAP/Gen VST.clap"   # bundle dir on macOS, single file on Linux
[ -d "$vst3_src" ] || fail "Built VST3 not found at: $vst3_src"
[ -e "$clap_src" ] || fail "Built CLAP not found at: $clap_src"

if [ "$os" = macos ]; then
    standalone="$artefacts/Standalone/Gen VST.app"
else
    standalone="$artefacts/Standalone/Gen VST"
fi

# Replace a bundle (a directory) at "$2/<name>" with a fresh recursive copy of "$1".
deploy_bundle() {
    local src="$1" root="$2"
    local dest="$root/$(basename "$src")"
    $SUDO mkdir -p "$root"
    $SUDO rm -rf "$dest"
    $SUDO cp -R "$src" "$dest"
}

step "Deploying VST3 -> $vst3_dest"
deploy_bundle "$vst3_src" "$vst3_root"

# deploy_bundle uses `cp -R`, which handles both the macOS .clap bundle and the
# single-file Linux .clap.
step "Deploying CLAP -> $clap_dest"
deploy_bundle "$clap_src" "$clap_root"

au_deployed=""
if [ "$os" = macos ]; then
    au_src="$artefacts/AU/Gen VST.component"
    if [ -d "$au_src" ]; then
        step "Deploying AU -> $au_dest"
        deploy_bundle "$au_src" "$au_root"
        au_deployed="$au_dest"
    fi
fi

# ---- summary ----------------------------------------------------------------
echo
step "Done."
info "VST3 deployed  : $vst3_dest"
info "CLAP deployed  : $clap_dest"
[ -n "$au_deployed" ] && info "AU deployed    : $au_deployed"
info "Standalone     : $standalone"
info "Factory patches: $patch_dir"
echo
info "Test end-to-end:"
info "  * Run the Standalone above (or re-run with --run) — the pixel-art UI should appear."
info "  * In your DAW, rescan plug-ins and load 'Gen VST' (Instrument/Synth); play MIDI."
[ "$os" = macos ] && info "  * Validate the AU:  auval -v aumu Genv GnVs"
echo

# ---- run --------------------------------------------------------------------
if [ "$opt_run" -eq 1 ]; then
    if [ -e "$standalone" ]; then
        step "Launching Standalone"
        if [ "$os" = macos ]; then
            open "$standalone"
        else
            ( "$standalone" >/dev/null 2>&1 & )
        fi
    else
        info "Standalone not found at $standalone — skipping --run."
    fi
fi
