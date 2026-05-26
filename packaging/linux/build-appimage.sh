#!/usr/bin/env bash
# packaging/linux/build-appimage.sh — bundle the Standalone as an AppImage.
#
# Usage:  bash packaging/linux/build-appimage.sh <version>
#
# WebKitGTK is intentionally NOT bundled (--exclude-library): pulling in
# libwebkit2gtk-4.1 + libsoup + GStreamer would add 200+ MB to the image and
# routinely conflicts with the host's WebKitGTK. Users must have the runtime
# installed (`sudo apt install libwebkit2gtk-4.1-0`); this is called out in the
# release body. The trade-off may be revisited if it generates support load.

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
work_dir="$build_dir/appimage-work"

standalone_src="$artefacts/Standalone/Gen VST"
[ -f "$standalone_src" ] || { echo "Missing Standalone: $standalone_src" >&2; exit 1; }

mkdir -p "$out_dir"
rm -rf "$work_dir"
appdir="$work_dir/AppDir"
mkdir -p \
    "$appdir/usr/bin" \
    "$appdir/usr/share/applications" \
    "$appdir/usr/share/icons/hicolor/256x256/apps" \
    "$appdir/usr/share/gen-vst-patches"

# ---- stage Standalone, icon, desktop ----------------------------------------
cp "$standalone_src" "$appdir/usr/bin/gen-vst"
chmod +x "$appdir/usr/bin/gen-vst"
cp "$repo_root/resources/icon.png" "$appdir/usr/share/icons/hicolor/256x256/apps/gen-vst.png"
cp "$script_dir/gen-vst.desktop"   "$appdir/usr/share/applications/gen-vst.desktop"

# Stage factory patches inside the AppImage; AppRun seeds the user data dir
# on first launch.
cp "$repo_root/extern/patches"/*.tfi "$appdir/usr/share/gen-vst-patches/" 2>/dev/null || true
cp "$repo_root/extern/patches"/*.vgi "$appdir/usr/share/gen-vst-patches/" 2>/dev/null || true
cp "$repo_root/extern/patches"/*.y12 "$appdir/usr/share/gen-vst-patches/" 2>/dev/null || true
mkdir -p "$appdir/usr/share/gen-vst-patches/sq"
cp "$repo_root/extern/patches/sq"/*.psg "$appdir/usr/share/gen-vst-patches/sq/" 2>/dev/null || true

# Custom AppRun: seed ~/.local/share/GenVst/patches on first launch, then exec.
cat > "$appdir/AppRun" <<'APPRUN'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(dirname "$(readlink -f "${0}")")"
patches="${XDG_DATA_HOME:-$HOME/.local/share}/GenVst/patches"
if [ ! -d "$patches" ]; then
    mkdir -p "$patches"
    cp -r "$HERE/usr/share/gen-vst-patches/." "$patches/" 2>/dev/null || true
fi
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/gen-vst" "$@"
APPRUN
chmod +x "$appdir/AppRun"

# linuxdeploy expects top-level symlinks to the desktop file and icon.
ln -sf "usr/share/applications/gen-vst.desktop" "$appdir/gen-vst.desktop"
ln -sf "usr/share/icons/hicolor/256x256/apps/gen-vst.png" "$appdir/gen-vst.png"
ln -sf "gen-vst.png" "$appdir/.DirIcon"

# ---- fetch linuxdeploy ------------------------------------------------------
cd "$work_dir"
curl -sSL -o linuxdeploy.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy.AppImage

# ---- run linuxdeploy --------------------------------------------------------
# --exclude-library swallows libwebkit2gtk and its transitive GStreamer/libsoup
# pull-in. The Standalone has WebKitGTK as a *runtime* dependency.
export OUTPUT="$out_dir/Gen-VST-${version}-x86_64.AppImage"
export VERSION="${version}"

./linuxdeploy.AppImage \
    --appdir "$appdir" \
    --output appimage \
    --exclude-library 'libwebkit2gtk*' \
    --exclude-library 'libgstreamer*' \
    --exclude-library 'libsoup*'

# linuxdeploy writes to ./Gen_VST-<version>-x86_64.AppImage; relocate.
generated=$(ls Gen_VST-*-x86_64.AppImage 2>/dev/null || true)
if [ -z "$generated" ]; then
    # Older linuxdeploy uses the desktop file's Name="Gen VST" verbatim.
    generated=$(ls 'Gen VST'-*.AppImage 2>/dev/null || true)
fi
if [ -n "$generated" ]; then
    mv "$generated" "$OUTPUT"
fi

[ -f "$OUTPUT" ] || { echo "AppImage missing at $OUTPUT" >&2; ls -la; exit 1; }
echo "Built: $OUTPUT"
ls -lh "$OUTPUT"
