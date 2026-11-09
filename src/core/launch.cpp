#include "launch.h"
#include "encoding.h"
#include "platform_path.h"
#include "ini.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace rl {
namespace {

fs::path Fs(const std::string& utf8) {
#ifdef _WIN32
    return fs::u8path(utf8);
#else
    return fs::u8path(rl::MapPlatformPath(utf8));
#endif
}
std::string U8(const fs::path& p) { return p.u8string(); }

std::string LowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool EndsWithNoCase(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           IniEquals(std::string_view(s).substr(s.size() - suffix.size()), suffix);
}

bool ReadAllBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(Fs(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(f) || f.eof();
}

bool ReadTextAnsi(const std::string& path, std::string& utf8) {
    std::vector<std::uint8_t> bytes;
    if (!ReadAllBytes(path, bytes)) return false;
    utf8 = DecodeAnsiToUtf8(bytes);
    return true;
}

bool DirExists(const std::string& p) {
    std::error_code ec;
    return fs::is_directory(Fs(p), ec);
}

bool FileExists(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(Fs(p), ec);
}

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '\\' && out.back() != '/') out.push_back('\\');
    out += b;
    return out;
}

bool IsAbsolutePath(const std::string& p) {
    if (p.size() >= 2 && p[1] == ':' &&
        ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')))
        return true;
    if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\') return true;
    return false;
}

std::string ResolveGamePath(const std::string& root, const std::string& p) {
    return IsAbsolutePath(p) ? p : Join(root, p);
}

bool HasMixFile(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        if (EndsWithNoCase(U8(it->path().filename()), ".mix")) return true;
    }
    return false;
}

std::string RelativeUnder(const fs::path& base, const fs::path& file) {
    std::error_code ec;
    fs::path rel = fs::relative(file, base, ec);
    std::string s = ec ? U8(file.filename()) : U8(rel);
    for (char& c : s)
        if (c == '/') c = '\\';
    return s;
}

void CollectFiles(const fs::path& base, const fs::path& dir, bool missionSide,
                  std::vector<std::string>& normalFiles, std::vector<std::string>& csfFiles) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const fs::path& p = it->path();
        std::string name = U8(p.filename());
        if (EndsWithNoCase(name, ".map")) continue;
        if (EndsWithNoCase(name, ".csf")) {
            csfFiles.push_back(RelativeUnder(base, p));
            continue;
        }
        if (!missionSide && IniEquals(name, "ra2md.ini")) continue; // keep user settings
        normalFiles.push_back(RelativeUnder(base, p));
    }
    std::sort(normalFiles.begin(), normalFiles.end());
    std::sort(csfFiles.begin(), csfFiles.end());
}

std::string FirstMod(const std::string& list) {
    std::size_t comma = list.find(',');
    std::string first = comma == std::string::npos ? list : list.substr(0, comma);
    while (!first.empty() && (first.front() == ' ' || first.front() == '\t')) first.erase(0, 1);
    while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) first.pop_back();
    return first;
}

std::string SpawnText(const Mission& m, const LaunchPlan& plan, const LaunchOptions& options) {
    std::string game = plan.modGamePath;
    for (char& c : game)
        if (c == '\\') c = '/';

    std::string out;
    out += "[Settings]\r\n";
    // The launcher slider is slow (0) to fast (6); spawn.ini and the engine
    // store the native direction (0 = fastest).
    int nativeSpeed = 6 - (options.gameSpeed < 0 ? 0 : options.gameSpeed > 6 ? 6 : options.gameSpeed);
    out += "GameSpeed=" + std::to_string(nativeSpeed) + "\r\n";
    out += "Game=" + game + "\r\n";
    out += "Mission=" + m.path + "\r\n";
    out += "Ra2Mode=False\r\n";
    out += std::string("chkSatellite=") + (options.satellite ? "True" : "False") + "\r\n";
    out += std::string("chkAres=") + (plan.useAres ? "True" : "False") + "\r\n";
    out += std::string("chkPhobos=") + (plan.usePhobos ? "True" : "False") + "\r\n";
    out += "Scenario=" + plan.scenarioName + "\r\n";
    out += "CampaignID=" + std::to_string(m.index) + "\r\n";
    out += "IsSinglePlayer=True\r\n";
    out += "SidebarHack=False\r\n";
    out += "Side=" + std::to_string(m.sideValue) + "\r\n";
    out += std::string("BuildOffAlly=") + (m.buildOffAlly ? "True" : "False") + "\r\n";
    out += "DifficultyModeHuman=" +
           std::to_string(m.alwaysNormal ? 1 : options.difficulty) + "\r\n";
    out += "DifficultyModeComputer=" + std::to_string(std::abs(options.difficulty - 2)) + "\r\n";
    out += std::string("chkTerrain=") + (plan.useTx ? "True" : "False") + "\r\n";
    return out;
}

