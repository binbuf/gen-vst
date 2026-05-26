#!/usr/bin/env bash
# update-version.sh — bump the project version everywhere before a release.
#
# Usage:  ./update-version.sh 0.2.0
#
# Rewrites every pinned version string the build / installers / about-modal
# read from, so a release is one `git tag v<X.Y.Z>` + push away. After running:
#     git diff                      # eyeball the changes
#     git commit -am "chore: bump version to v<X.Y.Z>"
#     git tag    v<X.Y.Z>
#     git push origin main --tags
#
# Files touched:
#   CMakeLists.txt                project(GenVst VERSION …)
#   src/CMakeLists.txt            juce_add_plugin(… VERSION "…")
#   ui/package.json               "version": "…"
#   ui/package-lock.json          "version": "…"  (top-level + root package)
#   ui/vite.config.js             __GENVST_VERSION__ Vite define
#   ui/src/modals/about.js        fallback when the Vite define isn't applied
#   .github/workflows/release.yml dev-build fallback ("X.Y.Z-dev.…", 4 jobs)
#
# packaging/windows/installer.iss is intentionally skipped — its "0.0.0-dev"
# default is a CI-overridden placeholder, not a project version.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <version>     (e.g. 0.2.0)" >&2
  exit 2
fi

NEW="$1"
if [[ ! "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: version must be X.Y.Z (got '$NEW')" >&2
  exit 2
fi

cd "$(dirname "$0")"

# Portable in-place sed: GNU wants `-i`, BSD/macOS wants `-i ''`.
if sed --version >/dev/null 2>&1; then
  sedi() { sed -i -E "$@"; }
else
  sedi() { sed -i '' -E "$@"; }
fi

V='[0-9]+\.[0-9]+\.[0-9]+'

echo "Bumping to v${NEW}…"

# CMakeLists.txt   ->   project(GenVst VERSION X.Y.Z LANGUAGES …)
sedi "s/(project\(GenVst VERSION )${V}/\1${NEW}/" CMakeLists.txt

# src/CMakeLists.txt   ->   VERSION "X.Y.Z"   inside juce_add_plugin(...)
sedi "s/(^[[:space:]]*VERSION[[:space:]]+\")${V}(\")/\1${NEW}\2/" src/CMakeLists.txt

# ui/package.json   ->   "version": "X.Y.Z"
sedi "s/(\"version\":[[:space:]]*\")${V}(\")/\1${NEW}\2/" ui/package.json

# ui/package-lock.json   ->   the two top-of-file "version" fields (top-level
# meta on line 3, root `packages[""]` entry on line 9). Restricted to the
# first 13 lines so we never touch the per-dependency `"version"` fields
# inside `node_modules/*`, which start at line 14.
sedi "1,13 s/(\"version\":[[:space:]]*\")${V}(\")/\1${NEW}\2/" ui/package-lock.json

# ui/vite.config.js   ->   __GENVST_VERSION__: JSON.stringify("X.Y.Z")
sedi "s/(__GENVST_VERSION__:[[:space:]]*JSON\.stringify\(\")${V}(\"\))/\1${NEW}\2/" \
  ui/vite.config.js

# ui/src/modals/about.js   ->   : "X.Y.Z";   (ternary fallback, line on its own)
sedi "s/^([[:space:]]*:[[:space:]]*\")${V}(\";[[:space:]]*\$)/\1${NEW}\2/" \
  ui/src/modals/about.js

# .github/workflows/release.yml   ->   "X.Y.Z-dev.…"   (4 jobs: win/macarm/macx86/linux)
sedi "s/(\")${V}(-dev\.)/\1${NEW}\2/g" .github/workflows/release.yml

echo
echo "Result:"
grep -nE "VERSION ${NEW//./\\.}|\"${NEW//./\\.}(\"|-dev)" \
  CMakeLists.txt \
  src/CMakeLists.txt \
  ui/package.json \
  ui/vite.config.js \
  ui/src/modals/about.js \
  .github/workflows/release.yml \
  || { echo "WARNING: no matches found — patterns may need adjustment." >&2; exit 1; }

echo
echo "Done. Review with 'git diff', commit, then tag v${NEW}."
