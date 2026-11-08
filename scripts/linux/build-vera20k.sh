#!/bin/sh
# Build the patched VERA20K that Reunion Lite uses on Linux/aarch64.
#
# Upstream VERA20K refuses Reunion campaign AIMD files (unknown task-force
# members), so the launcher needs our fork: https://github.com/yeungtuzi/vera20k
#
# Usage:
#   scripts/linux/build-vera20k.sh            # clone/update + build + install
#   VERA20K_SRC=~/src/vera20k ...             # custom checkout
#   VERA20K_PREFIX=~/.local ...               # custom install prefix
set -eu
REPO=${VERA20K_REPO:-https://github.com/yeungtuzi/vera20k.git}
SRC=${VERA20K_SRC:-$HOME/vera20k}
PREFIX=${VERA20K_PREFIX:-$HOME/.local}

command -v cargo >/dev/null 2>&1 || {
    echo "cargo not found: install Rust (https://rustup.rs) first" >&2
    exit 1
}

if [ -d "$SRC/.git" ]; then
    echo "Updating $SRC from $REPO"
    git -C "$SRC" fetch --depth=1 origin main
    git -C "$SRC" reset --hard origin/main
else
    echo "Cloning $REPO into $SRC"
    git clone --depth=1 "$REPO" "$SRC"
fi

echo "Building (release)…"
cargo build --release --manifest-path "$SRC/Cargo.toml"

install -Dm755 "$SRC/target/release/vera20k" "$PREFIX/bin/vera20k"
echo "Installed: $PREFIX/bin/vera20k"
echo "In Reunion Lite: 设置 → 引擎 → VERA20K，可执行文件留空或填上面的路径。"
