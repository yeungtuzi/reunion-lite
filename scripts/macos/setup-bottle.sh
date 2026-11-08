#!/bin/bash
# Creates the CrossOver bottle used by ReunionLite and exposes D: (used by the
# Lindlark mission packs) at ~/Documents.
#
#   ./scripts/setup-bottle.sh            # create / repair bottle "RA2"
#   REUNION_BOTTLE=YR ./scripts/setup-bottle.sh
set -euo pipefail

CX="${REUNION_CX_ROOT:-/Applications/CrossOver.app/Contents/SharedSupport/CrossOver}"
BOTTLE="${REUNION_BOTTLE:-RA2}"
D_ROOT="${REUNION_D_ROOT:-$HOME/Documents}"

if [ ! -x "$CX/bin/wine" ]; then
    echo "CrossOver not found at $CX" >&2
    echo "Install CrossOver 26.3.0 (NOT 27): brew install --cask crossover" >&2
    exit 2
fi

if "$CX/bin/cxbottle" --bottle "$BOTTLE" --status >/dev/null 2>&1; then
    echo "bottle '$BOTTLE' already exists"
else
    echo "creating bottle '$BOTTLE' (win10_64) ..."
    "$CX/bin/cxbottle" --bottle "$BOTTLE" --create --template win10_64 \
        --description "Reunion Lite - RA2/YR"
fi

BOTTLE_DIR="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
if [ -d "$BOTTLE_DIR/dosdevices" ]; then
    if [ ! -e "$BOTTLE_DIR/dosdevices/d:" ]; then
        ln -s "$D_ROOT" "$BOTTLE_DIR/dosdevices/d:"
        echo "linked D: -> $D_ROOT"
    else
        echo "D: already mapped: $(readlink "$BOTTLE_DIR/dosdevices/d:" || true)"
    fi
    if [ ! -e "$BOTTLE_DIR/dosdevices/z:" ]; then
        ln -s / "$BOTTLE_DIR/dosdevices/z:"
        echo "linked Z: -> /"
    fi
fi

"$CX/bin/wine" --bottle "$BOTTLE" --version
echo "bottle ready: $BOTTLE_DIR"
