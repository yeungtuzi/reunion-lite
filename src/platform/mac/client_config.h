#pragma once

#include <string>

#include "core/launch.h"

namespace rlmac {

// Defaults used by the Windows client (Reunion.exe) and by ReunionLite.exe.
// Both store the last selected campaign options on disk; we read them so that
// launching from the macOS launcher behaves like launching from the client.
//
//   ReunionLite.ini        [Options]  Difficulty/Credits/GameSpeed/...
//   Client/CampaignSetting.ini [GameOptions]  cmbCredits/cmbGameSpeed/...
//
// Values found in ReunionLite.ini win because it stores the actual amounts
// (e.g. Credits=100000) instead of combo-box indexes.
struct ClientDefaults {
    rl::LaunchOptions options;
    std::string lastMission;  // "<sourceFile>|<section>", may be empty
    bool found = false;       // at least one of the files existed
};

ClientDefaults LoadClientDefaults(const std::string& gameRootUtf8);

} // namespace rlmac
