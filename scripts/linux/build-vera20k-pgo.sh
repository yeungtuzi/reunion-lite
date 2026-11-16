#!/usr/bin/env bash
# PGO build of the VERA20k fork on aarch64 Linux.
#
# Stage 1 builds an instrumented binary, stage 2 replays a mission through it,
# stage 3 rebuilds with the collected profile.  The firmware and the launcher
# are not involved; point VERA_DIR at the vera20k checkout.
set -euo pipefail

VERA_DIR=${VERA_DIR:-$HOME/build/vera-fork}
PROFILE_DIR=${PROFILE_DIR:-$HOME/build/vera20k-pgo}
TARGET_CPU=${TARGET_CPU:-neoverse-n1}
RUN_DIR=${RUN_DIR:-$HOME/文档/ra2reunion/Run}
CARGO=${CARGO:-$HOME/.cargo/bin/cargo}
LLVM_PROFDATA=${LLVM_PROFDATA:-$($CARGO --version >/dev/null 2>&1 && command -v llvm-profdata || true)}

usage() {
    cat <<'USAGE'
usage: build-vera20k-pgo.sh [instrument|train|use|all]

  instrument  build with -Cprofile-generate into $PROFILE_DIR
  train       run one mission with the instrumented binary (needs a display)
  use         merge the profile and rebuild with -Cprofile-use
  all         instrument + train + use
USAGE
}

instrument() {
    mkdir -p "$PROFILE_DIR"
    ( cd "$VERA_DIR" && RUSTFLAGS="-C target-cpu=$TARGET_CPU -Cprofile-generate=$PROFILE_DIR" \
        "$CARGO" build --release )
}

train() {
    local bin="$VERA_DIR/target/release/vera20k"
    [ -x "$bin" ] || { echo "no instrumented binary at $bin" >&2; exit 1; }
    echo "launch the game from the launcher, play ~60 s, then quit" >&2
    echo "the run writes .profraw files into $PROFILE_DIR" >&2
    echo "run dir: $RUN_DIR" >&2
}

use() {
    local profdata="$PROFILE_DIR/merged.profdata"
    if [ -z "${LLVM_PROFDATA:-}" ]; then
        echo "llvm-profdata not found; set LLVM_PROFDATA=/path/to/llvm-profdata" >&2
        exit 1
    fi
    "$LLVM_PROFDATA" merge -o "$profdata" "$PROFILE_DIR"/*.profraw
    ( cd "$VERA_DIR" && RUSTFLAGS="-C target-cpu=$TARGET_CPU -Cprofile-use=$profdata -Cllvm-args=-pgo-warn-missing-function" \
        "$CARGO" build --release )
}

case "${1:-all}" in
    instrument) instrument ;;
    train) train ;;
    use) use ;;
    all) instrument; train ;;
    *) usage; exit 2 ;;
esac
