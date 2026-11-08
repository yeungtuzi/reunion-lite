#include "rl_api.h"

#include "core/campaign.h"
#include "core/launch.h"
#include "core/platform_path.h"
#include "platform/mac/client_config.h"
#include "platform/mac/launch.h"
#include "platform/mac/runprep.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

struct rl_context {
    std::string root;
    rl::Campaign campaign;
    std::vector<rl::ModInfo> mods;
    bool loaded = false;
};

namespace {

std::string Escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
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
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string Str(const std::string& value) { return "\"" + Escape(value) + "\""; }
std::string Str(const char* value) { return value ? Str(std::string(value)) : "\"\""; }

std::string Bool(bool value) { return value ? "true" : "false"; }

char* CopyOut(const std::string& json) {
    char* result = static_cast<char*>(std::malloc(json.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, json.data(), json.size());
    result[json.size()] = '\0';
    return result;
}

void EnsureLoaded(rl_context* ctx, bool force) {
    if (!ctx || ctx->loaded) {
        if (ctx && force) {
            /* fall through and rescan */
        } else {
            return;
        }
    }
    const std::string cache = rl::DefaultCachePath(ctx->root);
    rl::Fingerprint fingerprint = rl::FingerprintMapsFolder(ctx->root);
    bool hit = false;
    if (!force) hit = rl::LoadCampaignCache(cache, fingerprint, ctx->campaign);
    if (!hit) {
        ctx->campaign = rl::ScanCampaigns(ctx->root);
        rl::SaveCampaignCache(cache, fingerprint, ctx->campaign);
    }
    ctx->mods = rl::LoadModRegistry(ctx->root);
    ctx->loaded = true;
}

std::string MissionJson(const rl::Mission& m, const std::vector<rl::ModInfo>& registry,
                        int ordinal) {
    std::string out = "{";
    out += "\"ordinal\":" + std::to_string(ordinal);
    out += ",\"index\":" + std::to_string(m.index);
    out += ",\"name\":" + Str(m.name);
    out += ",\"description\":" + Str(m.description);
    out += ",\"pack\":" + Str(m.packName);
    out += ",\"packId\":" + Str(m.packId);
    out += ",\"scenario\":" + Str(m.scenario);
    out += ",\"path\":" + Str(m.path);
    out += ",\"side\":" + Str(m.side);
    out += ",\"sideValue\":" + std::to_string(m.sideValue);
    out += ",\"difficulty\":" + Str(m.difficulty);
    out += ",\"modList\":" + Str(m.modList);
    out += ",\"defaultMod\":" + Str(m.defaultMod);
    out += ",\"section\":" + Str(m.section);
    out += ",\"sourceFile\":" + Str(m.sourceFile);
    out += ",\"badReason\":" + Str(m.badReason);
    out += ",\"ares\":" + Bool(m.ares == 1);
    out += ",\"phobos\":" + Bool(m.phobos == 1);
    out += ",\"tx\":" + Bool(m.tx == 1);
    out += ",\"yr\":" + Bool(m.yr);
    out += ",\"buildOffAlly\":" + Bool(m.buildOffAlly);
    out += ",\"alwaysNormal\":" + Bool(m.alwaysNormal);
    out += ",\"launchable\":" + Bool(m.Launchable());

    out += ",\"mods\":[";
    const std::vector<rl::ModInfo> mods = rl::ModsForMission(registry, m);
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (i) out += ",";
        out += "{\"id\":" + Str(mods[i].id) + ",\"name\":" + Str(mods[i].name) + "}";
    }
    out += "]}";
    return out;
}


namespace {

void PutU8(std::string& out, unsigned value) { out.push_back(static_cast<char>(value & 0xFF)); }
void PutU32(std::string& out, unsigned value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}
void PutStr(std::string& out, const std::string& value) {
    const unsigned length = static_cast<unsigned>(std::min<std::size_t>(value.size(), 65535));
    out.push_back(static_cast<char>(length & 0xFF));
    out.push_back(static_cast<char>((length >> 8) & 0xFF));
    out.append(value.data(), length);
}

} // namespace

} // namespace

