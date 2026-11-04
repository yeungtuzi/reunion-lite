// Reunion Lite - native Win32 front-end.
// Standard controls only: no custom painting, no game loop, no networking.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/campaign.h"
#include "core/ini.h"
#include "core/launch.h"
#include "platform/win/runprep.h"
#include "platform/win/winutil.h"

using namespace rlwin;

namespace {

constexpr int ID_SEARCH = 100;
constexpr int ID_PACK = 101;
constexpr int ID_LIST = 102;
constexpr int ID_DESC = 103;
constexpr int ID_RESCAN = 104;
constexpr int ID_LAUNCH = 105;
constexpr int ID_MOD = 106;
constexpr int ID_DIFF = 107;
constexpr int ID_CREDITS = 108;
constexpr int ID_SPEED = 109;
constexpr int ID_AUTOSAVE = 110;
constexpr int ID_CHK_SAT = 111;
constexpr int ID_CHK_ARES = 112;
constexpr int ID_CHK_PHOBOS = 113;
constexpr int ID_CHK_TERRAIN = 114;
constexpr int ID_CHK_REPAIR = 115;
constexpr int ID_CHK_CORR = 116;
constexpr int ID_CHK_REVERSE = 117;
constexpr int ID_MUSIC = 118;
constexpr int ID_CHK_MOVIES = 119;
constexpr int WM_APP_GAME_EXIT = WM_APP + 1;

const wchar_t* kClassName = L"ReunionLiteWindow";

const int kCreditsValues[] = {-1, 100000, 50000, 30000, 20000, 15000, 10000, 7500, 5000};
const wchar_t* kCreditsLabels[] = {L"不修改", L"100000", L"50000", L"30000", L"20000",
                                   L"15000",  L"10000",  L"7500",  L"5000"};
const wchar_t* kDifficultyLabels[] = {L"简单", L"中等", L"困难"};
const wchar_t* kSpeedLabels[] = {L"6 最快", L"5 很快", L"4 快", L"3 中等",
                                 L"2 慢",   L"1 很慢", L"0 最慢"};
const wchar_t* kAutosaveLabels[] = {L"不自动", L"每 5 分钟", L"每 10 分钟", L"每 20 分钟"};
const int kMusicValues[] = {-1, 0, 30, 50, 70, 100};
const wchar_t* kMusicLabels[] = {L"保持游戏设置", L"关闭", L"30%", L"50%", L"70%", L"100%"};

struct Config {
    int x = -1, y = -1, w = 0, h = 0;
    bool maximized = false;
    std::string search;
    std::string packId;
    std::string lastMission;
    std::string gameRoot;
    rl::LaunchOptions options;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND hSearch = nullptr;
    HWND hPack = nullptr;
    HWND hList = nullptr;
    HWND hDesc = nullptr;
    HWND hStatus = nullptr;
    HWND hRescan = nullptr;
    HWND hLaunch = nullptr;
    HWND hGroup = nullptr;
    HWND hMod = nullptr;
    HWND hDiff = nullptr;
    HWND hCredits = nullptr;
    HWND hSpeed = nullptr;
    HWND hAutosave = nullptr;
    HWND hMusic = nullptr;
    HWND hLblMusic = nullptr;
    HWND hChkSat = nullptr;
    HWND hChkAres = nullptr;
    HWND hChkPhobos = nullptr;
    HWND hChkTerrain = nullptr;
    HWND hChkRepair = nullptr;
    HWND hChkCorr = nullptr;
    HWND hChkReverse = nullptr;
    HWND hChkMovies = nullptr;
    HWND hLblMod = nullptr;
    HWND hLblDiff = nullptr;
    HWND hLblCredits = nullptr;
    HWND hLblSpeed = nullptr;
    HWND hLblAutosave = nullptr;

    HFONT font = nullptr;
    int dpi = 96;
    std::wstring title;

    std::string root;
    std::string cachePath;
    std::string configPath;
    rl::Fingerprint fingerprint;
    rl::Campaign campaign;
    std::vector<rl::ModInfo> registry;
    std::vector<int> order;      // mission indices sorted for display
    std::vector<int> filtered;   // indices into order? no: mission indices
    std::vector<std::string> packIds;
    std::vector<std::wstring> packTitles;
    std::vector<rl::ModInfo> currentMods;
    int packFilter = -1;         // index into packIds
    std::string search;
    int selectedMission = -1;
    int selectedRow = -1;
    std::wstring cell;

    Config config;
    bool cacheHit = false;
    double fingerprintMs = 0;
    double loadMs = 0;
    double scanMs = 0;

    std::string startupFilter;
    std::string startupLaunch;
    std::string forcedRoot;

    bool launching = false;
    bool gameRunning = false;
    HANDLE gameProcess = nullptr;
    DWORD gameStartTick = 0;
    int gameMission = -1;
    bool gameUsedAres = false;

    std::atomic<bool> alive{true};
    std::atomic<bool> sawGameWindow{false};
};

App g;

int S(int v) { return MulDiv(v, g.dpi, 96); }
rl::LaunchOptions GatherOptions();

std::string LowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool ContainsCI(const std::string& haystack, const std::string& lowerNeedle) {
    if (lowerNeedle.empty()) return true;
    const std::size_t n = lowerNeedle.size();
    if (haystack.size() < n) return false;
    for (std::size_t i = 0; i + n <= haystack.size(); ++i) {
        std::size_t j = 0;
        for (; j < n; ++j) {
            char c = haystack[i + j];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lowerNeedle[j]) break;
        }
        if (j == n) return true;
    }
    return false;
}

std::wstring SideLabel(const std::string& side) {
    if (side == "Allied") return L"盟军";
    if (side == "Soviet") return L"苏军";
    if (side == "Yuri") return L"尤里";
    if (side.empty()) return L"—";
    return Wide(side);
}

void SetFont(HWND h) {
    if (h && g.font) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
}

void AddComboString(HWND combo, const wchar_t* text, LPARAM data) {
    int index = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(text)));
    if (index >= 0) SendMessageW(combo, CB_SETITEMDATA, index, data);
}

LPARAM ComboData(HWND combo) {
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel < 0) return -1;
    return SendMessageW(combo, CB_GETITEMDATA, sel, 0);
}

void SelectComboData(HWND combo, LPARAM data) {
    int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == data) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            return;
        }
    }
}

std::string GetTextUtf8(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(h, text.data(), len + 1);
    text.resize(static_cast<std::size_t>(len));
    return Utf8(text);
}

