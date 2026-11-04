#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "core/launch.h"

namespace rlwin {

// Sets up Run\ (base files + selected mod + mission + patched map + spawn.ini).
// Everything created here is recorded in Run\.rl_overlay and removed on the
// next launch, so switching mission packs never leaves stale overrides behind.
bool PrepareRun(const rl::LaunchPlan& plan, const rl::LaunchOptions& options, std::string& error,
                std::vector<std::string>& warnings);

// Starts Syringe.exe (Ares) or gamemd-spawn.exe in Run\.
bool StartGame(const rl::LaunchPlan& plan, HANDLE& process, std::string& error);

// Copies Run\RA2MD.ini back to the game root after the game exits.
void SyncSettingsBack(const std::string& gameRootUtf8);

} // namespace rlwin

