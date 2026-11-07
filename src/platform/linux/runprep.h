#pragma once

#include <string>
#include <vector>

#include "core/launch.h"

namespace rlwin {

// Creates/refreshes Run\ with base files, mod overlay, mission files and the
// patched map, writes spawn.ini, and prepares the per-pack save directory.
bool PrepareRun(const rl::LaunchPlan& plan, const rl::LaunchOptions& options,
                std::string& error, std::vector<std::string>& warnings);

// Copies Run\RA2MD.ini back to the game root and moves new saves into the
// per-pack folder.  Call after the game process exits.
void SyncSettingsBack(const std::string& gameRootUtf8);

} // namespace rlwin
