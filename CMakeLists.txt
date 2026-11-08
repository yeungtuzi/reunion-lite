cmake_minimum_required(VERSION 3.16)
project(ReunionLite VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One source tree, three UIs.  The shared core is identical everywhere; the
# platform layer and the UI are picked from the host detected here, so the same
# checkout builds natively on every supported system:
#
#   Windows : Win32 launcher  (CMake + MinGW, or build.ps1)
#   macOS   : SwiftUI launcher (SwiftPM, Package.swift) - CMake builds the
#             shared core library and points at `swift build`
#   Linux   : Qt6 launcher    (CMake + Qt6 Widgets)
set(REUNION_CORE
    src/core/ini.cpp
    src/core/campaign.cpp
    src/core/fs_generic.cpp
    src/core/launch.cpp
    src/core/client_defaults.cpp)

if(WIN32)
    # ---- Windows: native Win32 launcher -----------------------------------
    enable_language(RC)
    add_executable(ReunionLite WIN32
        ${REUNION_CORE}
        src/platform/win/encoding.cpp
        src/platform/win/path_map.cpp
        src/platform/win/fs_win.cpp
        src/platform/win/winutil.cpp
        src/platform/win/runprep.cpp
        src/platform/win/app.cpp
        src/platform/win/app.rc)
    target_include_directories(ReunionLite PRIVATE src src/core src/platform/win)
    target_compile_definitions(ReunionLite PRIVATE UNICODE _UNICODE)
    target_link_libraries(ReunionLite PRIVATE comctl32 shlwapi ole32 uxtheme)
    if(MINGW)
        target_link_options(ReunionLite PRIVATE -static -static-libgcc -static-libstdc++)
        target_compile_options(ReunionLite PRIVATE -municode -mwindows)
    endif()

elseif(APPLE)
    # ---- macOS: the UI is SwiftUI (Package.swift).  CMake builds the shared
    # C++ core so `rlctl`/tests and the Swift target compile against the same
    # sources; the app itself is `swift build -c release`.
    add_library(reunion-core STATIC
        ${REUNION_CORE}
        src/platform/mac/encoding.cpp
        src/platform/mac/path_map.cpp
        src/platform/mac/client_config.cpp
        src/platform/mac/runprep.cpp
        src/platform/mac/launch.cpp
        src/platform/mac/api/rl_api.cpp)
    target_include_directories(reunion-core PUBLIC src src/core src/platform/mac
                                             src/platform/mac/api/include)
    target_link_libraries(reunion-core PUBLIC iconv)
    message(STATUS "macOS UI: run `swift build -c release` (Package.swift) or scripts/macos/build-app.sh")

else()
    # ---- Linux (and other Unix): Qt6 Widgets launcher ---------------------
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_executable(reunion-lite-qt
        ${REUNION_CORE}
        src/platform/linux/encoding.cpp
        src/platform/linux/path_map.cpp
        src/platform/linux/runprep.cpp
        src/platform/linux/vera.cpp
        src/platform/linux/app_qt.cpp)
    target_include_directories(reunion-lite-qt PRIVATE src src/core src/platform/linux)
    target_link_libraries(reunion-lite-qt PRIVATE Qt6::Widgets)
endif()