// ------------------------------------------------------------- reverse swap

struct ReplacePair {
    const char* a;
    const char* b;
};

// Same table as the original client's CampaignSelector.
const ReplacePair kRa2Dict[] = {
    {"E1", "E2"},         {"ADOG", "DOG"},       {"ENGINEER", "SENGINEER"},
    {"GHOST", "DESO"},    {"CMIN", "HARV"},      {"CMON", "HORV"},
    {"MTNK", "HTNK"},     {"FV", "HTK"},         {"MGTK", "APOC"},
    {"SREF", "V3"},       {"AMCV", "SMCV"},      {"LCRF", "SAPC"},
    {"DEST", "SUB"},      {"AEGIS", "HYD"},      {"DLPH", "SQD"},
    {"CARRIER", "DRED"},  {"GAPOWR", "NAPOWR"},  {"GAREFN", "NAREFN"},
    {"GAPILE", "NAHAND"}, {"GAWEAP", "NAWEAP"},  {"GAAIRC", "NARADR"},
    {"AMRADR", "NARADR"}, {"GAYARD", "NAYARD"},  {"GADEPT", "NADEPT"},
    {"GATECH", "NATECH"}, {"GACNST", "NACNST"},  {"GAWALL", "NAWALL"},
    {"GAPILL", "NALASR"}, {"NASAM", "NAFLAK"},   {"ATESLA", "TESLA"},
    {"GAGAP", "NAPSIS"},  {"GACSPH", "NAIRON"},  {"GAWEAT", "NAMISL"},
};

const ReplacePair kYrDict[] = {
    {"E1", "E2"},         {"ADOG", "DOG"},       {"ENGINEER", "SENGINEER"},
    {"GGI", "FLAKT"},     {"JUMPJET", "LUNR"},   {"GHOST", "DESO"},
    {"TANY", "BORIS"},    {"CMIN", "HARV"},      {"CMON", "HORV"},
    {"MTNK", "HTNK"},     {"FV", "HTK"},         {"BFRT", "APOC"},
    {"SREF", "V3"},       {"AMCV", "SMCV"},      {"LCRF", "SAPC"},
    {"DEST", "SUB"},      {"AEGIS", "HYD"},      {"DLPH", "SQD"},
    {"CARRIER", "DRED"},  {"GAPOWR", "NAPOWR"},  {"GAREFN", "NAREFN"},
    {"GAPILE", "NAHAND"}, {"GAWEAP", "NAWEAP"},  {"GAAIRC", "NARADR"},
    {"AMRADR", "NARADR"}, {"GAYARD", "NAYARD"},  {"GADEPT", "NADEPT"},
    {"GATECH", "NATECH"}, {"GAOREP", "NAINDP"},  {"GACNST", "NACNST"},
    {"GAWALL", "NAWALL"}, {"GAPILL", "NALASR"},  {"NASAM", "NAFLAK"},
    {"ATESLA", "TESLA"},  {"GACSPH", "NAIRON"},  {"GAWEAT", "NAMISL"},
};

bool BoundaryBefore(char c) { return c == '\n' || c == '\r' || c == ',' || c == '='; }
bool BoundaryAfter(char c) { return c == '\n' || c == '\r' || c == ',' || c == '='; }