extern "C" {

rl_context* rl_create(const char* game_root_utf8) {
    auto* ctx = new rl_context();
    ctx->root = rl::MapPlatformPath(game_root_utf8 ? game_root_utf8 : "");
    return ctx;
}

void rl_destroy(rl_context* ctx) { delete ctx; }

void rl_free_string(char* string) { std::free(string); }

char* rl_scan_json(rl_context* ctx, int force_rescan) {
    if (!ctx) return CopyOut("{\"ok\":false,\"error\":\"no context\"}");
    EnsureLoaded(ctx, force_rescan != 0);
    const rl::Campaign& campaign = ctx->campaign;

    std::string out = "{\"ok\":true,\"gameRoot\":" + Str(ctx->root);
    out += ",\"scanMillis\":" + std::to_string(campaign.scanMillis);
    out += ",\"missions\":[";
    for (std::size_t i = 0; i < campaign.missions.size(); ++i) {
        if (i) out += ",";
        out += MissionJson(campaign.missions[i], ctx->mods, static_cast<int>(i));
    }
    out += "],\"packs\":[";
    std::vector<std::string> seen;
    bool first = true;
    for (const rl::MissionPack& pack : campaign.packs) {
        bool duplicate = false;
        for (const std::string& name : seen)
            if (name == pack.name) duplicate = true;
        if (duplicate) continue;
        seen.push_back(pack.name);
        if (!first) out += ",";
        first = false;
        out += "{\"id\":" + Str(pack.id) + ",\"name\":" + Str(pack.name) + "}";
    }
    out += "],\"issues\":[";
    for (std::size_t i = 0; i < campaign.issues.size(); ++i) {
        if (i) out += ",";
        out += "{\"file\":" + Str(campaign.issues[i].file) +
               ",\"message\":" + Str(campaign.issues[i].message) + "}";
    }
    out += "]}";
    return CopyOut(out);
}

char* rl_client_defaults_json(const char* game_root_utf8) {
    const rlmac::ClientDefaults defaults =
        rlmac::LoadClientDefaults(game_root_utf8 ? game_root_utf8 : "");
    const rl::LaunchOptions& o = defaults.options;
    std::string out = "{\"ok\":true,\"found\":" + Bool(defaults.found);
    out += ",\"lastMission\":" + Str(defaults.lastMission);
    out += ",\"options\":{\"difficulty\":" + std::to_string(o.difficulty);
    out += ",\"credits\":" + std::to_string(o.credits);
    out += ",\"gameSpeed\":" + std::to_string(o.gameSpeed);
    out += ",\"autosave\":" + std::to_string(o.autosave);
    out += ",\"musicVolume\":" + std::to_string(o.musicVolume);
    out += ",\"playMovies\":" + Bool(o.playMovies);
    out += ",\"satellite\":" + Bool(o.satellite);
    out += ",\"ares\":" + Bool(o.ares);
    out += ",\"phobos\":" + Bool(o.phobos);
    out += ",\"terrain\":" + Bool(o.terrain);
    out += ",\"autoRepair\":" + Bool(o.autoRepair);
    out += ",\"corr\":" + Bool(o.corr);
    out += ",\"reverse\":" + Bool(o.reverse);
    out += "},\"drives\":" + Str(rl::PlatformDriveMapSummary()) + "}";
    return CopyOut(out);
}

char* rl_drives_json(void) {
    std::string out = "{\"ok\":true,\"drives\":" + Str(rl::PlatformDriveMapSummary()) + "}";
    return CopyOut(out);
}


char* rl_prepare_json(rl_context* ctx, int mission_ordinal, const rl_options* options,
                      const rl_video* video) {
    if (!ctx) return CopyOut("{\"ok\":false,\"error\":\"no context\"}");
    EnsureLoaded(ctx, false);
    const rl::Mission* mission = nullptr;
    if (mission_ordinal >= 0 &&
        static_cast<std::size_t>(mission_ordinal) < ctx->campaign.missions.size()) {
        mission = &ctx->campaign.missions[static_cast<std::size_t>(mission_ordinal)];
    }
    if (!mission) return CopyOut("{\"ok\":false,\"error\":\"mission not found\"}");

    rl::LaunchOptions launchOptions;
    if (options) {
        if (options->mod_id && *options->mod_id) launchOptions.modId = options->mod_id;
        launchOptions.difficulty = options->difficulty;
        launchOptions.credits = options->credits;
        launchOptions.gameSpeed = options->game_speed;
        launchOptions.autosave = options->autosave;
        launchOptions.musicVolume = options->music_volume;
        launchOptions.playMovies = options->play_movies != 0;
        launchOptions.satellite = options->satellite != 0;
        launchOptions.ares = options->ares != 0;
        launchOptions.phobos = options->phobos != 0;
        launchOptions.terrain = options->terrain != 0;
        launchOptions.autoRepair = options->auto_repair != 0;
        launchOptions.corr = options->corr != 0;
        launchOptions.reverse = options->reverse != 0;
    }
    rlmac::VideoConfig videoConfig;
    if (video) {
        if (video->width >= 640) videoConfig.width = video->width;
        if (video->height >= 480) videoConfig.height = video->height;
        if (video->renderer && *video->renderer) videoConfig.renderer = video->renderer;
    }
    rl::LaunchPlan plan;
    if (!rl::BuildLaunchPlan(ctx->root, *mission, launchOptions, plan,
                             &ctx->campaign.missions)) {
        return CopyOut("{\"ok\":false,\"error\":" + Str(plan.error) + "}");
    }
    std::string error;
    std::vector<std::string> warnings;
    if (!rlmac::PrepareRun(plan, launchOptions, error, warnings, videoConfig)) {
        return CopyOut("{\"ok\":false,\"error\":" + Str(error) + "}");
    }
    std::string out = "{\"ok\":true,\"error\":\"\"";
    out += ",\"runDir\":" + Str(rl::MapPlatformPath(plan.runDir));
    out += ",\"scenario\":" + Str(plan.scenarioName);
    out += ",\"gameRoot\":" + Str(ctx->root);
    out += ",\"mission\":" + MissionJson(*mission, ctx->mods,
        static_cast<int>(mission - ctx->campaign.missions.data()));
    out += ",\"plan\":{\"modId\":" + Str(plan.modId);
    out += ",\"modName\":" + Str(plan.modName);
    out += ",\"scenario\":" + Str(plan.scenarioName);
    out += ",\"runDir\":" + Str(rl::MapPlatformPath(plan.runDir));
    out += ",\"ares\":" + Bool(plan.useAres);
    out += ",\"phobos\":" + Bool(plan.usePhobos);
    out += ",\"tx\":" + Bool(plan.useTx);
    out += ",\"warnings\":[";
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) out += ",";
        out += Str(warnings[i]);
    }
    out += "]}}";
    return CopyOut(out);
}

