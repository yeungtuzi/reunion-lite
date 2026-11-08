#!/bin/bash
# Builds VERA20K.app from our GPLv3 fork (feature/offline-trainer).
#   VERA20K_SRC=~/vscode/vera20k ./scripts/macos/build-vera20k-app.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${VERA20K_SRC:-$HOME/vscode/vera20k}"
APP="$ROOT/build/macos/VERA20K.app"

[ -f "$SRC/Cargo.toml" ] || { echo "VERA20K source not found: $SRC" >&2; exit 2; }
VERSION="$(sed -n 's/^version *= *"\(.*\)"/\1/p' "$SRC/Cargo.toml" | head -1)"
[ -n "$VERSION" ] || VERSION="0.0.0"

(
    cd "$SRC"
    cargo build --release --bin vera20k
)

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$SRC/target/release/vera20k" "$APP/Contents/MacOS/VERA20K"

cp "$SRC/LICENSE-GPL" "$APP/Contents/Resources/LICENSE-GPL" 2>/dev/null || true
for extra in LGPL-2.1-or-later.txt THIRD_PARTY_LICENSES.html README.txt; do
    if [ -f "/Applications/VERA20K.app/Contents/Resources/$extra" ]; then
        cp "/Applications/VERA20K.app/Contents/Resources/$extra" "$APP/Contents/Resources/"
    fi
done
if [ -f "/Applications/VERA20K.app/Contents/Resources/VERA20K.icns" ]; then
    cp "/Applications/VERA20K.app/Contents/Resources/VERA20K.icns" "$APP/Contents/Resources/"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>VERA20K</string>
    <key>CFBundleIconFile</key><string>VERA20K</string>
    <key>CFBundleIdentifier</key><string>io.github.yuriplanet.vera20k.trainer</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>VERA20K</string>
    <key>CFBundleDisplayName</key><string>VERA20K (Reunion)</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.strategy-games</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSHumanReadableCopyright</key><string>GPLv3. Fork of VERA20K by yeungtuzi; original by Halvor Nikolai Rusten and contributors.</string>
    <key>NSDocumentsFolderUsageDescription</key><string>VERA20K reads your Yuri's Revenge game files.</string>
    <key>NSDesktopFolderUsageDescription</key><string>VERA20K reads your Yuri's Revenge game files.</string>
    <key>NSDownloadsFolderUsageDescription</key><string>VERA20K reads your Yuri's Revenge game files.</string>
</dict>
</plist>
PLIST

IDENTITY="${REUNION_SIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
    IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | sed -n 's/.*"\(.*\)"/\1/p' | head -1)"
fi
if [ -n "$IDENTITY" ]; then
    codesign --force --deep --options runtime --sign "$IDENTITY" "$APP" >/dev/null
else
    codesign --force --deep --sign - "$APP" >/dev/null
fi
echo "built: $APP ($VERSION)"
