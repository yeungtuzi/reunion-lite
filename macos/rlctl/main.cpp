// rlctl - headless driver for the Reunion Lite core on macOS.
//
//   rlctl drives
//   rlctl scan   <gameRoot> [--json] [--no-cache] [--limit N]
//   rlctl plan   <gameRoot> <query> [--json] [options...]
//   rlctl prepare <gameRoot> <query> [--json] [options...]   (P2)
//
// <query> matches a scenario name, a section name or "<sourceFile>|<section>",
// and also accepts "#<mission index>".

#include "core/campaign.h"
#include "core/launch.h"
#include "core/platform_path.h"
#include "platform/mac/runprep.h"
#include "platform/mac/launch.h"
#include "platform/mac/client_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string Q(const std::string& s) { return "\"" + JsonEscape(s) + "\""; }

void PrintMissionJson(const rl::Mission& m, bool trailingComma = true) {
    std::printf(
        "{\"index\":%d,\"name\":%s,\"pack\":%s,\"packId\":%s,\"scenario\":%s,"
        "\"path\":%s,\"side\":%s,\"sideValue\":%d,\"difficulty\":%s,\"modList\":%s,"
        "\"defaultMod\":%s,\"section\":%s,\"sourceFile\":%s,\"badReason\":%s,"
        "\"ares\":%d,\"phobos\":%d,\"tx\":%d,\"yr\":%s,\"other\":%s,"
        "\"buildOffAlly\":%s,\"alwaysNormal\":%s,\"description\":%s}%s\n",
        m.index, Q(m.name).c_str(), Q(m.packName).c_str(), Q(m.packId).c_str(),
        Q(m.scenario).c_str(), Q(m.path).c_str(), Q(m.side).c_str(), m.sideValue,
        Q(m.difficulty).c_str(), Q(m.modList).c_str(), Q(m.defaultMod).c_str(),
        Q(m.section).c_str(), Q(m.sourceFile).c_str(), Q(m.badReason).c_str(), m.ares, m.phobos,
        m.tx, m.yr ? "true" : "false", m.other ? "true" : "false",
        m.buildOffAlly ? "true" : "false", m.alwaysNormal ? "true" : "false",
        Q(m.description).c_str(), trailingComma ? "," : "");
}

const rl::Mission* FindMission(const rl::Campaign& campaign, const std::string& query) {
    if (!query.empty() && query[0] == '#') {
        int index = std::atoi(query.c_str() + 1);
        for (const auto& m : campaign.missions)
            if (m.index == index) return &m;
        return nullptr;
    }
    for (const auto& m : campaign.missions)
        if ((m.sourceFile + "|" + m.section) == query || (m.scenario + "|" + m.section) == query ||
            m.section == query || m.scenario == query)
            return &m;
    // Fallback: case-insensitive substring on the mission name.
    std::string needle;
    for (char c : query) needle += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& m : campaign.missions) {
        std::string hay = m.name + " | " + m.scenario + " | " + m.packName;
        for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (hay.find(needle) != std::string::npos && m.badReason.empty()) return &m;
    }
    return nullptr;
}

bool LoadCampaign(const std::string& root, bool useCache, rl::Campaign& campaign) {
    rl::Fingerprint fingerprint = rl::FingerprintMapsFolder(root);
    if (useCache && rl::LoadCampaignCache(rl::DefaultCachePath(root), fingerprint, campaign))
        return true;
    campaign = rl::ScanCampaigns(root);
    if (useCache) rl::SaveCampaignCache(rl::DefaultCachePath(root), fingerprint, campaign);
    return false;
}

void ParseWindow(const std::string& arg, rlmac::VideoConfig& video) {
    const std::string prefix = "--window=";
    if (arg.rfind(prefix, 0) != 0) return;
    const std::string value = arg.substr(prefix.size());
    const std::size_t x = value.find('x');
    if (x == std::string::npos) return;
    const int w = std::atoi(value.substr(0, x).c_str());
    const int h = std::atoi(value.substr(x + 1).c_str());
    if (w >= 640 && h >= 480) { video.width = w; video.height = h; }
}