char* rl_launch_json(rl_context* ctx, int mission_index, const rl_options* options,
                     const rl_video* video, const rl_wine* wine) {
    if (!ctx) return CopyOut("{\"ok\":false,\"error\":\"no context\"}");
    EnsureLoaded(ctx, false);

    // `mission_index` is the ordinal inside campaign.missions: the per-pack
    // Mission::index is not unique and cannot identify a mission.
    const rl::Mission* mission = nullptr;
    if (mission_index >= 0 &&
        static_cast<std::size_t>(mission_index) < ctx->campaign.missions.size()) {
        mission = &ctx->campaign.missions[static_cast<std::size_t>(mission_index)];
    }
    if (!mission) return CopyOut("{\"ok\":false,\"error\":\"mission not found\"}");

    rl::LaunchOptions launchOptions;
    if (options) {
        if (options->mod_id && *options->mod_id) launchOptions.modId = options->mod_id;
        launchOptions.difficulty = options->difficulty;
        launchOptions.credits = options->credits;
        launchOptions.gameSpeed = options->game_speed;
        launchOptions.autosave = options->autosave;
        launchOptions.musicVolume = options->music_volume;
        launchOptions.playMovies = options->play_movies != 0;
        launchOptions.satellite = options->satellite != 0;
        launchOptions.ares = options->ares != 0;
        launchOptions.phobos = options->phobos != 0;
        launchOptions.terrain = options->terrain != 0;
        launchOptions.autoRepair = options->auto_repair != 0;
        launchOptions.corr = options->corr != 0;
        launchOptions.reverse = options->reverse != 0;
    }

    rlmac::VideoConfig videoConfig;
    if (video) {
        if (video->width >= 640) videoConfig.width = video->width;
        if (video->height >= 480) videoConfig.height = video->height;
        if (video->renderer && *video->renderer) videoConfig.renderer = video->renderer;
    }

    rlmac::WineConfig wineConfig;
    if (wine) {
        if (wine->crossover_root && *wine->crossover_root)
            wineConfig.crossOverRoot = wine->crossover_root;
        if (wine->bottle && *wine->bottle) wineConfig.bottle = wine->bottle;
        if (wine->lang && *wine->lang) wineConfig.lang = wine->lang;
        if (wine->log_dir && *wine->log_dir) wineConfig.logDir = wine->log_dir;
    }

    rl::LaunchPlan plan;
    if (!rl::BuildLaunchPlan(ctx->root, *mission, launchOptions, plan,
                             &ctx->campaign.missions)) {
        std::string out = "{\"ok\":false,\"error\":" + Str(plan.error) + "}";
        return CopyOut(out);
    }

    std::string error;
    std::vector<std::string> warnings;
    if (!rlmac::PrepareRun(plan, launchOptions, error, warnings, videoConfig)) {
        std::string out = "{\"ok\":false,\"error\":" + Str(error) + "}";
        return CopyOut(out);
    }

    rlmac::LaunchSpec spec;
    if (!rlmac::BuildLaunchSpec(plan, wineConfig, spec, error)) {
        std::string out = "{\"ok\":false,\"error\":" + Str(error) + "}";
        return CopyOut(out);
    }

    std::string out = "{\"ok\":true,\"error\":\"\"";
    out += ",\"mission\":" + MissionJson(*mission, ctx->mods,
        static_cast<int>(mission - ctx->campaign.missions.data()));
    out += ",\"plan\":{\"modId\":" + Str(plan.modId);
    out += ",\"modName\":" + Str(plan.modName);
    out += ",\"scenario\":" + Str(plan.scenarioName);
    out += ",\"runDir\":" + Str(rl::MapPlatformPath(plan.runDir));
    out += ",\"ares\":" + Bool(plan.useAres);
    out += ",\"phobos\":" + Bool(plan.usePhobos);
    out += ",\"tx\":" + Bool(plan.useTx);
    out += ",\"warnings\":[";
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) out += ",";
        out += Str(warnings[i]);
    }
    out += "]}";

    out += ",\"program\":" + Str(spec.program);
    out += ",\"workDir\":" + Str(spec.workDir);
    out += ",\"logPath\":" + Str(spec.logPath);
    out += ",\"command\":" + Str(spec.commandLine);
    out += ",\"args\":[";
    /* spec.args[0] is the wine path itself (argv[0]); Process passes it as the
       executable, so skip it. */
    for (std::size_t i = 1; i < spec.args.size(); ++i) {
        if (i > 1) out += ",";
        out += Str(spec.args[i]);
    }
    out += "],\"env\":{";
    bool firstEnv = true;
    for (const auto& [key, value] : spec.env) {
        if (!firstEnv) out += ",";
        firstEnv = false;
        out += Str(key) + ":" + Str(value);
    }
    out += "}}";
    return CopyOut(out);
}


