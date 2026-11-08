#include "client_defaults.h"

#include <fstream>
#include <sstream>

#include "ini.h"

namespace rl {
namespace {

const int kCreditsValues[] = {-1, 100000, 50000, 30000, 20000, 15000, 10000, 7500, 5000};

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

bool LoadClientDefaults(const std::string& gameRootUtf8, LaunchOptions& options) {
    const std::string text = ReadFile(gameRootUtf8 + "/Client/CampaignSetting.ini");
    if (text.empty()) return false;
    IniDocument ini;
    ini.Parse(text);

    int difficulty = ini.GetInt("Settings", "DifficultySelector", options.difficulty);
    if (difficulty < 0 || difficulty > 2) difficulty = 1;
    options.difficulty = difficulty;

    const int creditsIndex = ini.GetInt("GameOptions", "cmbCredits", -1);
    if (creditsIndex >= 0 &&
        creditsIndex < static_cast<int>(sizeof(kCreditsValues) / sizeof(int)))
        options.credits = kCreditsValues[creditsIndex];

    int speed = ini.GetInt("GameOptions", "cmbGameSpeed", options.gameSpeed);
    if (speed < 0 || speed > 6) speed = 2;
    options.gameSpeed = speed;

    int autosave = ini.GetInt("GameOptions", "cmbAutosave", options.autosave);
    if (autosave < 0 || autosave > 3) autosave = 0;
    options.autosave = autosave;

    options.satellite = ini.GetBool("GameOptions", "chkSatellite", options.satellite);
    options.autoRepair = ini.GetBool("GameOptions", "chkAutoRepair", options.autoRepair);
    options.corr = ini.GetBool("GameOptions", "chkCorr", options.corr);
    options.ares = ini.GetBool("GameOptions", "chkAres", options.ares);
    options.phobos = ini.GetBool("GameOptions", "chkPhobos", options.phobos);
    options.terrain = ini.GetBool("GameOptions", "chkTerrain", options.terrain);
    options.reverse = ini.GetBool("GameOptions", "chkReverse", options.reverse);
    return true;
}

} // namespace rl
