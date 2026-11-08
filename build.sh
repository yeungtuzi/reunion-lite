#!/bin/sh
# Build Reunion Lite for the host platform from this single source tree.
#   Linux  -> CMake + Qt6 (build/)
#   macOS  -> SwiftPM (Package.swift, SwiftUI app)
#   Windows (Git Bash/MSYS) -> build.ps1 (MinGW Win32 app)
set -eu
cd "$(dirname "$0")"
case "$(uname -s)" in
    Darwin)
        exec swift build -c release
        ;;
    MINGW*|MSYS*|CYGWIN*)
        exec powershell -NoProfile -ExecutionPolicy Bypass -File ./build.ps1
        ;;
    *)
        cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
        exec cmake --build build -j"$(nproc 2>/dev/null || echo 4)"
        ;;
esac
