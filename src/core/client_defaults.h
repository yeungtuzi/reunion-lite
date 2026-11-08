#pragma once

#include <string>

#include "launch.h"

namespace rl {

// Reads Client/CampaignSetting.ini — the Reunion client's own default options
// (difficulty, credits index, game speed, autosave, satellite, Ares/Phobos...).
// Returns false when the file is missing; options are left untouched then.
bool LoadClientDefaults(const std::string& gameRootUtf8, LaunchOptions& options);

} // namespace rl