bool IsChecked(HWND h) {
    return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetChecked(HWND h, bool value) {
    SendMessageW(h, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

void AppendLog(const std::string& text) {
    if (g.root.empty()) return;
    static bool first = true;
    std::ofstream f(JoinPath(g.root, "Client/ReunionLite.log"),
                    first ? std::ios::trunc : std::ios::app);
    first = false;
    if (f) f << text << "\r\n";
}

void ShowError(const std::wstring& title, const std::string& message) {
    MessageBoxW(g.hwnd, Wide(message).c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

std::string ParentDirectory(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos || pos == 0) return {};
    return path.substr(0, pos);
}

std::string CurrentDirectoryUtf8() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (len == 0) return {};
        if (len < buffer.size()) {
            buffer.resize(len);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return Utf8(buffer);
}

bool LooksLikeGameRoot(const std::string& dir) {
    return !dir.empty() && DirExists(JoinPath(dir, "Maps/CP")) &&
           (DirExists(JoinPath(dir, "Mod&AI")) || FileExists(JoinPath(dir, "gamemd-spawn.exe")));
}

// The launcher may be started from a shortcut, from a different working
// directory, or from a sub-folder.  Find the actual game root instead of
// relying on the process working directory.
std::string DetectGameRoot(const std::string& exeDir, const std::string& hint) {
    std::vector<std::string> candidates;
    if (!hint.empty()) candidates.push_back(hint);
    candidates.push_back(exeDir);
    const std::string cwd = CurrentDirectoryUtf8();
    if (!cwd.empty()) candidates.push_back(cwd);
    const std::string exeParent = ParentDirectory(exeDir);
    if (!exeParent.empty()) candidates.push_back(exeParent);
    const std::string cwdParent = ParentDirectory(cwd);
    if (!cwdParent.empty()) candidates.push_back(cwdParent);
    const std::string exeGrand = ParentDirectory(exeParent);
    if (!exeGrand.empty()) candidates.push_back(exeGrand);

    for (const auto& candidate : candidates)
        if (LooksLikeGameRoot(candidate)) return candidate;
    return {};
}

std::string DefaultWindowTitle(const std::string& root) {
    std::string text = ReadFileTextAuto(JoinPath(root, "Resources/ClientDefinitions.ini"));
    if (!text.empty()) {
        rl::IniDocument ini;
        ini.Parse(text);
        std::string title = ini.Get("Settings", "WindowTitle", "");
        if (!title.empty()) return title;
    }
    return "红色警戒2 重聚未来";
}

// ------------------------------------------------------------------- config

void LoadLegacyDefaults(Config& cfg) {
    std::string text = ReadFileTextAuto(JoinPath(g.root, "Client/CampaignSetting.ini"));
    if (text.empty()) return;
    rl::IniDocument ini;
    ini.Parse(text);
    cfg.options.difficulty = ini.GetInt("Settings", "DifficultySelector", 1);
    if (cfg.options.difficulty < 0 || cfg.options.difficulty > 2) cfg.options.difficulty = 1;

    int creditsIndex = ini.GetInt("GameOptions", "cmbCredits", -1);
    if (creditsIndex >= 0 && creditsIndex < static_cast<int>(sizeof(kCreditsValues) / sizeof(int)))
        cfg.options.credits = kCreditsValues[creditsIndex];

    cfg.options.gameSpeed = ini.GetInt("GameOptions", "cmbGameSpeed", 2);
    if (cfg.options.gameSpeed < 0 || cfg.options.gameSpeed > 6) cfg.options.gameSpeed = 2;

    cfg.options.autosave = ini.GetInt("GameOptions", "cmbAutosave", 0);
    if (cfg.options.autosave < 0 || cfg.options.autosave > 3) cfg.options.autosave = 0;

    cfg.options.satellite = ini.GetBool("GameOptions", "chkSatellite", false);
    cfg.options.autoRepair = ini.GetBool("GameOptions", "chkAutoRepair", true);
    cfg.options.corr = ini.GetBool("GameOptions", "chkCorr", true);
    cfg.options.ares = ini.GetBool("GameOptions", "chkAres", false);
    cfg.options.phobos = ini.GetBool("GameOptions", "chkPhobos", false);
    cfg.options.terrain = ini.GetBool("GameOptions", "chkTerrain", false);
    cfg.options.reverse = ini.GetBool("GameOptions", "chkReverse", false);
}

void LoadConfig() {
    g.config = Config{};
    LoadLegacyDefaults(g.config);
    std::string text = ReadFileTextAuto(g.configPath);
    if (text.empty()) {
        // Migrate the ini written by earlier builds (Client\ReunionLite.ini).
        std::string legacy = JoinPath(g.root, "Client/ReunionLite.ini");
        if (legacy != g.configPath) text = ReadFileTextAuto(legacy);
    }
    if (text.empty()) return;
    rl::IniDocument ini;
    ini.Parse(text);
    g.config.gameRoot = ini.Get("Game", "Root", "");
    g.config.x = ini.GetInt("Window", "X", -1);
    g.config.y = ini.GetInt("Window", "Y", -1);
    g.config.w = ini.GetInt("Window", "W", 0);
    g.config.h = ini.GetInt("Window", "H", 0);
    g.config.maximized = ini.GetBool("Window", "Maximized", false);
    g.config.search = ini.Get("UI", "Search", "");
    g.config.packId = ini.Get("UI", "PackId", "");
    g.config.lastMission = ini.Get("UI", "LastMission", "");
    auto& o = g.config.options;
    o.difficulty = ini.GetInt("Options", "Difficulty", o.difficulty);
    o.credits = ini.GetInt("Options", "Credits", o.credits);
    o.gameSpeed = ini.GetInt("Options", "GameSpeed", o.gameSpeed);
    o.autosave = ini.GetInt("Options", "Autosave", o.autosave);
    o.musicVolume = ini.GetInt("Options", "MusicVolume", o.musicVolume);
    o.playMovies = ini.GetBool("Options", "PlayMovies", o.playMovies);
    o.satellite = ini.GetBool("Options", "Satellite", o.satellite);
    o.ares = ini.GetBool("Options", "Ares", o.ares);
    o.phobos = ini.GetBool("Options", "Phobos", o.phobos);
    o.terrain = ini.GetBool("Options", "Terrain", o.terrain);
    o.autoRepair = ini.GetBool("Options", "AutoRepair", o.autoRepair);
    o.corr = ini.GetBool("Options", "Corr", o.corr);
    o.reverse = ini.GetBool("Options", "Reverse", o.reverse);
    if (o.difficulty < 0 || o.difficulty > 2) o.difficulty = 1;
    if (o.gameSpeed < 0 || o.gameSpeed > 6) o.gameSpeed = 2;
    if (o.autosave < 0 || o.autosave > 3) o.autosave = 0;
}

void SaveConfig() {
    if (g.hDiff && g.hMod) {
        rl::LaunchOptions current = GatherOptions();
        g.config.options = current;
    }
    rl::IniDocument ini;
    RECT rc{};
    if (g.hwnd && GetWindowRect(g.hwnd, &rc)) {
        const int dpi = g.dpi > 0 ? g.dpi : 96;
        ini.SetInt("Window", "X", MulDiv(rc.left, 96, dpi));
        ini.SetInt("Window", "Y", MulDiv(rc.top, 96, dpi));
        ini.SetInt("Window", "W", MulDiv(rc.right - rc.left, 96, dpi));
        ini.SetInt("Window", "H", MulDiv(rc.bottom - rc.top, 96, dpi));
    }
    ini.SetBool("Window", "Maximized", IsZoomed(g.hwnd) != FALSE);
    ini.Set("Game", "Root", g.root);
    ini.Set("UI", "Search", g.search);
    if (g.packFilter >= 0 && g.packFilter < static_cast<int>(g.packIds.size()))
        ini.Set("UI", "PackId", g.packIds[g.packFilter]);
    else
        ini.Set("UI", "PackId", "");
    if (g.selectedMission >= 0) {
        const auto& m = g.campaign.missions[g.selectedMission];
        ini.Set("UI", "LastMission", m.sourceFile + "|" + m.section);
    }
    const auto& o = g.config.options;
    ini.SetInt("Options", "Difficulty", o.difficulty);
    ini.SetInt("Options", "Credits", o.credits);
    ini.SetInt("Options", "GameSpeed", o.gameSpeed);
    ini.SetInt("Options", "Autosave", o.autosave);
    ini.SetInt("Options", "MusicVolume", o.musicVolume);
    ini.SetBool("Options", "PlayMovies", o.playMovies);
    ini.SetBool("Options", "Satellite", o.satellite);
    ini.SetBool("Options", "Ares", o.ares);
    ini.SetBool("Options", "Phobos", o.phobos);
    ini.SetBool("Options", "Terrain", o.terrain);
    ini.SetBool("Options", "AutoRepair", o.autoRepair);
    ini.SetBool("Options", "Corr", o.corr);
    ini.SetBool("Options", "Reverse", o.reverse);

    std::string data = ini.Serialize();
    WriteFileBytes(g.configPath, data.data(), data.size());
}

// --------------------------------------------------------------- data / ui

void BuildOrder() {
    g.order.clear();
    g.order.reserve(g.campaign.missions.size());
    for (std::size_t i = 0; i < g.campaign.missions.size(); ++i) {
        if (g.campaign.missions[i].badReason.empty()) g.order.push_back(static_cast<int>(i));
    }
    std::sort(g.order.begin(), g.order.end(), [](int a, int b) {
        const auto& ma = g.campaign.missions[a];
        const auto& mb = g.campaign.missions[b];
        int cmp = LowerAscii(ma.packName).compare(LowerAscii(mb.packName));
        if (cmp != 0) return cmp < 0;
        if (ma.index != mb.index) return ma.index < mb.index;
        return LowerAscii(ma.name) < LowerAscii(mb.name);
    });
}

void BuildPackList() {
    g.packIds.clear();
    g.packTitles.clear();
    std::map<std::string, std::pair<std::string, int>> byId; // lower id -> title, first order idx
    for (int missionIndex : g.order) {
        const auto& m = g.campaign.missions[missionIndex];
        std::string id = m.packId.empty() ? m.packName : m.packId;
        std::string lower = LowerAscii(id);
        if (byId.find(lower) == byId.end()) byId.emplace(lower, std::make_pair(m.packName, missionIndex));
    }
    std::vector<std::pair<std::string, std::wstring>> items;
    items.reserve(byId.size());
    for (auto& kv : byId) {
        std::wstring title = Wide(kv.second.first.empty() ? std::string("未分类") : kv.second.first);
        items.emplace_back(kv.first, title);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return LowerAscii(Utf8(a.second)) < LowerAscii(Utf8(b.second));
    });
    for (auto& item : items) {
        g.packIds.push_back(item.first);
        g.packTitles.push_back(item.second);
    }
}

void RebuildPackCombo() {
    SendMessageW(g.hPack, CB_RESETCONTENT, 0, 0);
    SendMessageW(g.hPack, CB_INITSTORAGE, g.packTitles.size(),
                 g.packTitles.size() * 40);
    AddComboString(g.hPack, L"全部任务包", -1);
    for (int i = 0; i < static_cast<int>(g.packTitles.size()); ++i)
        AddComboString(g.hPack, g.packTitles[i].c_str(), i);

    std::string wantedId = LowerAscii(g.config.packId);
    if (g.packFilter >= 0 && g.packFilter < static_cast<int>(g.packIds.size()))
        wantedId = g.packIds[g.packFilter];
    int wanted = -1;
    if (!wantedId.empty()) {
        for (int i = 0; i < static_cast<int>(g.packIds.size()); ++i) {
            if (g.packIds[i] == wantedId) {
                wanted = i;
                break;
            }
        }
    }
    g.packFilter = wanted;
    SelectComboData(g.hPack, g.packFilter);
    if (SendMessageW(g.hPack, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(g.hPack, CB_SETCURSEL, 0, 0);
}

void UpdateStatus() {
    int total = static_cast<int>(g.campaign.missions.size());
    int usable = static_cast<int>(g.order.size());
    wchar_t cacheInfo[128];
    if (g.cacheHit)
        swprintf(cacheInfo, 128, L"缓存命中 %.1f ms / 指纹 %.1f ms", g.loadMs, g.fingerprintMs);
    else
        swprintf(cacheInfo, 128, L"已扫描 %.1f ms", g.scanMs);
    std::wstring text = L"显示 " + std::to_wstring(g.filtered.size()) + L" / 可启动 " +
                        std::to_wstring(usable) + L" / 总 " + std::to_wstring(total) +
                        L"（已排除 " + std::to_wstring(total - usable) + L"）   |   " +
                        cacheInfo + L"   |   搜索：" + Wide(g.search);
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void UpdateDescription() {
    if (g.selectedMission < 0) {
        SetWindowTextW(g.hDesc, L"");
        return;
    }
    const auto& m = g.campaign.missions[g.selectedMission];
    std::wstring text;
    text += Wide(m.name);
    text += L"\r\n";
    text += L"任务包：" + Wide(m.packName) + L"\r\n";
    text += L"阵营：" + SideLabel(m.side) + L"\r\n";
    text += L"难度：" + Wide(m.difficulty.empty() ? std::string("—") : m.difficulty) + L"\r\n";
    text += L"场景：" + Wide(m.scenario) + L"\r\n";
    text += L"默认模组：" + Wide(m.defaultMod) + L"\r\n";
    text += L"目录：" + Wide(m.path) + L"\r\n";
    text += L"────────────────────────────────\r\n";
    std::string description = m.description;
    if (description.empty()) description = m.name;
    for (char& c : description)
        if (c == '@') c = '\n';
    text += Wide(description);
    SetWindowTextW(g.hDesc, text.c_str());
    SendMessageW(g.hDesc, EM_SETSEL, 0, 0);
    SendMessageW(g.hDesc, EM_SCROLLCARET, 0, 0);
    EnableWindow(g.hLaunch, TRUE);
}

void RebuildModCombo() {
    SendMessageW(g.hMod, CB_RESETCONTENT, 0, 0);
    g.currentMods.clear();
    if (g.selectedMission < 0) {
        EnableWindow(g.hMod, FALSE);
        return;
    }
    const auto& mission = g.campaign.missions[g.selectedMission];
    g.currentMods = rl::ModsForMission(g.registry, mission);
    int select = 0;
    for (int i = 0; i < static_cast<int>(g.currentMods.size()); ++i) {
        const auto& mod = g.currentMods[i];
        AddComboString(g.hMod, Wide(mod.name).c_str(), i);
        if (rl::IniEquals(mod.id, mission.defaultMod)) select = i;
    }
    if (g.currentMods.empty()) {
        EnableWindow(g.hMod, FALSE);
    } else {
        EnableWindow(g.hMod, TRUE);
        SendMessageW(g.hMod, CB_SETCURSEL, select, 0);
    }
}

void ApplyForcedOptions() {
    if (g.selectedMission < 0) return;
    const auto& m = g.campaign.missions[g.selectedMission];

    // Same behaviour as the original campaign selector: the mission's own
    // requirements are forced on selection, the user may still enable them
    // manually afterwards.
    const bool ares = m.ares == 1;
    SetChecked(g.hChkAres, ares);
    EnableWindow(g.hChkAres, !ares);

    const bool phobos = m.phobos == 1 && ares;
    SetChecked(g.hChkPhobos, phobos);
    EnableWindow(g.hChkPhobos, ares);

    const bool terrain = m.tx == 1;
    SetChecked(g.hChkTerrain, terrain);
    EnableWindow(g.hChkTerrain, !terrain);
}

void SelectionChanged(int row) {
    if (row == g.selectedRow) return;
    g.selectedRow = row;
    if (row < 0 || row >= static_cast<int>(g.filtered.size())) {
        g.selectedMission = -1;
        UpdateDescription();
        EnableWindow(g.hLaunch, FALSE);
        return;
    }
    g.selectedMission = g.filtered[row];
    UpdateDescription();
    RebuildModCombo();
    ApplyForcedOptions();
    UpdateStatus();
}

void ApplyFilters() {
    g.filtered.clear();
    std::string needle = LowerAscii(g.search);
    const std::string packId =
        (g.packFilter >= 0 && g.packFilter < static_cast<int>(g.packIds.size()))
            ? g.packIds[g.packFilter]
            : std::string();
    g.filtered.reserve(g.order.size());
    for (int missionIndex : g.order) {
        const auto& m = g.campaign.missions[missionIndex];
        std::string missionPackId = LowerAscii(m.packId.empty() ? m.packName : m.packId);
        if (!packId.empty() && missionPackId != packId) continue;
        if (!needle.empty()) {
            if (!ContainsCI(m.name, needle) && !ContainsCI(m.packName, needle) &&
                !ContainsCI(m.scenario, needle) && !ContainsCI(m.difficulty, needle) &&
                !ContainsCI(m.side, needle))
                continue;
        }
        g.filtered.push_back(missionIndex);
    }

    int keep = -1;
    if (g.selectedMission >= 0) {
        auto it = std::find(g.filtered.begin(), g.filtered.end(), g.selectedMission);
        if (it != g.filtered.end()) keep = static_cast<int>(it - g.filtered.begin());
    }
    g.selectedRow = -2;
    SendMessageW(g.hList, LVM_SETITEMCOUNT, g.filtered.size(),
                 LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    InvalidateRect(g.hList, nullptr, TRUE);

    if (g.filtered.empty()) {
        ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        SelectionChanged(-1);
    } else {
        if (keep < 0) keep = 0;
        ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(g.hList, keep, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g.hList, keep, FALSE);
        SelectionChanged(keep);
    }
    UpdateStatus();
}

void ApplyOptionsToUi() {
    const auto& o = g.config.options;
    SelectComboData(g.hDiff, o.difficulty);
    SelectComboData(g.hCredits, o.credits);
    SelectComboData(g.hSpeed, o.gameSpeed);
    SelectComboData(g.hAutosave, o.autosave);
    SelectComboData(g.hMusic, o.musicVolume);
    SetChecked(g.hChkSat, o.satellite);
    SetChecked(g.hChkMovies, o.playMovies);
    SetChecked(g.hChkRepair, o.autoRepair);
    SetChecked(g.hChkCorr, o.corr);
    SetChecked(g.hChkReverse, o.reverse);
    SetChecked(g.hChkAres, o.ares);
    SetChecked(g.hChkPhobos, o.phobos);
    SetChecked(g.hChkTerrain, o.terrain);
}

rl::LaunchOptions GatherOptions() {
    rl::LaunchOptions o = g.config.options;
    o.difficulty = static_cast<int>(ComboData(g.hDiff));
    o.credits = static_cast<int>(ComboData(g.hCredits));
    o.gameSpeed = static_cast<int>(ComboData(g.hSpeed));
    o.autosave = static_cast<int>(ComboData(g.hAutosave));
    o.musicVolume = static_cast<int>(ComboData(g.hMusic));
    o.playMovies = IsChecked(g.hChkMovies);
    if (o.difficulty < 0) o.difficulty = 1;
    if (o.gameSpeed < 0) o.gameSpeed = 2;
    if (o.autosave < 0) o.autosave = 0;
    o.satellite = IsChecked(g.hChkSat);
    o.autoRepair = IsChecked(g.hChkRepair);
    o.corr = IsChecked(g.hChkCorr);
    o.reverse = IsChecked(g.hChkReverse);
    o.ares = IsChecked(g.hChkAres);
    o.phobos = IsChecked(g.hChkPhobos);
    o.terrain = IsChecked(g.hChkTerrain);
    int modSel = static_cast<int>(ComboData(g.hMod));
    if (modSel >= 0 && modSel < static_cast<int>(g.currentMods.size()))
        o.modId = g.currentMods[modSel].id;
    return o;
}

void MarkFailed(int missionIndex, const std::string& reason) {
    if (missionIndex < 0 || missionIndex >= static_cast<int>(g.campaign.missions.size())) return;
    g.campaign.missions[missionIndex].badReason = "启动失败：" + reason;
    if (!g.cachePath.empty()) rl::SaveCampaignCache(g.cachePath, g.fingerprint, g.campaign);
    BuildOrder();
    BuildPackList();
    RebuildPackCombo();
    ApplyFilters();
}

void FailLaunch(int missionIndex, const std::string& reason) {
    AppendLog("FailLaunch mission=" + std::to_string(missionIndex) + " reason=" + reason);
    std::string name = "该地图";
    if (missionIndex >= 0) name = g.campaign.missions[missionIndex].name;
    ShowError(L"无法启动任务", name + "\r\n\r\n" + reason + "\r\n\r\n此地图已从列表中排除。");
    MarkFailed(missionIndex, reason);
}

// --------------------------------------------------------------- launch flow

struct GameExitInfo {
    DWORD code = 0;
    DWORD elapsed = 0;
};

void OnLaunch() {
    AppendLog("OnLaunch launching=" + std::to_string(g.launching) +
              " running=" + std::to_string(g.gameRunning) +
              " selected=" + std::to_string(g.selectedMission));
    if (g.launching || g.gameRunning) return;
    if (g.selectedMission < 0) return;
    const int missionIndex = g.selectedMission;
    const rl::Mission& mission = g.campaign.missions[missionIndex];

    rl::LaunchOptions options = GatherOptions();
    rl::LaunchPlan plan;
    std::string error;
    if (!rl::BuildLaunchPlan(g.root, mission, options, plan, &g.campaign.missions)) {
        FailLaunch(missionIndex, plan.error.empty() ? "无法建立启动计划" : plan.error);
        return;
    }

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::vector<std::string> warnings;
    bool prepared = rlwin::PrepareRun(plan, options, error, warnings);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    if (!prepared) {
        FailLaunch(missionIndex, error);
        return;
    }

    HANDLE process = nullptr;
    if (!rlwin::StartGame(plan, process, error)) {
        FailLaunch(missionIndex, error);
        return;
    }

    g.config.options = options;
    g.sawGameWindow = false;
    g.gameProcess = process;
    g.gameStartTick = GetTickCount();
    g.gameMission = missionIndex;
    g.gameUsedAres = plan.useAres;
    g.launching = true;
    g.gameRunning = true;
    EnableWindow(g.hLaunch, FALSE);
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"游戏运行中…"));
    ShowWindow(g.hwnd, SW_MINIMIZE);

    HANDLE handle = process;
    HWND hwnd = g.hwnd;
    const DWORD startTick = g.gameStartTick;
    const DWORD pid = GetProcessId(process);
    const bool pollWindow = !plan.useAres;
    std::thread([handle, hwnd, startTick, pid, pollWindow]() {
        HWND gameWindow = nullptr;
        int missingCount = 0;
        bool windowClosed = false;
        for (;;) {
            DWORD wait = WaitForSingleObject(handle, 500);
            if (wait == WAIT_OBJECT_0) break;
            DWORD elapsed = GetTickCount() - startTick;
            if (pollWindow && elapsed > 1000) {
                struct Finder {
                    DWORD pid;
                    HWND found;
                } finder{pid, nullptr};
                EnumWindows(
                    [](HWND child, LPARAM param) -> BOOL {
                        auto* data = reinterpret_cast<Finder*>(param);
                        DWORD childPid = 0;
                        GetWindowThreadProcessId(child, &childPid);
                        if (childPid != data->pid) return TRUE;
                        if (!IsWindowVisible(child)) return TRUE;
                        if (GetWindowTextLengthW(child) == 0) return TRUE;
                        data->found = child;
                        return FALSE;
                    },
                    reinterpret_cast<LPARAM>(&finder));
                if (finder.found) {
                    gameWindow = finder.found;
                    g.sawGameWindow.store(true);
                    missingCount = 0;
                } else if (gameWindow) {
                    if (++missingCount >= 6) {
                        windowClosed = true;
                        break;
                    }
                }
            }
            if (elapsed > 6u * 60u * 60u * 1000u) break;
        }
        DWORD code = 0;
        GetExitCodeProcess(handle, &code);
        // Some YR builds keep a hidden process alive after the game window is
        // gone.  Treat "window closed" as a normal exit.
        if (windowClosed || code == STILL_ACTIVE) code = 0;
        DWORD elapsed = GetTickCount() - startTick;
        rlwin::SyncSettingsBack(g.root);
        PostMessageW(hwnd, WM_APP_GAME_EXIT, static_cast<WPARAM>(code),
                     static_cast<LPARAM>(elapsed));
    }).detach();
}

void OnGameExited(DWORD code, DWORD elapsed) {
    if (g.gameProcess) {
        CloseHandle(g.gameProcess);
        g.gameProcess = nullptr;
    }
    g.launching = false;
    g.gameRunning = false;
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);

    int mission = g.gameMission;
    g.gameMission = -1;
    const bool sawWindow = g.sawGameWindow.exchange(false);
    // Only a launch that never produced a game window counts as a startup
    // failure.  In-game mission failures must not hide the map permanently.
    bool failed = !sawWindow && !g.gameUsedAres && (code != 0 || elapsed < 5000);
    if (failed) {
        std::string reason = "游戏进程提前结束（代码 " + std::to_string(code) + "，" +
                             std::to_string(elapsed) + " 毫秒）";
        FailLaunch(mission, reason);
    } else {
        SendMessageW(g.hStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"游戏已退出"));
    }
    EnableWindow(g.hLaunch, g.selectedMission >= 0);
    UpdateStatus();
}

// ------------------------------------------------------------------ scanning

void LoadData() {
    using clock = std::chrono::steady_clock;
    g.cachePath = rl::DefaultCachePath(g.root);
    if (g.configPath.empty()) g.configPath = JoinPath(g.root, "ReunionLite.ini");
    LoadConfig();
    g.title = Wide(DefaultWindowTitle(g.root));

    auto t0 = clock::now();
    g.fingerprint = rl::FingerprintMapsFolder(g.root);
    auto t1 = clock::now();
    g.fingerprintMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    t0 = clock::now();
    g.cacheHit = rl::LoadCampaignCache(g.cachePath, g.fingerprint, g.campaign);
    t1 = clock::now();
    g.loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (!g.cacheHit) {
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        t0 = clock::now();
        g.campaign = rl::ScanCampaigns(g.root);
        t1 = clock::now();
        g.scanMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        rl::SaveCampaignCache(g.cachePath, g.fingerprint, g.campaign);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }

    g.registry = rl::LoadModRegistry(g.root);
    BuildOrder();
    BuildPackList();
    RebuildPackCombo();
    ApplyOptionsToUi();
    SetWindowTextW(g.hwnd, g.title.c_str());
    ApplyFilters();

    if (!g.config.lastMission.empty()) {
        for (int row = 0; row < static_cast<int>(g.filtered.size()); ++row) {
            const auto& m = g.campaign.missions[g.filtered[row]];
            if ((m.sourceFile + "|" + m.section) == g.config.lastMission) {
                ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(g.hList, row, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g.hList, row, FALSE);
                SelectionChanged(row);
                break;
            }
        }
    }

    if (!g.campaign.issues.empty()) {
        std::string text = "扫描时发现以下问题：\r\n\r\n";
        for (std::size_t i = 0; i < g.campaign.issues.size() && i < 12; ++i)
            text += g.campaign.issues[i].file + "：" + g.campaign.issues[i].message + "\r\n";
        MessageBoxW(g.hwnd, Wide(text).c_str(), L"地图扫描", MB_OK | MB_ICONWARNING);
    }
}

void Rescan() {
    if (g.launching || g.gameRunning) return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    g.fingerprint = rl::FingerprintMapsFolder(g.root);
    g.campaign = rl::ScanCampaigns(g.root);
    auto t1 = clock::now();
    g.scanMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    g.cacheHit = false;
    rl::SaveCampaignCache(g.cachePath, g.fingerprint, g.campaign);
    g.registry = rl::LoadModRegistry(g.root);
    BuildOrder();
    BuildPackList();
    RebuildPackCombo();
    ApplyFilters();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    std::wstring message = L"扫描完成：" + std::to_wstring(g.order.size()) + L" 张可启动地图";
    if (!g.campaign.issues.empty()) {
        message += L"\r\n\r\n发现问题：" + std::to_wstring(g.campaign.issues.size()) + L" 个";
        for (std::size_t i = 0; i < g.campaign.issues.size() && i < 8; ++i)
            message += L"\r\n" + Wide(g.campaign.issues[i].file) + L"：" +
                       Wide(g.campaign.issues[i].message);
    }
    MessageBoxW(g.hwnd, message.c_str(), L"重新扫描", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------------ window

void EnsureSelectionVisible() {
    if (g.selectedMission < 0) return;
    for (int row = 0; row < static_cast<int>(g.filtered.size()); ++row) {
        if (g.filtered[row] != g.selectedMission) continue;
        ListView_EnsureVisible(g.hList, row, FALSE);
        return;
    }
}

void Layout() {
    if (!g.hwnd || !g.hStatus) return;
    RECT rc{};
    GetClientRect(g.hwnd, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int margin = S(10);
    const int rowHeight = S(28);
    const int statusHeight = S(24);
    const int optionsWidth = S(330);
    const int descriptionHeight = S(150);

    int top = margin;
    int x = margin;

    // Search label + edit
    HWND searchLabel = GetDlgItem(g.hwnd, 200);
    MoveWindow(searchLabel, x, top + S(6), S(40), S(20), TRUE);
    x += S(42);
    MoveWindow(g.hSearch, x, top, S(230), rowHeight, TRUE);
    x += S(238);

    HWND packLabel = GetDlgItem(g.hwnd, 201);
    MoveWindow(packLabel, x, top + S(6), S(52), S(20), TRUE);
    x += S(54);
    int packWidth = std::max(S(220), width - x - margin - S(120));
    if (packWidth > width - x - margin) packWidth = width - x - margin;
    MoveWindow(g.hPack, x, top, packWidth, rowHeight + S(360), TRUE);

    int rescanX = width - margin - S(110);
    MoveWindow(g.hRescan, rescanX, top, S(110), rowHeight, TRUE);

    int contentTop = top + rowHeight + S(10);
    int contentBottom = height - statusHeight - margin;
    int listWidth = width - margin * 3 - optionsWidth;
    int listHeight = contentBottom - contentTop - descriptionHeight - S(10);
    if (listHeight < S(120)) listHeight = S(120);

    MoveWindow(g.hList, margin, contentTop, listWidth, listHeight, TRUE);
    MoveWindow(g.hDesc, margin, contentTop + listHeight + S(10), listWidth, descriptionHeight,
               TRUE);
    {
        const int sideW = S(70);
        const int diffW = S(70);
        const int scenarioW = S(120);
        const int packW = S(280);
        int nameW = listWidth - sideW - diffW - scenarioW - packW - S(24);
        if (nameW < S(180)) nameW = S(180);
        ListView_SetColumnWidth(g.hList, 0, nameW);
        ListView_SetColumnWidth(g.hList, 1, packW);
        ListView_SetColumnWidth(g.hList, 2, sideW);
        ListView_SetColumnWidth(g.hList, 3, diffW);
        ListView_SetColumnWidth(g.hList, 4, scenarioW);
    }

    int ox = width - margin - optionsWidth;
    int oy = contentTop;
    MoveWindow(g.hGroup, ox, oy, optionsWidth, contentBottom - oy, TRUE);
    int innerX = ox + S(12);
    int fieldWidth = optionsWidth - S(24);
    int cy = oy + S(24);
    const int labelHeight = S(18);
    const int comboHeight = S(24);
    const int fieldGap = S(42);

    auto field = [&](HWND label, const wchar_t*) {
        MoveWindow(label, innerX, cy, fieldWidth, labelHeight, TRUE);
        cy += labelHeight + S(4);
        return cy;
    };

    field(g.hLblMod, L"模组");
    MoveWindow(g.hMod, innerX, cy, fieldWidth, comboHeight + S(300), TRUE);
    cy += fieldGap;
    field(g.hLblDiff, L"启动难度");
    MoveWindow(g.hDiff, innerX, cy, fieldWidth, comboHeight, TRUE);
    cy += fieldGap;
    field(g.hLblCredits, L"初始资金");
    MoveWindow(g.hCredits, innerX, cy, fieldWidth, comboHeight, TRUE);
    cy += fieldGap;
    field(g.hLblSpeed, L"游戏速度");
    MoveWindow(g.hSpeed, innerX, cy, fieldWidth, comboHeight, TRUE);
    cy += fieldGap;
    field(g.hLblAutosave, L"自动存档");
    MoveWindow(g.hAutosave, innerX, cy, fieldWidth, comboHeight, TRUE);
    cy += fieldGap;
    field(g.hLblMusic, L"游戏音乐");
    MoveWindow(g.hMusic, innerX, cy, fieldWidth, comboHeight, TRUE);
    cy += fieldGap + S(4);

    HWND checks[] = {g.hChkMovies, g.hChkSat, g.hChkAres, g.hChkPhobos, g.hChkTerrain,
                     g.hChkRepair, g.hChkCorr, g.hChkReverse};
    for (HWND check : checks) {
        MoveWindow(check, innerX, cy, fieldWidth, S(22), TRUE);
        cy += S(23);
    }

    int launchHeight = S(38);
    int launchY = contentBottom - launchHeight - S(10);
    if (launchY < cy + S(6)) launchY = cy + S(6);
    MoveWindow(g.hLaunch, innerX, launchY, fieldWidth, launchHeight, TRUE);

    // Status bar: a single full-width part keeps all information visible.
    int parts[1] = {-1};
    SendMessageW(g.hStatus, SB_SETPARTS, 1, reinterpret_cast<LPARAM>(parts));
    SendMessageW(g.hStatus, WM_SIZE, 0, 0);
    UpdateStatus();
}

void CreateChildren(HWND hwnd) {
    HINSTANCE inst = g.instance;
    auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle,
                      int id) {
        return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                               hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
                               nullptr);
    };

    create(L"STATIC", L"搜索", SS_LEFT, 0, 200);
    g.hSearch = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, ID_SEARCH);
    SendMessageW(g.hSearch, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入任务名 / 任务包 / 场景名，即时过滤"));

    create(L"STATIC", L"任务包", SS_LEFT, 0, 201);
    g.hPack = CreateWindowExW(0, L"COMBOBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST |
                                  WS_TABSTOP,
                              0, 0, 10, 10, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PACK)), inst,
                              nullptr);

    g.hRescan = create(L"BUTTON", L"重新扫描", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_RESCAN);

    g.hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                                  LVS_SHOWSELALWAYS | LVS_OWNERDATA | LVS_NOSORTHEADER |
                                  WS_TABSTOP,
                              0, 0, 10, 10, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)), inst,
                              nullptr);
    ListView_SetExtendedListViewStyle(g.hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                   LVS_EX_LABELTIP);

    struct Column {
        const wchar_t* text;
        int width;
    } columns[] = {{L"任务名称", 360}, {L"任务包", 320}, {L"阵营", 70},
                   {L"难度", 70},      {L"场景", 120}};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<wchar_t*>(columns[i].text);
        col.cx = S(columns[i].width);
        col.iSubItem = i;
        ListView_InsertColumn(g.hList, i, &col);
    }

    g.hDesc = create(L"EDIT", L"",
                     WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                     WS_EX_CLIENTEDGE, ID_DESC);

    g.hGroup = create(L"BUTTON", L"战役选项", BS_GROUPBOX, 0, 202);

    auto label = [&](const wchar_t* text) { return create(L"STATIC", text, SS_LEFT, 0, 0); };
    g.hLblMod = label(L"模组");
    g.hMod = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
                             0, 0, 10, 10, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MOD)), inst, nullptr);
    g.hLblDiff = label(L"启动难度");
    g.hDiff = CreateWindowExW(0, L"COMBOBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
                              0, 0, 10, 10, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DIFF)), inst,
                              nullptr);
    for (int i = 0; i < 3; ++i) AddComboString(g.hDiff, kDifficultyLabels[i], i);

    g.hLblCredits = label(L"初始资金");
    g.hCredits = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST |
                                     WS_TABSTOP,
                                 0, 0, 10, 10, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CREDITS)), inst,
                                 nullptr);
    for (int i = 0; i < static_cast<int>(sizeof(kCreditsValues) / sizeof(int)); ++i)
        AddComboString(g.hCredits, kCreditsLabels[i], kCreditsValues[i]);

    g.hLblSpeed = label(L"游戏速度");
    g.hSpeed = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
                               0, 0, 10, 10, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SPEED)), inst,
                               nullptr);
    for (int i = 0; i < 7; ++i) AddComboString(g.hSpeed, kSpeedLabels[i], i);

    g.hLblAutosave = label(L"自动存档");
    g.hAutosave = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST |
                                      WS_TABSTOP,
                                  0, 0, 10, 10, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_AUTOSAVE)), inst,
                                  nullptr);
    for (int i = 0; i < 4; ++i) AddComboString(g.hAutosave, kAutosaveLabels[i], i);

    g.hLblMusic = label(L"游戏音乐");
    g.hMusic = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
                               0, 0, 10, 10, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MUSIC)), inst,
                               nullptr);
    for (int i = 0; i < 6; ++i) AddComboString(g.hMusic, kMusicLabels[i], kMusicValues[i]);

    g.hChkMovies = create(L"BUTTON", L"播放简报 / 过场", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                          ID_CHK_MOVIES);
    SetChecked(g.hChkMovies, true);
    g.hChkSat = create(L"BUTTON", L"透明迷雾", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_CHK_SAT);
    g.hChkAres = create(L"BUTTON", L"启用 Ares", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_CHK_ARES);
    g.hChkPhobos = create(L"BUTTON", L"启用 Phobos", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                          ID_CHK_PHOBOS);
    g.hChkTerrain = create(L"BUTTON", L"地形扩展 (TX)", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                           ID_CHK_TERRAIN);
    g.hChkRepair = create(L"BUTTON", L"自动维修", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_CHK_REPAIR);
    g.hChkCorr = create(L"BUTTON", L"数值修复", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_CHK_CORR);
    g.hChkReverse = create(L"BUTTON", L"军备反转", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                           ID_CHK_REVERSE);

    g.hLaunch = create(L"BUTTON", L"启动游戏", BS_PUSHBUTTON | BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
                       ID_LAUNCH);
    EnableWindow(g.hLaunch, FALSE);

    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(203)), inst,
                                nullptr);

    HWND all[] = {g.hSearch,   g.hPack,      g.hRescan,   g.hList,      g.hDesc,
                  g.hGroup,    g.hMod,       g.hDiff,     g.hCredits,   g.hSpeed,
                  g.hAutosave, g.hMusic,   g.hChkMovies, g.hChkSat,   g.hChkAres,   g.hChkPhobos, g.hChkTerrain,
                  g.hChkRepair, g.hChkCorr,  g.hChkReverse, g.hLaunch, g.hStatus,
                  GetDlgItem(hwnd, 200), GetDlgItem(hwnd, 201)};
    for (HWND h : all) SetFont(h);
}