std::vector<std::size_t> FindToken(const std::string& text, const std::string& token) {
    std::vector<std::size_t> hits;
    if (token.empty()) return hits;
    const std::size_t n = text.size();
    const std::size_t m = token.size();
    for (std::size_t i = 0; i + m <= n; ++i) {
        if (i > 0 && !BoundaryBefore(text[i - 1])) continue;
        bool match = true;
        for (std::size_t k = 0; k < m; ++k) {
            char a = text[i + k];
            char b = token[k];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        if (i + m < n && !BoundaryAfter(text[i + m])) continue;
        hits.push_back(i);
    }
    return hits;
}

std::string ReplaceToken(const std::string& text, const std::string& token,
                         const std::string& replacement) {
    std::vector<std::size_t> hits = FindToken(text, token);
    if (hits.empty()) return text;
    std::string out;
    out.reserve(text.size() + replacement.size() * hits.size());
    std::size_t last = 0;
    for (std::size_t pos : hits) {
        out.append(text, last, pos - last);
        out += replacement;
        last = pos + token.size();
    }
    out.append(text, last, std::string::npos);
    return out;
}

std::string ReplaceTwoWay(std::string text, bool ra2Dict) {
    const ReplacePair* dict = ra2Dict ? kRa2Dict : kYrDict;
    const std::size_t count = ra2Dict ? (sizeof(kRa2Dict) / sizeof(kRa2Dict[0]))
                                      : (sizeof(kYrDict) / sizeof(kYrDict[0]));
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        pairs.emplace_back(dict[i].a, dict[i].b);

    // Phase 1: hide both directions behind placeholders.
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        text = ReplaceToken(text, pairs[i].first, "\x01K" + std::to_string(i) + "\x01");
        text = ReplaceToken(text, pairs[i].second, "\x01V" + std::to_string(i) + "\x01");
    }
    // Phase 2: substitute placeholders with the opposite side.
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        std::string k = "\x01K" + std::to_string(i) + "\x01";
        std::string v = "\x01V" + std::to_string(i) + "\x01";
        std::vector<std::size_t> hits;
        for (std::size_t pos = text.find(k); pos != std::string::npos; pos = text.find(k, pos))
            hits.push_back(pos);
        if (!hits.empty()) {
            std::string out;
            std::size_t last = 0;
            for (std::size_t pos : hits) {
                out.append(text, last, pos - last);
                out += pairs[i].second;
                last = pos + k.size();
            }
            out.append(text, last, std::string::npos);
            text.swap(out);
        }
        hits.clear();
        for (std::size_t pos = text.find(v); pos != std::string::npos; pos = text.find(v, pos))
            hits.push_back(pos);
        if (!hits.empty()) {
            std::string out;
            std::size_t last = 0;
            for (std::size_t pos : hits) {
                out.append(text, last, pos - last);
                out += pairs[i].first;
                last = pos + v.size();
            }
            out.append(text, last, std::string::npos);
            text.swap(out);
        }
    }
    return text;
}

std::string MapCodePath(const std::string& gameRoot, const char* relative) {
    return Join(gameRoot, relative);
}

} // namespace

std::vector<ModInfo> LoadModRegistry(const std::string& gameRootUtf8) {
    std::vector<ModInfo> result;
    fs::path dir = Fs(Join(gameRootUtf8, "Mod&AI"));
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return result;

    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = U8(it->path().filename());
        if (!EndsWithNoCase(name, ".ini")) continue;
        if (LowerAscii(name).rfind("mod&ai", 0) != 0) continue;
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    std::unordered_map<std::string, std::size_t> seen;
    for (const fs::path& file : files) {
        std::string text;
        if (!ReadTextAnsi(U8(file), text)) continue;
        IniDocument ini;
        ini.Parse(text);
        if (!ini.HasSection("Mod")) continue;
        for (const std::string& key : ini.GetKeys("Mod")) {
            std::string id = ini.Get("Mod", key, "");
            if (id.empty() || !ini.HasSection(id)) continue;
            std::string lower = LowerAscii(id);
            if (seen.find(lower) != seen.end()) continue;
            ModInfo info;
            info.id = id;
            info.name = ini.Get(id, "Name", id);
            info.filePath = NormalizeRelPath(
                ini.Get(id, "FilePath", "Mod&AI/Mod/" + id));
            info.iniPath = U8(file);
            info.cpVisible = ini.GetBool(id, "CpVisible", true);
            info.yr = ini.GetBool(id, "YR", true);
            seen.emplace(lower, result.size());
            result.push_back(std::move(info));
        }
    }
    return result;
}

