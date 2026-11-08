#!/bin/bash
# End-to-end smoke test: scan, prepare Run/ for the first training mission and
# launch it through CrossOver.  Prints the command and the log path.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME="${REUNION_GAME_ROOT:-$HOME/Documents/ra2reunion}"
"$ROOT/scripts/build-cli.sh" >/dev/null
"$ROOT/build/rlctl" scan "$GAME" --limit=3
"$ROOT/build/rlctl" launch "$GAME" "${1:-TRN01T}" --dry-run
echo "--- launching for real (Ctrl-C to abort) ---"
"$ROOT/build/rlctl" launch "$GAME" "${1:-TRN01T}"
