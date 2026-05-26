#!/usr/bin/env bash
# install.sh — drop Gen VST.vst3 + factory patches into the user's plug-in and
# data dirs. Bundled into the Linux tar.gz release; run from the extracted
# directory.
#
# Usage:  ./install.sh

set -euo pipefail

cd "$(dirname "$(readlink -f "${0}")")"

vst3_dest="$HOME/.vst3"
patches_dest="${XDG_DATA_HOME:-$HOME/.local/share}/GenVst/patches"

if [ ! -d "Gen VST.vst3" ]; then
    echo "ERROR: 'Gen VST.vst3' not found in $(pwd). Extract the tarball and run from inside it." >&2
    exit 1
fi

echo "Installing Gen VST..."
mkdir -p "$vst3_dest" "$patches_dest" "$patches_dest/sq"

rm -rf "$vst3_dest/Gen VST.vst3"
cp -r "Gen VST.vst3" "$vst3_dest/"
echo "  VST3:    $vst3_dest/Gen VST.vst3"

if [ -d factory-patches ]; then
    cp -f factory-patches/*.tfi    "$patches_dest/"    2>/dev/null || true
    cp -f factory-patches/*.vgi    "$patches_dest/"    2>/dev/null || true
    cp -f factory-patches/*.y12    "$patches_dest/"    2>/dev/null || true
    cp -f factory-patches/sq/*.psg "$patches_dest/sq/" 2>/dev/null || true
    echo "  Patches: $patches_dest"
fi

echo
echo "Done. Rescan plug-ins in your DAW; Gen VST will appear under Instruments/Synth."
