#include "platform/mac/client_config.h"

#include "core/campaign.h"
#include "core/encoding.h"
#include "core/ini.h"
#include "core/platform_path.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace rlmac {
namespace {

bool ReadAll(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool ReadIni(const std::string& path, rl::IniDocument& ini) {
    std::vector<std::uint8_t> bytes;
    if (!ReadAll(path, bytes)) return false;
    ini.Parse(rl::DecodeAnsiToUtf8(bytes));
    return true;
}

bool BoolValue(const std::string& value, bool fallback) {
    if (value.empty()) return fallback;
    return rl::IniEquals(value, "true") || rl::IniEquals(value, "yes") ||
           rl::IniEquals(value, "1");
}

// CampaignSetting.ini stores combo-box indexes for these two.
int CreditsFromIndex(int index) {
    switch (index) {
        case 0: return 10000;
        case 2: return 50000;
        case 3: return 100000;
        default: return 10000;
    }
}

} // namespace

ClientDefaults LoadClientDefaults(const std::string& gameRootUtf8) {
    ClientDefaults defaults;
    rl::LaunchOptions& options = defaults.options;
    const std::string root = rl::MapPlatformPath(gameRootUtf8);

    rl::IniDocument lite;
    if (ReadIni(root + "/ReunionLite.ini", lite)) {
        defaults.found = true;
        options.difficulty = lite.GetInt("Options", "Difficulty", options.difficulty);
        options.credits = lite.GetInt("Options", "Credits", options.credits);
        options.gameSpeed = lite.GetInt("Options", "GameSpeed", options.gameSpeed);
        options.autosave = lite.GetInt("Options", "Autosave", options.autosave);
        options.musicVolume = lite.GetInt("Options", "MusicVolume", options.musicVolume);
        options.playMovies =
            BoolValue(lite.Get("Options", "PlayMovies", ""), options.playMovies);
        options.satellite = BoolValue(lite.Get("Options", "Satellite", ""), options.satellite);
        options.ares = BoolValue(lite.Get("Options", "Ares", ""), options.ares);
        options.phobos = BoolValue(lite.Get("Options", "Phobos", ""), options.phobos);
        options.terrain = BoolValue(lite.Get("Options", "Terrain", ""), options.terrain);
        options.autoRepair =
            BoolValue(lite.Get("Options", "AutoRepair", ""), options.autoRepair);
        options.corr = BoolValue(lite.Get("Options", "Corr", ""), options.corr);
        options.reverse = BoolValue(lite.Get("Options", "Reverse", ""), options.reverse);
        defaults.lastMission = lite.Get("UI", "LastMission", "");
    }

    rl::IniDocument setting;
    if (ReadIni(root + "/Client/CampaignSetting.ini", setting)) {
        defaults.found = true;
        if (defaults.lastMission.empty()) {
            const std::string map = setting.Get("Settings", "Map", "");
            if (!map.empty()) defaults.lastMission = map;
        }
        // Only fill what ReunionLite.ini did not provide.
        if (!lite.HasSection("Options")) {
            options.gameSpeed = setting.GetInt("GameOptions", "cmbGameSpeed", options.gameSpeed);
            options.autosave = setting.GetInt("GameOptions", "cmbAutosave", options.autosave);
            options.credits =
                CreditsFromIndex(setting.GetInt("GameOptions", "cmbCredits", 1));
            options.satellite =
                BoolValue(setting.Get("GameOptions", "chkSatellite", ""), options.satellite);
            options.autoRepair =
                BoolValue(setting.Get("GameOptions", "chkAutoRepair", ""), options.autoRepair);
            options.corr = BoolValue(setting.Get("GameOptions", "chkCorr", ""), options.corr);
            options.ares = BoolValue(setting.Get("GameOptions", "chkAres", ""), options.ares);
            options.phobos = BoolValue(setting.Get("GameOptions", "chkPhobos", ""), options.phobos);
            options.terrain = BoolValue(setting.Get("GameOptions", "chkTerrain", ""), options.terrain);
            options.reverse = BoolValue(setting.Get("GameOptions", "chkReverse", ""), options.reverse);
        }
    }
    return defaults;
}

} // namespace rlmac
