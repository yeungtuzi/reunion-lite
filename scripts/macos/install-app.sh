#!/bin/bash
# Builds, signs and installs ReunionLite.app into /Applications.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="/Applications/ReunionLite.app"

"$ROOT/scripts/macos/build-app.sh"

echo "installing to $DEST"
if [ -d "$DEST" ]; then
    osascript -e 'tell application "Reunion Lite" to quit' >/dev/null 2>&1 || true
    sleep 1
    rm -rf "$DEST"
fi
ditto "$ROOT/build/macos/ReunionLite.app" "$DEST"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true
codesign --verify --deep --strict "$DEST" && echo "signature OK"
echo "installed: $DEST"
