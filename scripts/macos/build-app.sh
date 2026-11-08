#!/bin/bash
# Builds build/ReunionLite.app (release), signs it, and optionally installs.
#   SIGN_IDENTITY="Apple Development: ..." ./scripts/build-app.sh
# Signing identity is auto-detected (Developer ID / Apple Development),
# falling back to an ad-hoc signature.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(tr -d "\r\n" < "$ROOT/VERSION")"
GAME="${REUNION_GAME_ROOT:-$HOME/Documents/ra2reunion}"
APP="$ROOT/build/macos/ReunionLite.app"

cd "$ROOT"
swift build -c release --product ReunionLite

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$ROOT/.build/release/ReunionLite" "$APP/Contents/MacOS/ReunionLite"

if [ -f "$GAME/Resources/clienticon.ico" ] && python3 -c "import PIL" >/dev/null 2>&1; then
    ICONSET="$ROOT/build/macos/AppIcon.iconset"
    rm -rf "$ICONSET"
    python3 "$ROOT/scripts/macos/make-icon.py" "$GAME/Resources/clienticon.ico" "$ICONSET" >/dev/null
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns" || true
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>ReunionLite</string>
    <key>CFBundleDisplayName</key><string>Reunion Lite</string>
    <key>CFBundleIdentifier</key><string>com.yeungtuzi.reunionlite.macos</string>
    <key>CFBundleExecutable</key><string>ReunionLite</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${VERSION}</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.strategy-games</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

IDENTITY="${REUNION_SIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
    IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | sed -n 's/.*"\(.*\)"/\1/p' | head -1)"
fi
if [ -n "$IDENTITY" ]; then
    codesign --force --deep --options runtime --sign "$IDENTITY" "$APP" >/dev/null
    echo "signed with: $IDENTITY"
else
    codesign --force --deep --sign - "$APP" >/dev/null
    echo "signed ad-hoc (no codesigning identity found)"
fi
echo "built: $APP"