void RecreateFont() {
    if (g.font && g.font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(g.font);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, g.dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    g.font = CreateFontIndirectW(&lf);
    if (!g.font) g.font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

BOOL CALLBACK SetChildFont(HWND child, LPARAM) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    return TRUE;
}

void ApplyFonts() {
    if (g.hwnd) EnumChildWindows(g.hwnd, SetChildFont, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        CreateChildren(hwnd);
        return 0;
    case WM_DPICHANGED: {
        g.dpi = static_cast<int>(HIWORD(wParam));
        if (g.dpi < 96) g.dpi = 96;
        RecreateFont();
        ApplyFonts();
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = S(960);
        mmi->ptMinTrackSize.y = S(620);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
        case ID_SEARCH:
            if (code == EN_CHANGE && !g.launching) {
                g.search = GetTextUtf8(g.hSearch);
                ApplyFilters();
            }
            return 0;
        case ID_PACK:
            if (code == CBN_SELCHANGE) {
                g.packFilter = static_cast<int>(ComboData(g.hPack));
                ApplyFilters();
            }
            return 0;
        case ID_RESCAN:
            Rescan();
            return 0;
        case ID_LAUNCH:
            OnLaunch();
            return 0;
        case ID_CHK_ARES:
            if (IsChecked(g.hChkAres)) {
                if (!IsWindowEnabled(g.hChkPhobos) && g.selectedMission >= 0 &&
                    g.campaign.missions[g.selectedMission].phobos != 1)
                    EnableWindow(g.hChkPhobos, TRUE);
            } else {
                SetChecked(g.hChkPhobos, false);
                EnableWindow(g.hChkPhobos, FALSE);
            }
            return 0;
        case ID_CHK_PHOBOS:
            if (IsChecked(g.hChkPhobos) && !IsChecked(g.hChkAres)) {
                SetChecked(g.hChkAres, true);
                EnableWindow(g.hChkPhobos, TRUE);
            }
            return 0;
        default:
            return 0;
        }
    }
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->idFrom != ID_LIST) break;
        switch (header->code) {
        case LVN_GETDISPINFO: {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
            if (!(info->item.mask & LVIF_TEXT)) return 0;
            int row = info->item.iItem;
            if (row < 0 || row >= static_cast<int>(g.filtered.size())) return 0;
            const auto& m = g.campaign.missions[g.filtered[row]];
            switch (info->item.iSubItem) {
            case 0:
                g.cell = Wide(m.name);
                break;
            case 1:
                g.cell = Wide(m.packName);
                break;
            case 2:
                g.cell = SideLabel(m.side);
                break;
            case 3:
                g.cell = Wide(m.difficulty);
                break;
            case 4:
                g.cell = Wide(m.scenario);
                break;
            default:
                g.cell.clear();
                break;
            }
            info->item.pszText = const_cast<wchar_t*>(g.cell.c_str());
            return 0;
        }
        case LVN_ITEMCHANGED: {
            auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((changed->uChanged & LVIF_STATE) &&
                (changed->uNewState & LVIS_SELECTED) &&
                !(changed->uOldState & LVIS_SELECTED)) {
                SelectionChanged(changed->iItem);
            }
            return 0;
        }
        case NM_DBLCLK:
            OnLaunch();
            return 0;
        case LVN_KEYDOWN: {
            auto* key = reinterpret_cast<NMLVKEYDOWN*>(lParam);
            if (key->wVKey == VK_RETURN) OnLaunch();
            return 0;
        }
        default:
            break;
        }
        break;
    }
    case WM_APP_GAME_EXIT:
        OnGameExited(static_cast<DWORD>(wParam), static_cast<DWORD>(lParam));
        return 0;
    case WM_CLOSE:
        SaveConfig();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g.alive = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g.instance = instance;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i + 1 < argc; ++i) {
                std::wstring key = argv[i];
                if (key == L"--root")
                    g.forcedRoot = Utf8(argv[++i]);
                else if (key == L"--filter")
                    g.startupFilter = Utf8(argv[++i]);
                else if (key == L"--launch")
                    g.startupLaunch = Utf8(argv[++i]);
            }
            LocalFree(argv);
        }
    }
    {
        g.dpi = 96;
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            using GetDpiForSystemFn = UINT(WINAPI*)();
            auto getDpiForSystem =
                reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
            if (getDpiForSystem) g.dpi = static_cast<int>(getDpiForSystem());
        }
        if (g.dpi < 96) g.dpi = 96;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    HICON appIcon = reinterpret_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!appIcon) appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIcon = appIcon;
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), 0));
    if (!wc.hIconSm) wc.hIconSm = appIcon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;

    // Window geometry is stored in DIPs and scaled to the current monitor DPI.
    const std::string exeDir = ExeDirectoryUtf8();
    g.root = exeDir;
    g.configPath = JoinPath(exeDir, "ReunionLite.ini");
    LoadConfig();
    std::string detectedRoot;
    if (!g.forcedRoot.empty() && LooksLikeGameRoot(g.forcedRoot)) {
        detectedRoot = g.forcedRoot;
    } else {
        detectedRoot = DetectGameRoot(exeDir, g.config.gameRoot);
    }
    if (detectedRoot.empty()) {
        MessageBoxW(nullptr,
                    L"找不到游戏目录。\n\n请把 ReunionLite.exe 放到包含 Maps\\CP 和 "
                    L"Mod&AI 的红色警戒2重聚未来根目录中运行。",
                    L"Reunion Lite", MB_OK | MB_ICONERROR);
        return 1;
    }
    g.root = detectedRoot;
    g.cachePath = rl::DefaultCachePath(g.root);
    LoadConfig();
    int dipW = (g.config.w > 400) ? g.config.w : 1200;
    int dipH = (g.config.h > 300) ? g.config.h : 760;

    RecreateFont();

    int windowW = MulDiv(dipW, g.dpi, 96);
    int windowH = MulDiv(dipH, g.dpi, 96);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowX = std::max(0, (screenW - windowW) / 2);
    int windowY = std::max(0, (screenH - windowH) / 2);

    HWND hwnd = CreateWindowExW(0, kClassName, L"红色警戒2 重聚未来", WS_OVERLAPPEDWINDOW,
                                windowX, windowY, windowW, windowH, nullptr, nullptr, instance,
                                nullptr);
    if (!hwnd) return 1;

    // The window may land on a monitor whose DPI differs from the system DPI.
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto getDpiForWindow =
            reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow) {
            int actual = static_cast<int>(getDpiForWindow(hwnd));
            if (actual >= 96 && actual != g.dpi) {
                g.dpi = actual;
                RecreateFont();
                ApplyFonts();
                int w = MulDiv(dipW, g.dpi, 96);
                int h = MulDiv(dipH, g.dpi, 96);
                SetWindowPos(hwnd, nullptr, std::max(0, (screenW - w) / 2),
                             std::max(0, (screenH - h) / 2), w, h,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    LoadData();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (!g.startupFilter.empty()) {
        SetWindowTextW(g.hSearch, Wide(g.startupFilter).c_str());
        g.search = g.startupFilter;
        ApplyFilters();
    }
    ShowWindow(hwnd, g.config.maximized ? SW_SHOWMAXIMIZED : (showCommand == SW_SHOWMINIMIZED
                                                                  ? SW_SHOWMINIMIZED
                                                                  : SW_SHOW));
    UpdateWindow(hwnd);
    EnsureSelectionVisible();

    AppendLog("loaded filtered=" + std::to_string(g.filtered.size()) +
              " launchArg=" + g.startupLaunch);
    if (!g.startupLaunch.empty()) {
        int missionIndex = -1;
        for (int i = 0; i < static_cast<int>(g.campaign.missions.size()); ++i) {
            const auto& m = g.campaign.missions[i];
            if ((m.sourceFile + "|" + m.section) == g.startupLaunch ||
                (m.scenario + "|" + m.section) == g.startupLaunch ||
                m.section == g.startupLaunch) {
                missionIndex = i;
                break;
            }
        }
        AppendLog("startup missionIndex=" + std::to_string(missionIndex));
        if (missionIndex >= 0) {
            g.packFilter = -1;
            g.config.packId.clear();
            SelectComboData(g.hPack, -1);
            SetWindowTextW(g.hSearch, L"");
            g.search.clear();
            ApplyFilters();
            for (int row = 0; row < static_cast<int>(g.filtered.size()); ++row) {
                if (g.filtered[row] != missionIndex) continue;
                ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(g.hList, row, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                SelectionChanged(row);
                OnLaunch();
                break;
            }
        }
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g.font && g.font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(g.font);
    return static_cast<int>(message.wParam);
}