void ParseOption(const std::string& arg, rl::LaunchOptions& o) {
    auto value = [&](const char* prefix) -> const char* {
        std::size_t n = std::strlen(prefix);
        return arg.rfind(prefix, 0) == 0 ? arg.c_str() + n : nullptr;
    };
    if (const char* v = value("--difficulty=")) o.difficulty = std::atoi(v);
    else if (const char* v = value("--credits=")) o.credits = std::atoi(v);
    else if (const char* v = value("--speed=")) o.gameSpeed = std::atoi(v);
    else if (const char* v = value("--autosave=")) o.autosave = std::atoi(v);
    else if (const char* v = value("--music=")) o.musicVolume = std::atoi(v);
    else if (const char* v = value("--mod=")) o.modId = v;
    else if (arg == "--no-movies") o.playMovies = false;
    else if (arg == "--satellite") o.satellite = true;
    else if (arg == "--ares") o.ares = true;
    else if (arg == "--phobos") o.phobos = true;
    else if (arg == "--terrain") o.terrain = true;
    else if (arg == "--no-auto-repair") o.autoRepair = false;
    else if (arg == "--no-corr") o.corr = false;
    else if (arg == "--reverse") o.reverse = true;
}

int Usage() {
    std::fprintf(stderr,
                 "rlctl - Reunion Lite core driver\n"
                 "  rlctl drives\n"
                 "  rlctl defaults <gameRoot>\n"
                 "  rlctl scan    <gameRoot> [--json] [--no-cache] [--limit N]\n"
                 "  rlctl plan    <gameRoot> <query> [--json] [options]\n"
                 "  rlctl prepare <gameRoot> <query> [--json] [options]\n"
                 "  rlctl launch  <gameRoot> <query> [--dry-run] [--bottle=RA2] [options]\n"
                 "\n"
                 "options: --difficulty=N --credits=N --speed=N --autosave=N --music=N\n"
                 "         --mod=ID --no-movies --satellite --ares --phobos --terrain\n"
                 "         --no-auto-repair --no-corr --reverse\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    std::string command = argv[1];

    if (command == "drives") {
        std::printf("%s\n", rl::PlatformDriveMapSummary().c_str());
        return 0;
    }

    if (command == "defaults") {
        if (argc < 3) return Usage();
        const rlmac::ClientDefaults defaults = rlmac::LoadClientDefaults(argv[2]);
        std::printf("found=%d lastMission=%s\n", defaults.found ? 1 : 0,
                    defaults.lastMission.c_str());
        const rl::LaunchOptions& o = defaults.options;
        std::printf("difficulty=%d credits=%d speed=%d autosave=%d music=%d movies=%d\n",
                    o.difficulty, o.credits, o.gameSpeed, o.autosave, o.musicVolume,
                    o.playMovies ? 1 : 0);
        std::printf("satellite=%d ares=%d phobos=%d terrain=%d autorepair=%d corr=%d reverse=%d\n",
                    o.satellite ? 1 : 0, o.ares ? 1 : 0, o.phobos ? 1 : 0, o.terrain ? 1 : 0,
                    o.autoRepair ? 1 : 0, o.corr ? 1 : 0, o.reverse ? 1 : 0);
        return 0;
    }

    if (command == "compact") {
        if (argc < 3) return Usage();
        rl::Campaign campaign;
        LoadCampaign(argv[2], false, campaign);
        rl::LaunchOptions options;
        rl::LaunchPlan plan;
        const rl::Mission* mission = nullptr;
        for (const auto& m : campaign.missions)
            if (m.Launchable()) { mission = &m; break; }
        if (!mission || !rl::BuildLaunchPlan(argv[2], *mission, options, plan, &campaign.missions)) {
            std::fprintf(stderr, "compact: cannot build a base plan\n");
            return 4;
        }
        std::string error;
        std::uint64_t saved = rlmac::CompactBaseHardLinks(plan, error);
        std::printf("reclaimed %.1f MB%s%s\n", saved / (1024.0 * 1024.0),
                    error.empty() ? "" : " error=", error.c_str());
        return error.empty() ? 0 : 5;
    }

    if (command == "scan") {
        if (argc < 3) return Usage();
        std::string root = argv[2];
        bool json = false, useCache = true;
        int limit = 10;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--json") json = true;
            else if (arg == "--no-cache") useCache = false;
            else if (arg.rfind("--limit=", 0) == 0) limit = std::atoi(arg.c_str() + 8);
        }
        rl::Fingerprint fingerprint = rl::FingerprintMapsFolder(root);
        rl::Campaign campaign;
        bool hit = useCache && rl::LoadCampaignCache(rl::DefaultCachePath(root), fingerprint, campaign);
        if (!hit) {
            campaign = rl::ScanCampaigns(root);
            if (useCache) rl::SaveCampaignCache(rl::DefaultCachePath(root), fingerprint, campaign);
        }
        int launchable = 0;
        for (const auto& m : campaign.missions)
            if (m.badReason.empty() && m.Launchable()) ++launchable;

        if (!json) {
            std::fprintf(stderr,
                         "cache=%s fingerprint=%zu files scan=%.1fms packs=%zu missions=%zu "
                         "launchable=%d excluded=%zu\n",
                         hit ? "HIT" : "MISS", fingerprint.files.size(),
                         static_cast<double>(campaign.scanMillis), campaign.packs.size(),
                         campaign.missions.size(), launchable,
                         campaign.missions.size() - static_cast<std::size_t>(launchable));
            int shown = 0;
            for (const auto& m : campaign.missions) {
                if (!m.Launchable()) continue;
                std::printf("[%s] %s | %s | %s | %s | mod=%s\n", m.scenario.c_str(),
                            m.name.c_str(), m.packName.c_str(), m.side.c_str(),
                            m.difficulty.c_str(), m.defaultMod.c_str());
                if (++shown >= limit) break;
            }
            return 0;
        }

        std::printf("{\"scanMillis\":%llu,\"cached\":%s,\"packs\":%zu,\"missions\":[",
                    static_cast<unsigned long long>(campaign.scanMillis), hit ? "true" : "false",
                    campaign.missions.size());
        for (std::size_t i = 0; i < campaign.missions.size(); ++i) {
            std::printf("%s", i == 0 ? "\n" : ",\n");
            PrintMissionJson(campaign.missions[i], false);
        }
        std::printf("\n],\"issues\":[");
        for (std::size_t i = 0; i < campaign.issues.size(); ++i)
            std::printf("%s{\"file\":%s,\"message\":%s}", i == 0 ? "" : ",",
                        Q(campaign.issues[i].file).c_str(),
                        Q(campaign.issues[i].message).c_str());
        std::printf("]}\n");
        return 0;
    }

    if (command == "launch") {
        if (argc < 4) return Usage();
        const std::string root = argv[2];
        const std::string query = argv[3];
        rl::LaunchOptions options = rlmac::LoadClientDefaults(root).options;
        rlmac::VideoConfig video;
        rlmac::WineConfig config;
        if (const char* env = std::getenv("REUNION_CX_ROOT")) config.crossOverRoot = env;
        if (const char* env = std::getenv("REUNION_BOTTLE")) config.bottle = env;
        if (const char* env = std::getenv("REUNION_LANG")) config.lang = env;
        bool dryRun = false;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--dry-run") { dryRun = true; continue; }
            if (arg.rfind("--bottle=", 0) == 0) { config.bottle = arg.substr(9); continue; }
            if (arg.rfind("--cx-root=", 0) == 0) { config.crossOverRoot = arg.substr(10); continue; }
            if (arg.rfind("--lang=", 0) == 0) { config.lang = arg.substr(7); continue; }
            if (arg.rfind("--debug=", 0) == 0) { config.debugChannels = arg.substr(8); continue; }
            ParseWindow(arg, video);
            ParseOption(arg, options);
        }

        rl::Campaign campaign;
        LoadCampaign(root, true, campaign);
        const rl::Mission* mission = FindMission(campaign, query);
        if (!mission) {
            std::fprintf(stderr, "mission not found: %s\n", query.c_str());
            return 3;
        }
        rl::LaunchPlan plan;
        if (!rl::BuildLaunchPlan(root, *mission, options, plan, &campaign.missions)) {
            std::fprintf(stderr, "plan failed: %s\n", plan.error.c_str());
            return 4;
        }
        std::string error;
        std::vector<std::string> warnings;
        if (!rlmac::PrepareRun(plan, options, error, warnings, video)) {
            std::fprintf(stderr, "prepare failed: %s\n", error.c_str());
            return 5;
        }
        rlmac::LaunchSpec spec;
        if (!rlmac::BuildLaunchSpec(plan, config, spec, error)) {
            std::fprintf(stderr, "launch spec failed: %s\n", error.c_str());
            return 6;
        }
        std::printf("log: %s\n%s\n", spec.logPath.c_str(), spec.commandLine.c_str());
        if (dryRun) return 0;
        std::printf("launching %s ...\n", mission->name.c_str());
        std::fflush(stdout);
        const int code = rlmac::RunGame(spec, error);
        if (code < 0) {
            std::fprintf(stderr, "launch failed: %s\n", error.c_str());
            return 7;
        }
        rlmac::SyncSettingsBack(root);
        std::printf("game exited: %d\n", code);
        return code == 0 ? 0 : 8;
    }

    if (command == "plan" || command == "prepare") {
        if (argc < 4) return Usage();
        std::string root = argv[2];
        std::string query = argv[3];
        bool json = false;
        rl::LaunchOptions options = rlmac::LoadClientDefaults(root).options;
        rlmac::VideoConfig video;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--json") {
                json = true;
                continue;
            }
            ParseWindow(arg, video);
            ParseOption(arg, options);
        }

        rl::Campaign campaign;
        LoadCampaign(root, true, campaign);
        const rl::Mission* mission = FindMission(campaign, query);
        if (!mission) {
            std::fprintf(stderr, "mission not found: %s\n", query.c_str());
            return 3;
        }

        rl::LaunchPlan plan;
        bool ok = rl::BuildLaunchPlan(root, *mission, options, plan, &campaign.missions);
        std::string prepareError;
        std::vector<std::string> prepareWarnings;
        bool prepared = false;
        if (ok && command == "prepare") {
            prepared = rlmac::PrepareRun(plan, options, prepareError, prepareWarnings, video);
            if (!prepared) ok = false;
        }
        if (!json) {
            std::printf("mission: %s / %s / %s\n", mission->name.c_str(),
                        mission->section.c_str(), mission->scenario.c_str());
            std::printf("ok=%d error=%s\n", ok ? 1 : 0, plan.error.c_str());
            if (ok) {
                std::printf("mod=%s (%s)\nmodDir=%s\nrunDir=%s\nscenario=%s map=%s loose=%d\n",
                            plan.modId.c_str(), plan.modName.c_str(), plan.modDir.c_str(),
                            plan.runDir.c_str(), plan.scenarioName.c_str(),
                            plan.mapSourcePath.c_str(), plan.hasLooseMap ? 1 : 0);
                std::printf("ares=%d phobos=%d tx=%d files: mod=%zu mission=%zu csf=%zu/%zu\n",
                            plan.useAres ? 1 : 0, plan.usePhobos ? 1 : 0, plan.useTx ? 1 : 0,
                            plan.modFiles.size(), plan.missionFiles.size(),
                            plan.modCsfFiles.size(), plan.missionCsfFiles.size());
                std::printf("---- spawn.ini ----\n%s\n", plan.spawnIni.c_str());
                for (const auto& w : plan.warnings) std::printf("warning: %s\n", w.c_str());
            }
            if (command == "prepare")
                std::printf("prepared=%d error=%s warnings=%zu\n", prepared ? 1 : 0,
                            prepareError.c_str(), prepareWarnings.size());
            return ok ? 0 : 4;
        }

        std::printf("{\"ok\":%s,\"error\":%s,\"prepared\":%s,\"prepareError\":%s,"
                    "\"mission\":", ok ? "true" : "false",
                    Q(plan.error.empty() ? prepareError : plan.error).c_str(),
                    prepared ? "true" : "false", Q(prepareError).c_str());
        PrintMissionJson(*mission, false);
        std::printf(",\"plan\":{\"modId\":%s,\"modName\":%s,\"modDir\":%s,\"gamePath\":%s,"
                    "\"runDir\":%s,\"scenario\":%s,\"mapSource\":%s,\"looseMap\":%s,"
                    "\"ares\":%s,\"phobos\":%s,\"tx\":%s,\"spawnIni\":%s,\"extraArgs\":%s,"
                    "\"warnings\":[",
                    Q(plan.modId).c_str(), Q(plan.modName).c_str(), Q(plan.modDir).c_str(),
                    Q(plan.modGamePath).c_str(), Q(plan.runDir).c_str(),
                    Q(plan.scenarioName).c_str(), Q(plan.mapSourcePath).c_str(),
                    plan.hasLooseMap ? "true" : "false", plan.useAres ? "true" : "false",
                    plan.usePhobos ? "true" : "false", plan.useTx ? "true" : "false",
                    Q(plan.spawnIni).c_str(), Q(plan.extraArgs).c_str());
        {
            std::vector<std::string> allWarnings = plan.warnings;
            allWarnings.insert(allWarnings.end(), prepareWarnings.begin(), prepareWarnings.end());
            for (std::size_t i = 0; i < allWarnings.size(); ++i)
                std::printf("%s%s", i == 0 ? "" : ",", Q(allWarnings[i]).c_str());
        }
        std::printf("]}}\n");
        return ok ? 0 : 4;
    }

    return Usage();
}
