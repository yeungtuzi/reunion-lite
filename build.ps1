# Reunion Lite build script (Windows x64, no third-party libraries)
# Requirements: MinGW-w64 g++ / windres / strip in PATH or in .\tools\mingw\mingw64\bin
param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'build')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$mingwCandidates = @(
    (Join-Path $root 'tools\mingw\mingw64\bin'),
    'C:\msys64\mingw64\bin',
    'C:\mingw64\bin'
)
$mingw = $mingwCandidates | Where-Object { Test-Path (Join-Path $_ 'g++.exe') } | Select-Object -First 1
if (-not $mingw) {
    $g = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($g) { $mingw = Split-Path $g.Source }
}
if (-not $mingw) {
    throw 'MinGW-w64 not found. Put it in .\tools\mingw\mingw64 or add g++.exe to PATH.'
}

New-Item -ItemType Directory -Force $OutDir | Out-Null

$sources = @(
    'src\core\ini.cpp',
    'src\core\campaign.cpp',
    'src\core\fs_generic.cpp',
    'src\core\launch.cpp',
    'src\platform\win\encoding.cpp',
    'src\platform\win\fs_win.cpp',
    'src\platform\win\winutil.cpp',
    'src\platform\win\runprep.cpp',
    'src\platform\win\app.cpp'
) | ForEach-Object { Join-Path $root $_ }

$res = Join-Path $OutDir 'app.res.o'
& (Join-Path $mingw 'windres.exe') -I (Join-Path $root 'src\platform\win') `
    (Join-Path $root 'src\platform\win\app.rc') -O coff -o $res
if ($LASTEXITCODE -ne 0) { throw 'windres failed' }

$exe = Join-Path $OutDir 'ReunionLite.exe'
& (Join-Path $mingw 'g++.exe') -std=c++17 -O2 -Wall -Wextra -municode -mwindows `
    -I (Join-Path $root 'src') -I (Join-Path $root 'src\core') -I (Join-Path $root 'src\platform\win') `
    $sources $res -o $exe `
    -static -static-libgcc -static-libstdc++ `
    -lcomctl32 -lshlwapi -lole32 -luxtheme
if ($LASTEXITCODE -ne 0) { throw 'compile failed' }

& (Join-Path $mingw 'strip.exe') $exe
Write-Host "built: $exe"
