#!/usr/bin/env bash
# packaging/linux/build-tarball.sh — package the VST3 + CLAP plug-ins and the
# factory patches as a user-installable tar.gz.
#
# Usage:  bash packaging/linux/build-tarball.sh <version>
#
# The tarball drops a "Gen VST.vst3" bundle directory, a "Gen VST.clap" file, a
# factory-patches/ tree, and an install.sh that places them under the
# conventional per-user paths.

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version>" >&2
    exit 2
fi
version="$1"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
build_dir="$repo_root/build/linux-release"
artefacts="$build_dir/src/GenVst_artefacts/Release"
out_dir="$build_dir/installer"

vst3_src="$artefacts/VST3/Gen VST.vst3"
clap_src="$artefacts/CLAP/Gen VST.clap"
[ -d "$vst3_src" ] || { echo "Missing VST3: $vst3_src" >&2; exit 1; }
[ -f "$clap_src" ] || { echo "Missing CLAP: $clap_src" >&2; exit 1; }

mkdir -p "$out_dir"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

# ---- stage the payload ------------------------------------------------------
cp -r "$vst3_src"                        "$stage/"
cp    "$clap_src"                        "$stage/"
mkdir -p "$stage/factory-patches/sq"
cp "$repo_root/extern/patches"/*.tfi     "$stage/factory-patches/"     2>/dev/null || true
cp "$repo_root/extern/patches"/*.vgi     "$stage/factory-patches/"     2>/dev/null || true
cp "$repo_root/extern/patches"/*.y12     "$stage/factory-patches/"     2>/dev/null || true
cp "$repo_root/extern/patches/sq"/*.psg  "$stage/factory-patches/sq/"  2>/dev/null || true
cp "$script_dir/install.sh"              "$stage/install.sh"
chmod +x "$stage/install.sh"

# Pretty-print a README inside the tarball so users know what they got.
cat > "$stage/README.txt" <<EOF
Gen VST ${version} — Linux plug-in package (VST3 + CLAP)

Contents:
  Gen VST.vst3/        — the VST3 plug-in bundle
  Gen VST.clap         — the CLAP plug-in
  factory-patches/     — 39 FM .tfi + 6 .vgi + 2 .y12 + 12 SQ .psg
  install.sh           — copies the above to per-user locations

Quick install:
  ./install.sh

The script drops the .vst3 into ~/.vst3, the .clap into ~/.clap, and the
patches into ~/.local/share/GenVst/patches. Rescan plug-ins in your DAW.

Manual install (system-wide, needs sudo):
  sudo cp -r 'Gen VST.vst3' /usr/lib/vst3/
  sudo cp    'Gen VST.clap' /usr/lib/clap/

Source + issues: https://github.com/binbuf/gen-vst
EOF

# ---- tar.gz -----------------------------------------------------------------
tarball="$out_dir/Gen-VST-${version}-linux-vst3.tar.gz"
tar -czf "$tarball" -C "$stage" .

echo "Built: $tarball"
ls -lh "$tarball"