unsigned char* rl_scan_binary(rl_context* ctx, int force_rescan, unsigned long* out_size) {
    if (out_size) *out_size = 0;
    if (!ctx) return nullptr;
    const rl::Fingerprint fingerprint = rl::FingerprintMapsFolder(ctx->root);
    const std::string cachePath = rl::DefaultCachePath(ctx->root);
    bool cached = false;
    if (!force_rescan && !ctx->loaded) {
        cached = rl::LoadCampaignCache(cachePath, fingerprint, ctx->campaign);
        if (cached) {
            ctx->mods = rl::LoadModRegistry(ctx->root);
            ctx->loaded = true;
        }
    } else if (ctx->loaded) {
        cached = true;
    }
    if (!ctx->loaded) {
        ctx->campaign = rl::ScanCampaigns(ctx->root);
        rl::SaveCampaignCache(cachePath, fingerprint, ctx->campaign);
        ctx->mods = rl::LoadModRegistry(ctx->root);
        ctx->loaded = true;
        cached = false;
    }
    const rl::Campaign& campaign = ctx->campaign;

    std::string out;
    out.reserve(1024 * 1024);
    out.append("RLMB");
    PutU32(out, 1);
    PutU32(out, static_cast<unsigned>(campaign.scanMillis));
    PutU8(out, cached ? 1 : 0);

    PutU32(out, static_cast<unsigned>(campaign.issues.size()));
    for (const rl::ScanIssue& issue : campaign.issues) {
        PutStr(out, issue.file);
        PutStr(out, issue.message);
    }

    PutU32(out, static_cast<unsigned>(campaign.packs.size()));
    for (const rl::MissionPack& pack : campaign.packs) {
        PutStr(out, pack.id);
        PutStr(out, pack.name);
    }

    PutU32(out, static_cast<unsigned>(campaign.missions.size()));
    for (const rl::Mission& m : campaign.missions) {
        PutU32(out, static_cast<unsigned>(m.index));
        PutStr(out, m.name);
        PutStr(out, m.description);
        PutStr(out, m.packName);
        PutStr(out, m.packId);
        PutStr(out, m.scenario);
        PutStr(out, m.path);
        PutStr(out, m.side);
        PutU32(out, static_cast<unsigned>(m.sideValue));
        PutStr(out, m.difficulty);
        PutStr(out, m.modList);
        PutStr(out, m.defaultMod);
        PutStr(out, m.section);
        PutStr(out, m.sourceFile);
        PutStr(out, m.badReason);
        unsigned flags = 0;
        if (m.ares == 1) flags |= 1u << 0;
        if (m.phobos == 1) flags |= 1u << 1;
        if (m.tx == 1) flags |= 1u << 2;
        if (m.yr) flags |= 1u << 3;
        if (m.buildOffAlly) flags |= 1u << 4;
        if (m.alwaysNormal) flags |= 1u << 5;
        if (m.Launchable()) flags |= 1u << 6;
        PutU8(out, flags);
        const std::vector<rl::ModInfo> mods = rl::ModsForMission(ctx->mods, m);
        PutU32(out, static_cast<unsigned>(mods.size()));
        for (const rl::ModInfo& mod : mods) {
            PutStr(out, mod.id);
            PutStr(out, mod.name);
        }
    }

    if (out_size) *out_size = static_cast<unsigned long>(out.size());
    auto* buffer = static_cast<unsigned char*>(std::malloc(out.size()));
    if (!buffer) return nullptr;
    std::memcpy(buffer, out.data(), out.size());
    return buffer;
}

char* rl_sync_json(rl_context* ctx) {
    if (ctx) rlmac::SyncSettingsBack(ctx->root);
    return CopyOut("{\"ok\":true}");
}

} // extern "C"
