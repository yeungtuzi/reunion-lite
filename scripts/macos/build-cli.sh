#!/bin/bash
# Builds the headless rlctl driver for the unified tree.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/macos"
CXX="${CXX:-clang++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra \
  -I "$ROOT/src" -I "$ROOT/src/core" -I "$ROOT/src/platform/mac" \
  -I "$ROOT/src/platform/mac/api/include" \
  "$ROOT/macos/rlctl/main.cpp" \
  "$ROOT"/src/core/*.cpp \
  "$ROOT"/src/platform/mac/*.cpp \
  "$ROOT"/src/platform/mac/api/*.cpp \
  -liconv -o "$ROOT/build/macos/rlctl"
echo "built: $ROOT/build/macos/rlctl"