std::vector<ModInfo> ModsForMission(const std::vector<ModInfo>& registry, const Mission& mission) {
    std::vector<ModInfo> out;
    std::unordered_map<std::string, std::size_t> seen;
    auto add = [&](const ModInfo& mod) {
        std::string lower = LowerAscii(mod.id);
        if (seen.find(lower) != seen.end()) return;
        seen.emplace(lower, out.size());
        out.push_back(mod);
    };
    auto find = [&](const std::string& id) -> const ModInfo* {
        for (const auto& m : registry)
            if (IniEquals(m.id, id)) return &m;
        return nullptr;
    };

    std::string list = mission.modList;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t comma = list.find(',', start);
        std::string id = list.substr(start, comma == std::string::npos ? std::string::npos
                                                                       : comma - start);
        while (!id.empty() && (id.front() == ' ' || id.front() == '\t')) id.erase(0, 1);
        while (!id.empty() && (id.back() == ' ' || id.back() == '\t')) id.pop_back();
        if (!id.empty()) {
            if (const ModInfo* mod = find(id)) {
                if (mod->cpVisible) add(*mod);
            } else {
                ModInfo synthetic;
                synthetic.id = id;
                synthetic.name = id;
                synthetic.filePath = "Mod&AI/Mod/" + id;
                add(synthetic);
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    if (out.empty()) {
        for (const auto& mod : registry)
            if (mod.cpVisible) add(mod);
    }
    if (!mission.defaultMod.empty()) {
        bool hasDefault = false;
        for (const auto& mod : out)
            if (IniEquals(mod.id, mission.defaultMod)) hasDefault = true;
        if (!hasDefault) {
            if (const ModInfo* mod = find(mission.defaultMod)) add(*mod);
        }
    }
    return out;
}

bool BuildLaunchPlan(const std::string& gameRootUtf8, const Mission& mission,
                     const LaunchOptions& options, LaunchPlan& plan,
                     const std::vector<Mission>* allMissions) {
    plan = LaunchPlan{};
    plan.gameRoot = gameRootUtf8;
    plan.runDir = Join(gameRootUtf8, "Run");
    plan.satellite = options.satellite;

    if (mission.scenario.empty()) {
        plan.error = "该条目没有场景文件";
        return false;
    }

    // The registry is a handful of small INI files; cache it for batch work
    // (startup preflight / audits) instead of re-reading it for every mission.
    static std::string registryRoot;
    static std::vector<ModInfo> registry;
    if (registryRoot != gameRootUtf8) {
        registryRoot = gameRootUtf8;
        registry = LoadModRegistry(gameRootUtf8);
    }
    std::string wantedMod = !options.modId.empty() ? options.modId : mission.defaultMod;
    if (wantedMod.empty()) wantedMod = FirstMod(mission.modList);
    if (wantedMod.empty()) wantedMod = "YR";

    const ModInfo* chosen = nullptr;
    for (const auto& mod : registry)
        if (IniEquals(mod.id, wantedMod)) chosen = &mod;

    std::string modRel = chosen ? chosen->filePath : "Mod&AI/Mod/" + wantedMod;
    std::string modDir = ResolveGamePath(gameRootUtf8, modRel);
    if (!DirExists(modDir)) {
        std::string alt = Join(gameRootUtf8, "Mod&AI/" + wantedMod);
        if (DirExists(alt)) {
            modDir = alt;
            modRel = "Mod&AI/" + wantedMod;
        } else {
            plan.error = "找不到模组目录：" + modRel;
            return false;
        }
    }
    for (char& c : modRel)
        if (c == '\\') c = '/';
    plan.modId = chosen ? chosen->id : wantedMod;
    plan.modName = chosen ? chosen->name : wantedMod;
    plan.modDir = modDir;
    plan.modGamePath = modRel;
    plan.useRa2Dict = chosen ? !chosen->yr
                             : (IniEquals(wantedMod, "RA2") ||
                                LowerAscii(wantedMod).rfind("ra2_", 0) == 0);

    std::string missionPath = mission.path;
    if (missionPath.empty()) missionPath = mission.packFilePath;
    if (missionPath.empty()) {
        plan.error = "任务没有配置地图目录";
        return false;
    }
    plan.missionDir = ResolveGamePath(gameRootUtf8, missionPath);
    if (!DirExists(plan.missionDir)) {
        const std::string resolved = ResolvePathIgnoreCase(plan.missionDir);
        if (resolved.empty()) {
            plan.error = "任务目录不存在：" + missionPath;
            return false;
        }
        plan.missionDir = resolved;
    }
    std::string actualScenario = FindFileIgnoreCase(plan.missionDir, mission.scenario);
    if (!actualScenario.empty()) {
        plan.hasLooseMap = true;
        plan.scenarioName = actualScenario;
        plan.mapSourcePath = Join(plan.missionDir, actualScenario);
    } else if (HasMixFile(Fs(plan.missionDir))) {
        // The scenario is packed inside a MIX shipped with the mission pack.
        plan.hasLooseMap = false;
        plan.scenarioName = mission.scenario;
        plan.mapSourcePath.clear();
        plan.warnings.push_back("地图位于 MIX 内，地图级选项（资金/数值修复等）不会写入");
    } else {
        plan.error = "场景文件不存在：" + mission.scenario;
        return false;
    }

    plan.useAres = options.ares || mission.ares == 1;
    plan.usePhobos = options.phobos || mission.phobos == 1;
    if (plan.usePhobos) plan.useAres = true;
    plan.useTx = options.terrain || mission.tx == 1;

    if (plan.useAres) {
        if (!FileExists(Join(gameRootUtf8, "Ares/Ares.dll"))) {
            plan.error = "启用了 Ares，但缺少 Ares\\Ares.dll";
            return false;
        }
    }
    if (plan.usePhobos && !FileExists(Join(gameRootUtf8, "Phobos/Phobos.dll"))) {
        plan.error = "启用了 Phobos，但缺少 Phobos\\Phobos.dll";
        return false;
    }
    if (plan.useTx && !DirExists(Join(gameRootUtf8, "TX"))) {
        plan.error = "启用了地形扩展，但缺少 TX 目录";
        return false;
    }
    if (plan.satellite && !FileExists(Join(gameRootUtf8, "Resources/shroud.shp")))
        plan.warnings.push_back("未找到 Resources\\shroud.shp，透明迷雾可能不完整");

    CollectFiles(Fs(modDir), Fs(modDir), false, plan.modFiles, plan.modCsfFiles);
    CollectFiles(Fs(plan.missionDir), Fs(plan.missionDir), true, plan.missionFiles,
                 plan.missionCsfFiles);

    // Campaign packs continue into the next mission by loading another
    // scenario from the same folder.  Make every sibling scenario of this
    // pack available in Run\ so the transition works (the original client
    // does the same asynchronously).
    if (allMissions) {
        std::vector<std::string> seen;
        seen.push_back(LowerAscii(plan.scenarioName));
        for (const Mission& sibling : *allMissions) {
            if (&sibling == &mission) continue;
            if (sibling.scenario.empty()) continue;
            if (!mission.packId.empty()) {
                if (!IniEquals(sibling.packId, mission.packId)) continue;
            } else if (!IniEquals(sibling.packName, mission.packName)) {
                continue;
            }
            std::string path = sibling.path.empty() ? sibling.packFilePath : sibling.path;
            if (path.empty()) continue;
            std::string dir = ResolveGamePath(gameRootUtf8, path);
            if (!DirExists(dir)) continue;
            std::string actual = FindFileIgnoreCase(dir, sibling.scenario);
            if (actual.empty()) continue;
            std::string lower = LowerAscii(actual);
            bool duplicate = false;
            for (const auto& item : seen)
                if (item == lower) duplicate = true;
            if (duplicate) continue;
            seen.push_back(lower);
            plan.siblingMaps.push_back(SiblingMap{actual, Join(dir, actual)});
        }
    }

    auto addMapCode = [&](const std::string& path, const char* what) {
        if (FileExists(path)) {
            plan.mapCodeInis.push_back(path);
        } else {
            plan.warnings.push_back(std::string("缺少") + what + "：" + path);
        }
    };

    if (options.autoRepair)
        addMapCode(MapCodePath(gameRootUtf8, "INI/Multi/GameOptions/Auto Repair.ini"), "自动维修配置");
    if (options.corr)
        addMapCode(MapCodePath(gameRootUtf8, "INI/Multi/GameOptions/Repair.INI"), "数值修复配置");
    if (options.autosave > 0) {
        static const char* names[] = {"", "5.ini", "10.ini", "20.ini"};
        int index = options.autosave;
        if (index < 1 || index > 3) index = 0;
        if (index > 0)
            addMapCode(MapCodePath(gameRootUtf8,
                                   (std::string("INI/Cp/GameOptions/Autosave/") + names[index]).c_str()),
                       "自动存档配置");
    }
    static const char* diffFiles[] = {"Easy", "Medium", "Hard"};
    int diff = options.difficulty;
    if (diff < 0) diff = 0;
    if (diff > 2) diff = 2;
    {
        std::string path = Join(gameRootUtf8, std::string("INI/MapCode/Difficulty ") +
                                                  diffFiles[diff] + ".ini");
        if (FileExists(path)) plan.mapCodeInis.push_back(path);
    }
    {
        std::string custom = Join(gameRootUtf8, "Client/custom_rules_all.ini");
        if (FileExists(custom)) plan.mapCodeInis.push_back(custom);
    }
    {
        fs::path customDir = Fs(Join(gameRootUtf8, "Custom/INI"));
        std::error_code ec;
        IniDocument components;
        std::string componentText;
        if (ReadTextAnsi(Join(gameRootUtf8, "Resources/component"), componentText))
            components.Parse(componentText);
        if (fs::is_directory(customDir, ec)) {
            std::vector<fs::path> dirs;
            for (fs::directory_iterator it(customDir, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                std::error_code ec2;
                if (it->is_directory(ec2)) dirs.push_back(it->path());
            }
            std::sort(dirs.begin(), dirs.end());
            for (const auto& dir : dirs) {
                std::string folder = U8(dir.filename());
                if (components.HasSection(folder) &&
                    !components.GetBool(folder, "enable", true))
                    continue;
                std::string ini = U8(dir / "custom_rules_all.ini");
                if (FileExists(ini)) plan.mapCodeInis.push_back(ini);
            }
        }
    }

    plan.extraArgs = "-LOG -CD -speedcontrol -b=27 -b=38 -b=44 -b=11 -AI-CONTROL -AFFINITY:1048575";
    {
        std::string definitions;
        if (ReadTextAnsi(Join(gameRootUtf8, "Resources/ClientDefinitions.ini"), definitions)) {
            IniDocument defs;
            defs.Parse(definitions);
            plan.extraArgs = defs.Get("Settings", "ExtraCommandLineParams", plan.extraArgs);
        }
    }

    plan.spawnIni = SpawnText(mission, plan, options);
    return true;
}

bool BuildPatchedMap(const LaunchPlan& plan, const LaunchOptions& options,
                     std::vector<std::uint8_t>& outBytes, std::string& error) {
    std::vector<std::uint8_t> raw;
    if (!ReadAllBytes(plan.mapSourcePath, raw)) {
        error = "无法读取地图文件：" + plan.mapSourcePath;
        return false;
    }
    IniDocument map;
    map.Parse(DecodeAnsiToUtf8(raw));
    if (map.Sections().empty()) {
        error = "地图不是有效的 INI 文件：" + plan.scenarioName;
        return false;
    }

    for (const std::string& codePath : plan.mapCodeInis) {
        std::string text;
        if (!ReadTextAnsi(codePath, text)) continue;
        IniDocument code;
        code.Parse(text);
        map.Merge(code);
    }

    if (options.credits > 0) {
        std::string player = map.Get("Basic", "Player", "");
        if (!player.empty()) map.SetInt(player, "Credits", options.credits / 100);
    }

    std::string text = map.Serialize();
    if (options.reverse) text = ReplaceTwoWay(std::move(text), plan.useRa2Dict);
    outBytes = EncodeUtf8ToAnsi(text);
    if (outBytes.empty()) {
        error = "地图编码转换失败：" + plan.scenarioName;
        return false;
    }
    return true;
}

} // namespace rl
