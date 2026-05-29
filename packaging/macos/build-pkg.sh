#!/usr/bin/env bash
# packaging/macos/build-pkg.sh — build a productbuild .pkg installer.
#
# Usage:  bash packaging/macos/build-pkg.sh <version>
#
# Stages each plugin format into a per-format root, runs pkgbuild to produce
# four component .pkgs (VST3, AU, Standalone, CLAP), then runs productbuild to
# combine them via Distribution.xml. v0.1 ships unsigned. To codesign in v0.2: add
# --sign "Developer ID Installer: <name> (<team>)" to both pkgbuild and
# productbuild calls, then `xcrun notarytool submit ... --wait` + staple.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version> [arch-label]" >&2
    exit 2
fi
version="$1"
arch_label="${2:-macos}"

# ---- paths ------------------------------------------------------------------
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
build_dir="$repo_root/build/macos-release"
artefacts="$build_dir/src/GenVst_artefacts/Release"
out_dir="$build_dir/installer"
work_dir="$build_dir/pkg-work"

vst3_src="$artefacts/VST3/Gen VST.vst3"
au_src="$artefacts/AU/Gen VST.component"
app_src="$artefacts/Standalone/Gen VST.app"
clap_src="$artefacts/CLAP/Gen VST.clap"

[ -d "$vst3_src" ] || { echo "Missing: $vst3_src" >&2; exit 1; }
[ -d "$au_src"   ] || { echo "Missing: $au_src"   >&2; exit 1; }
[ -d "$app_src"  ] || { echo "Missing: $app_src"  >&2; exit 1; }
[ -d "$clap_src" ] || { echo "Missing: $clap_src" >&2; exit 1; }

mkdir -p "$out_dir" "$work_dir/components"
rm -rf "$work_dir/roots" "$work_dir/components"/*
mkdir -p "$work_dir/roots/vst3" "$work_dir/roots/au" "$work_dir/roots/app" "$work_dir/roots/clap"

# ---- stage each format under its own root -----------------------------------
cp -R "$vst3_src" "$work_dir/roots/vst3/"
cp -R "$au_src"   "$work_dir/roots/au/"
cp -R "$app_src"  "$work_dir/roots/app/"
cp -R "$clap_src" "$work_dir/roots/clap/"

# ---- pkgbuild × 3 -----------------------------------------------------------
pkgbuild --root "$work_dir/roots/vst3" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    --identifier       "com.genvst.vst3" \
    --version          "$version" \
    "$work_dir/components/Gen-VST-VST3.pkg"

pkgbuild --root "$work_dir/roots/au" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    --identifier       "com.genvst.au" \
    --version          "$version" \
    "$work_dir/components/Gen-VST-AU.pkg"

pkgbuild --root "$work_dir/roots/app" \
    --install-location "/Applications" \
    --identifier       "com.genvst.standalone" \
    --version          "$version" \
    "$work_dir/components/Gen-VST-Standalone.pkg"

pkgbuild --root "$work_dir/roots/clap" \
    --install-location "/Library/Audio/Plug-Ins/CLAP" \
    --identifier       "com.genvst.clap" \
    --version          "$version" \
    "$work_dir/components/Gen-VST-CLAP.pkg"

# ---- productbuild via Distribution.xml --------------------------------------
# Substitute the version into the static distribution template.
distribution_src="$script_dir/Distribution.xml"
distribution_out="$work_dir/Distribution.xml"
sed "s/__VERSION__/${version}/g" "$distribution_src" > "$distribution_out"

productbuild \
    --distribution "$distribution_out" \
    --package-path "$work_dir/components" \
    "$out_dir/Gen-VST-${version}-${arch_label}.pkg"

echo "Built: $out_dir/Gen-VST-${version}-${arch_label}.pkg"
ls -lh "$out_dir/"
