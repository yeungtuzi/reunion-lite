#!/bin/bash
# Installs a small .app wrapper for the RA2Master trainer into /Applications.
set -euo pipefail
TRAINER="${REUNION_TRAINER:-$HOME/Documents/ra2master/红色警戒2修改大师.exe}"
DEST="/Applications/红色警戒2修改大师.app"

if [ ! -f "$TRAINER" ]; then
    echo "trainer not found: $TRAINER" >&2
    exit 2
fi

rm -rf "$DEST"
mkdir -p "$DEST/Contents/MacOS"
cat > "$DEST/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>RA2Master</string>
    <key>CFBundleDisplayName</key><string>红色警戒2修改大师</string>
    <key>CFBundleIdentifier</key><string>com.yeungtuzi.reunionlite.trainer</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSUIElement</key><true/>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
</dict>
</plist>
PLIST

cat > "$DEST/Contents/MacOS/launch" <<'LAUNCH'
#!/bin/bash
CX="${REUNION_CX_ROOT:-/Applications/CrossOver.app/Contents/SharedSupport/CrossOver}"
BOTTLE="${REUNION_BOTTLE:-RA2}"
TRAINER="${REUNION_TRAINER:-$HOME/Documents/ra2master/红色警戒2修改大师.exe}"
if [ ! -f "$TRAINER" ]; then
    osascript -e 'display alert "找不到修改器" message "请在 Reunion Lite 的设置里指定修改器路径。"'
    exit 1
fi
WINPATH="Z:$(printf '%s' "$TRAINER" | sed 's|/|\\|g')"
exec "$CX/bin/wine" --bottle "$BOTTLE" --no-update \
    --workdir "$(dirname "$TRAINER")" --cx-app "$WINPATH"
LAUNCH
chmod +x "$DEST/Contents/MacOS/launch"
codesign --force --deep --sign - "$DEST" >/dev/null 2>&1 || true
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true
echo "installed: $DEST"
