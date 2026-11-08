#!/bin/bash
# Builds a single-download macOS DMG:
#   ReunionLite.app (Apache-2.0) + VERA20K.app (GPLv3 fork) + licenses + source
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERA_SRC="${VERA20K_SRC:-$HOME/vscode/vera20k}"
VERSION="$(tr -d "\r\n" < "$ROOT/VERSION")"
DIST="$ROOT/dist"
STAGE="$ROOT/build/macos/dmg"
COMMIT="$(git -C "$VERA_SRC" rev-parse --short=10 HEAD)"

"$ROOT/scripts/macos/build-app.sh"
VERA20K_SRC="$VERA_SRC" "$ROOT/scripts/macos/build-vera20k-app.sh"

rm -rf "$STAGE"
mkdir -p "$STAGE/Licenses" "$STAGE/Source" "$DIST"
cp -R "$ROOT/build/macos/ReunionLite.app" "$STAGE/"
cp -R "$ROOT/build/macos/VERA20K.app" "$STAGE/"

cp "$ROOT/LICENSE" "$STAGE/Licenses/LICENSE-Apache-2.0.txt"
cp "$ROOT/NOTICE" "$STAGE/Licenses/NOTICE-ReunionLite.txt"
cp "$VERA_SRC/LICENSE-GPL" "$STAGE/Licenses/LICENSE-GPL-3.0.txt"
for extra in LGPL-2.1-or-later.txt THIRD_PARTY_LICENSES.html; do
    if [ -f "/Applications/VERA20K.app/Contents/Resources/$extra" ]; then
        cp "/Applications/VERA20K.app/Contents/Resources/$extra" "$STAGE/Licenses/"
    fi
done

SRC_TAR="$STAGE/Source/vera20k-trainer-${COMMIT}-source.tar.gz"
git -C "$VERA_SRC" archive --format=tar.gz --prefix="vera20k-trainer-${COMMIT}/" \
    -o "$SRC_TAR" HEAD

cat > "$STAGE/SOURCE.txt" <<SOURCE
Reunion Lite ${VERSION} — source and licenses
==============================================

ReunionLite.app — Apache License 2.0
  https://github.com/yeungtuzi/reunion-lite
  See Licenses/LICENSE-Apache-2.0.txt and NOTICE-ReunionLite.txt.

VERA20K.app — GNU GPL version 3 (fork of https://github.com/YuriPlanet/vera20k)
  Fork:    https://github.com/yeungtuzi/vera20k
  Branch:  feature/offline-trainer
  Commit:  ${COMMIT}
  Source:  Source/vera20k-trainer-${COMMIT}-source.tar.gz (included in this DMG)
  Build:   cd vera20k-trainer-${COMMIT} && cargo build --release --bin vera20k

The VERA20K binary is distributed under GPLv3; its complete corresponding
source is included in the Source/ folder and at the fork above. No game assets
are included. Red Alert 2 and Yuri's Revenge are trademarks of Electronic Arts.
SOURCE

cat > "$STAGE/README.txt" <<'README'
Reunion Lite 0.5 — macOS (Apple Silicon)
=======================================

Drag both ReunionLite.app and VERA20K.app into /Applications.

ReunionLite.app
  Launcher for Red Alert 2 / Yuri's Revenge campaigns and mission packs.
  Engines:
    1. CrossOver (original gamemd.exe) — requires CrossOver 26.x, Rosetta 2.
    2. VERA20K (experimental) — bundled, no CrossOver/Rosetta needed.
  Choose the engine in Settings. For VERA20K the launcher prepares Run/,
  writes config.toml, mounts the matching Mod&AI aimd.ini and starts
  VERA20K -SPAWN.

Notes:
  - VERA20K is a GPLv3 fork (see SOURCE.txt and Licenses/).
  - The RA2Master trainer only works with the CrossOver/gamemd engine.
  - No game files are included; you need your own Yuri's Revenge assets.
README

hdiutil create -volname "Reunion Lite ${VERSION}" -srcfolder "$STAGE" \
    -ov -format UDZO "$DIST/ReunionLite-${VERSION}-macos-arm64.dmg" >/dev/null

cp "$SRC_TAR" "$DIST/$(basename "$SRC_TAR")"
(
    cd "$DIST"
    shasum -a 256 "ReunionLite-${VERSION}-macos-arm64.dmg" "$(basename "$SRC_TAR")" > SHA256SUMS.txt
    cat SHA256SUMS.txt
)
echo "dmg: $DIST/ReunionLite-${VERSION}-macos-arm64.dmg"
