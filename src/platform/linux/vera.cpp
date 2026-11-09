#include "vera.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rlwin {
namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Slash(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    return p;
}

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool ReadFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFile(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

std::string DataHome() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return xdg;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.local/share" : std::string(".");
}

// Copies aimd.ini from the AI pack that ships the mission's scenario.
bool InstallAiRoot(const rl::LaunchPlan& plan, std::string& error) {
    const fs::path aiDir = fs::path(plan.gameRoot) / "Mod&AI";
    std::error_code ec;
    if (!fs::is_directory(aiDir, ec)) {
        error = "找不到 Mod&AI 目录，无法选择 AI 根";
        return false;
    }
    const std::string scenario = ToLower(plan.scenarioName);
    std::string fallback;
    for (const auto& entry : fs::directory_iterator(aiDir, ec)) {
        if (!entry.is_directory()) continue;
        const fs::path aimd = entry.path() / "aimd.ini";
        if (!fs::is_regular_file(aimd, ec)) continue;
        if (fallback.empty()) fallback = aimd.string();
        for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file() && ToLower(f.path().filename().string()) == scenario) {
                fallback = aimd.string();
                goto found;
            }
        }
    }
found:
    if (fallback.empty()) {
        error = "找不到可用的 aimd.ini（Mod&AI 里没有 AI 包），VERA20K 无法载入任务";
        return false;
    }
    const fs::path dest = fs::path(Slash(plan.runDir)) / "aimd.ini";
    fs::copy_file(fallback, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "复制 aimd.ini 失败：" + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::string VeraIniPath() {
    return DataHome() + "/VERA20k/VERA.INI";
}

bool PrepareVera20k(const rl::LaunchPlan& plan, const rl::LaunchOptions& options,
                    std::string& error) {
    // Reunion ships an already merged asset root; VERA still insists on the
    // original base-game movie archive.  When it is absent, drop in a valid
    // empty MIX so the archive mount succeeds (the mission does not need it).
    const fs::path movies = fs::path(Slash(plan.runDir)) / "movies03.mix";
    std::error_code mixEc;
    if (!fs::exists(movies, mixEc))
        WriteFile(movies, std::string(10, '\0'));

    // 1. spawn.ini / spawn-vera.ini.
    //    gamemd reads the ANSI codepage; VERA reads UTF-8 and (in our fork)
    //    honours RA2_SPAWN_INI.  Our patched pacer maps the slider in the
    //    retail direction (6 = uncapped, 5 = 60 Hz), so the launcher's 0..6
    //    value is written through unchanged.
    const int veraSpeed = std::max(0, std::min(6, options.gameSpeed));
    std::string veraIniText = plan.spawnIni;
    {
        const std::string needle = "GameSpeed=";
        std::size_t pos = 0;
        while ((pos = veraIniText.find(needle, pos)) != std::string::npos) {
            std::size_t start = pos + needle.size();
            std::size_t end = veraIniText.find_first_of("\r\n", start);
            if (end == std::string::npos) end = veraIniText.size();
            veraIniText.replace(start, end - start, std::to_string(veraSpeed));
            pos = start;
        }
    }
    if (!WriteFile(fs::path(Slash(plan.runDir)) / "spawn.ini", plan.spawnIni) ||
        !WriteFile(fs::path(Slash(plan.runDir)) / "spawn-vera.ini", veraIniText)) {
        error = "写入 UTF-8 spawn 文件失败";
        return false;
    }

    // 2. aimd.ini next to the run (VERA reads it from the asset root).  Packs
    //    without a Mod&AI directory simply run without custom AI.
    std::string aiError;
    InstallAiRoot(plan, aiError);

    // 3. config.toml
    const std::string settings = DataHome() + "/reunion-lite/vera20k-settings";
    std::ostringstream toml;
    toml << "[paths]\n"
         << "ra2_dir = \"" << Slash(plan.runDir) << "\"\n"
         << "settings_dir = \"" << settings << "\"\n\n"
         << "[ini]\n"
         << "profile = \"yr\"\n\n"
         << "[gameplay]\n"
         << "input_delay_ticks = 15\n"
         << "pause_on_focus_loss = false\n";
    if (!WriteFile(fs::path(Slash(plan.runDir)) / "config.toml", toml.str())) {
        error = "写入 config.toml 失败";
        return false;
    }

    // 4. VERA.INI game speed (the engine owns the campaign pace there).
    std::string ini;
    if (ReadFile(VeraIniPath(), ini)) {
        std::istringstream in(ini);
        std::ostringstream out;
        std::string line;
        std::string section;
        while (std::getline(in, line)) {
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r"));
            if (!trimmed.empty() && trimmed.front() == '[' &&
                trimmed.back() == ']') {
                section = trimmed.substr(1, trimmed.size() - 2);
                out << line << "\n";
                continue;
            }
            if (section == "Options" && trimmed.rfind("GameSpeed=", 0) == 0) {
                out << "GameSpeed=" << (6 - (options.gameSpeed < 0 ? 0 : options.gameSpeed > 6 ? 6 : options.gameSpeed)) << "\n";
                continue;
            }
            out << line << "\n";
        }
        WriteFile(VeraIniPath(), out.str());
    }
    return true;
}

} // namespace rlwin
