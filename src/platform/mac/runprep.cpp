#include "platform/mac/runprep.h"

#include "core/campaign.h"
#include "core/ini.h"
#include "core/encoding.h"
#include "core/platform_path.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;

namespace rlmac {
namespace {

const char* kOverlayManifest = ".rl_overlay";
const char* kBaseManifest = ".rl_base";
const char* kSaveMarker = ".rl_saves";

fs::path P(const std::string& utf8) { return fs::u8path(rl::MapPlatformPath(utf8)); }

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

bool Exists(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(P(p), ec);
}

bool EnsureDir(const std::string& p) {
    std::error_code ec;
    fs::create_directories(P(p), ec);
    return !ec;
}

bool RemoveFile(const std::string& p) {
    std::error_code ec;
    fs::remove(P(p), ec);
    return !ec;
}

bool WriteBytes(const std::string& p, const void* data, std::size_t size) {
    std::ofstream f(P(p), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

bool ReadBytes(const std::string& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(P(p), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string ReadTextAuto(const std::string& p) {
    std::vector<std::uint8_t> bytes;
    if (!ReadBytes(p, bytes)) return {};
    return rl::DecodeAnsiToUtf8(bytes);
}

bool WriteTextAnsi(const std::string& p, const std::string& utf8) {
    std::vector<std::uint8_t> bytes = rl::EncodeUtf8ToAnsi(utf8);
    return WriteBytes(p, bytes.data(), bytes.size());
}

bool LinkOrCopy(const std::string& from, const std::string& to, bool forceCopy) {
    if (!Exists(from)) return false;
    if (!forceCopy) {
        std::error_code ec;
        fs::create_hard_link(P(from), P(to), ec);
        if (!ec) return true;
    }
    std::error_code ec;
    fs::copy_file(P(from), P(to), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

struct MappedFile {
    const char* sourceRel;
    const char* targetRel;
};

const MappedFile kBaseFiles[] = {
    {"YR/gamemd.exe", "gamemd.exe"},
    {"YR/ra2.mix", "ra2.mix"},
    {"YR/ra2md.mix", "ra2md.mix"},
    {"YR/MAPSMD03.MIX", "MAPSMD03.MIX"},
    {"YR/MULTIMD.MIX", "MULTIMD.MIX"},
    {"YR/expandmd01.mix", "expandmd01.mix"},
    {"YR/movmd03.mix", "movmd03.mix"},
    {"YR/BINKW32.DLL", "BINKW32.DLL"},
    {"YR/Blowfish.dll", "Blowfish.dll"},
    {"zh/game.fnt", "game.fnt"},
    {"zh/langmd.mix", "langmd.mix"},
    {"zh/language.mix", "language.mix"},
    {"zh/subtitle.txt", "subtitle.txt"},
    {"zh/subtitlemd.txt", "subtitlemd.txt"},
    {"cncnet5.dll", "cncnet5.dll"},
    {"qres.dat", "qres.dat"},
    {"qres32.dll", "qres32.dll"},
    {"KeyboardMD.ini", "KeyboardMD.ini"},
    {"Resources/Voice/English/audiomd.mix", "audiomd.mix"},
};

std::vector<std::string> ReadLines(const std::string& p) {
    std::vector<std::string> lines;
    std::ifstream f(P(p), std::ios::binary);
    if (!f) return lines;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

bool WriteLines(const std::string& p, const std::vector<std::string>& lines) {
    std::ofstream f(P(p), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (const auto& l : lines) f << l << "\r\n";
    return static_cast<bool>(f);
}

bool IsSaveFile(const std::string& name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext == ".sav";
}

std::string ParentDir(const std::string& path) {
    std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

void CopyRenderer(const std::string& root, const std::string& run) {
    std::string renderer = "CnC_DDraw_Experimental";
    std::string settings = ReadTextAuto(Join(root, "RA2MD.ini"));
    if (!settings.empty()) {
        rl::IniDocument ini;
        ini.Parse(settings);
        renderer = ini.Get("Compatibility", "Renderer", renderer);
    }
    std::string dir = Join(root, "Resources/Render/" + renderer);
    std::error_code ec;
    if (!fs::is_directory(P(dir), ec)) return;
    for (fs::directory_iterator it(P(dir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string ext = it->path().extension().u8string();
        std::string target = Join(run, "ddraw" + ext);
        if (Exists(target)) continue;
        std::error_code ec3;
        fs::copy_file(it->path(), P(target), fs::copy_options::overwrite_existing, ec3);
    }
}

bool EnsureBase(const std::string& root, const std::string& run, std::string& error) {
    if (!EnsureDir(run)) {
        error = "无法创建 Run 目录：" + run;
        return false;
    }
    std::string marker = Join(run, kBaseManifest);
    bool baseOk = Exists(marker) && Exists(Join(run, "gamemd.exe")) && Exists(Join(run, "ra2.mix")) &&
                  Exists(Join(run, "language.mix")) && Exists(Join(run, "gamemd-spawn.exe"));
    if (baseOk) return true;

    for (const auto& item : kBaseFiles) {
        std::string source = Join(root, item.sourceRel);
        std::string target = Join(run, item.targetRel);
        if (!Exists(source) || Exists(target)) continue;
        if (!LinkOrCopy(source, target, false)) {
            error = "复制基础文件失败：" + std::string(item.sourceRel);
            return false;
        }
    }

    if (!Exists(Join(run, "gamemd-spawn.exe"))) {
        if (!LinkOrCopy(Join(root, "gamemd-spawn.exe"), Join(run, "gamemd-spawn.exe"), true)) {
            error = "缺少 gamemd-spawn.exe";
            return false;
        }
    }

    if (!Exists(Join(run, "RA2MD.ini")) && Exists(Join(root, "RA2MD.ini")))
        LinkOrCopy(Join(root, "RA2MD.ini"), Join(run, "RA2MD.ini"), true);

    if (!Exists(Join(run, "spawnmap.ini"))) {
        if (Exists(Join(root, "spawnmap.ini"))) {
            LinkOrCopy(Join(root, "spawnmap.ini"), Join(run, "spawnmap.ini"), true);
        } else {
            const char* text = "[Map]\r\nSize=0,0,50,50\r\nLocalSize=0,0,50,50\r\n";
            WriteBytes(Join(run, "spawnmap.ini"), text, std::strlen(text));
        }
    }

    if (!Exists(Join(run, "ddraw.dll"))) CopyRenderer(root, run);
    EnsureDir(Join(run, "Saved Games"));
    WriteLines(marker, {"ReunionLite base"});
    return true;
}

void BackupMusicFiles(const std::string& run) {
    std::string keep = Join(run, ".rl_base_keep");
    EnsureDir(keep);
    for (const char* name : {"thememd.mix", "thememd.ini"}) {
        std::string source = Join(run, name);
        std::string target = Join(keep, name);
        if (Exists(source) && !Exists(target)) LinkOrCopy(source, target, false);
    }
}

void RestoreMusicFiles(const std::string& run) {
    std::string keep = Join(run, ".rl_base_keep");
    for (const char* name : {"thememd.mix", "thememd.ini"}) {
        std::string source = Join(keep, name);
        std::string target = Join(run, name);
        if (Exists(source) && !Exists(target)) LinkOrCopy(source, target, false);
    }
}

bool ClearOverlay(const std::string& run) {
    for (const std::string& rel : ReadLines(Join(run, kOverlayManifest))) RemoveFile(Join(run, rel));
    RemoveFile(Join(run, kOverlayManifest));
    RestoreMusicFiles(run);
    return true;
}

void ClearStaleRuntimeFiles(const std::string& run) {
    std::error_code ec;
    for (fs::directory_iterator it(P(run), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        std::string lower = name;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        bool staleMap = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".map";
        bool staleCsf = lower == "ra2md.csf" || lower == "ra2.csf";
        if (staleMap || staleCsf) {
            std::error_code rm;
            fs::remove(it->path(), rm);
        }
    }
}

bool CopyDirFiles(const std::string& sourceDir, const std::string& run,
                  std::vector<std::string>& created, std::string& error, bool onlyMix = false) {
    std::error_code ec;
    if (!fs::is_directory(P(sourceDir), ec)) return true;
    for (fs::directory_iterator it(P(sourceDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (onlyMix) {
            std::string lower = name;
            for (char& c : lower)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".mix") continue;
        }
        std::string target = Join(run, name);
        RemoveFile(target);
        if (!LinkOrCopy(it->path().u8string(), target, false)) {
            error = "写入文件失败：" + name;
            return false;
        }
        created.push_back(name);
    }
    return true;
}


std::string TrimCopy(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool SetIniKeysAnsi(const std::string& path, const std::string& section,
                    const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::vector<std::uint8_t> bytes;
    if (!ReadBytes(path, bytes)) return false;
    const std::string text = rl::DecodeAnsiToUtf8(bytes);
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }

    const std::string header = "[" + section + "]";
    std::vector<std::string> out;
    std::vector<bool> applied(pairs.size(), false);
    bool inSection = false;
    bool sawSection = false;
    for (const std::string& line : lines) {
        const std::string trimmed = TrimCopy(line);
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            inSection = rl::IniEquals(trimmed, header);
            out.push_back(line);
            if (inSection) {
                sawSection = true;
                for (std::size_t i = 0; i < pairs.size(); ++i) {
                    if (applied[i]) continue;
                    out.push_back(pairs[i].first + "=" + pairs[i].second);
                    applied[i] = true;
                }
            }
            continue;
        }
        if (inSection) {
            const std::size_t eq = trimmed.find('=');
            if (eq != std::string::npos) {
                const std::string key = TrimCopy(trimmed.substr(0, eq));
                bool replaced = false;
                for (std::size_t i = 0; i < pairs.size(); ++i) {
                    if (!rl::IniEquals(key, pairs[i].first)) continue;
                    out.push_back(pairs[i].first + "=" + pairs[i].second);
                    applied[i] = true;
                    replaced = true;
                    break;
                }
                if (replaced) continue;
            }
        }
        out.push_back(line);
    }
    if (!sawSection) {
        out.push_back(header);
        for (std::size_t i = 0; i < pairs.size(); ++i) out.push_back(pairs[i].first + "=" + pairs[i].second);
    }
    std::string serialized;
    for (const std::string& line : out) serialized += line + "\r\n";
    return WriteTextAnsi(path, serialized);
}

bool ApplyVideoProfile(const std::string& root, const std::string& run, const VideoConfig& video) {
    const std::string width = std::to_string(video.width);
    const std::string height = std::to_string(video.height);
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"Video.Windowed", "False"},
        {"NoWindowFrame", "False"},
        {"AllowHiResModes", "yes"},
        {"ScreenWidth", width},
        {"ScreenHeight", height},
        {"ClientResolutionX", width},
        {"ClientResolutionY", height},
        {"BorderlessWindowedClient", "False"},
        {"StretchMovies", "no"},
    };
    if (!SetIniKeysAnsi(Join(run, "RA2MD.ini"), "Video", pairs)) return false;
    SetIniKeysAnsi(Join(root, "RA2MD.ini"), "Video", pairs); // best effort

    std::string ddraw;
    ddraw += "[ddraw]\r\n";
    ddraw += "; ReunionLite macOS profile: cnc-ddraw presents a window, the game\n";
    ddraw += "; itself stays in its 16-bit fullscreen code path (required by YR).\r\n";
    // Explicit window size equal to the game resolution: 1:1, no upscaling.
    // (width=0 makes cnc-ddraw follow the mode-change request and leaves the
    // window unmapped on macOS, so keep it explicit.)
    ddraw += "width=" + width + "\r\n";
    ddraw += "height=" + height + "\r\n";
    ddraw += "fullscreen=false\r\n";
    ddraw += "windowed=true\r\n";
    ddraw += "maintas=false\r\n";
    ddraw += "aspect_ratio=\r\n";
    ddraw += "boxing=false\r\n";
    ddraw += "maxfps=60\r\n";
    ddraw += "vsync=false\r\n";
    ddraw += "adjmouse=true\r\n";
    ddraw += "shader=\r\n";
    ddraw += "renderer=" + video.renderer + "\r\n";
    ddraw += "devmode=false\r\n";
    ddraw += "border=true\r\n";
    ddraw += "savesettings=0\r\n";
    ddraw += "resizable=false\r\n";
    ddraw += "posX=-32000\r\n";
    ddraw += "posY=-32000\r\n";
    ddraw += "center_window=1\r\n";
    ddraw += "d3d9_filter=2\r\n";
    ddraw += "noactivateapp=false\r\n";
    ddraw += "no_compat_warning=true\r\n";
    return WriteTextAnsi(Join(run, "ddraw.ini"), ddraw);
}

void PrepareSaves(const std::string& run, const rl::LaunchPlan& plan) {
    std::string savesDir = Join(run, "Saved Games");
    if (!EnsureDir(savesDir)) return;
    std::string packFolder = P(plan.missionDir).filename().u8string();
    if (packFolder.empty()) packFolder = "Other";
    std::string packDir = Join(savesDir, packFolder);
    EnsureDir(packDir);

    std::vector<std::string> markerLines{packFolder};
    std::error_code ec;
    for (fs::directory_iterator it(P(savesDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        markerLines.push_back(name + "|" + std::to_string(fs::file_size(it->path(), ec2)) + "|" +
                              std::to_string(static_cast<long long>(fs::last_write_time(it->path(), ec2).time_since_epoch().count())));
    }
    for (fs::directory_iterator it(P(packDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        std::error_code ec3;
        fs::copy_file(it->path(), P(Join(savesDir, name)), fs::copy_options::overwrite_existing, ec3);
    }
    WriteLines(Join(run, kSaveMarker), markerLines);
}

} // namespace

std::uint64_t CompactBaseHardLinks(const rl::LaunchPlan& plan, std::string& error) {
    error.clear();
    std::uint64_t saved = 0;
    for (const auto& item : kBaseFiles) {
        const std::string source = Join(plan.gameRoot, item.sourceRel);
        const std::string target = Join(plan.runDir, item.targetRel);
        if (!Exists(source) || !Exists(target)) continue;
        struct stat srcStat {}, dstStat {};
        if (::stat(P(source).c_str(), &srcStat) != 0 || ::stat(P(target).c_str(), &dstStat) != 0)
            continue;
        if (srcStat.st_dev == dstStat.st_dev && srcStat.st_ino == dstStat.st_ino) continue;
        if (srcStat.st_size != dstStat.st_size) continue;
        std::error_code ec;
        fs::remove(P(target), ec);
        if (ec) continue;
        if (!LinkOrCopy(source, target, false)) {
            if (error.empty()) error = "无法硬链接：" + std::string(item.targetRel);
            continue;
        }
        saved += static_cast<std::uint64_t>(srcStat.st_size);
    }
    return saved;
}

bool PrepareRun(const rl::LaunchPlan& plan, const rl::LaunchOptions& options, std::string& error,
                std::vector<std::string>& warnings, const VideoConfig& video) {
    error.clear();
    warnings.clear();
    const std::string& root = plan.gameRoot;
    const std::string& run = plan.runDir;

    if (!EnsureBase(root, run, error)) return false;
    BackupMusicFiles(run);
    ClearOverlay(run);
    if (!EnsureBase(root, run, error)) return false;
    ClearStaleRuntimeFiles(run);

    std::vector<std::string> created;
    auto fail = [&](const std::string& message) {
        for (const auto& rel : created) RemoveFile(Join(run, rel));
        error = message;
        return false;
    };
    auto addFile = [&](const std::string& source, const std::string& rel, bool forceCopy) -> bool {
        std::string target = Join(run, rel);
        std::string parent = ParentDir(target);
        if (!parent.empty() && !EnsureDir(parent)) return false;
        RemoveFile(target);
        if (!LinkOrCopy(source, target, forceCopy)) return false;
        created.push_back(rel);
        return true;
    };

    for (const std::string& rel : plan.modFiles)
        if (!addFile(Join(plan.modDir, rel), rel, false)) return fail("无法载入模组文件：" + rel);
    for (const std::string& rel : plan.modCsfFiles) {
        std::string name = rel.substr(rel.find_last_of("\\/") == std::string::npos ? 0 : rel.find_last_of("\\/") + 1);
        if (!addFile(Join(plan.modDir, rel), name, true)) return fail("无法载入模组 CSF：" + name);
    }
    for (const std::string& rel : plan.missionFiles)
        if (!addFile(Join(plan.missionDir, rel), rel, false)) return fail("无法载入任务文件：" + rel);
    for (const std::string& rel : plan.missionCsfFiles) {
        std::string name = rel.substr(rel.find_last_of("\\/") == std::string::npos ? 0 : rel.find_last_of("\\/") + 1);
        if (!addFile(Join(plan.missionDir, rel), name, true)) return fail("无法载入任务 CSF：" + name);
    }

    for (const auto& sibling : plan.siblingMaps) {
        rl::LaunchPlan tmp = plan;
        tmp.mapSourcePath = sibling.sourcePath;
        tmp.scenarioName = sibling.scenario;
        std::vector<std::uint8_t> bytes;
        std::string mapError;
        if (!rl::BuildPatchedMap(tmp, options, bytes, mapError)) {
            if (!ReadBytes(sibling.sourcePath, bytes)) continue;
        }
        if (!WriteBytes(Join(run, sibling.scenario), bytes.data(), bytes.size())) continue;
        created.push_back(sibling.scenario);
    }

    if (plan.hasLooseMap) {
        std::vector<std::uint8_t> mapBytes;
        std::string mapError;
        if (!rl::BuildPatchedMap(plan, options, mapBytes, mapError)) {
            if (!rl::FileExistsUtf8(plan.mapSourcePath) || !ReadBytes(plan.mapSourcePath, mapBytes))
                return fail("无法读取地图：" + plan.scenarioName);
            warnings.push_back(mapError + "（已使用原始地图）");
        }
        std::string target = Join(run, plan.scenarioName);
        RemoveFile(target);
        if (!WriteBytes(target, mapBytes.data(), mapBytes.size()))
            return fail("无法写入场景文件：" + plan.scenarioName);
        created.push_back(plan.scenarioName);
    } else {
        warnings.push_back("地图在 MIX 档案中，直接由游戏读取");
    }

    if (plan.satellite) {
        std::string shroud = Join(root, "Resources/shroud.shp");
        if (Exists(shroud) && !addFile(shroud, "shroud.shp", true)) return fail("无法载入透明迷雾资源");
    }
    if (plan.useAres) {
        if (!CopyDirFiles(Join(root, "Ares"), run, created, error)) return fail(error);
        if (Exists(Join(root, "Ares/Syringe.exe")) && !addFile(Join(root, "Ares/Syringe.exe"), "Syringe.exe", true))
            return fail("无法载入 Syringe.exe");
    }
    if (plan.usePhobos) {
        std::string phobos = Join(root, "Phobos/Phobos.dll");
        if (Exists(phobos) && !addFile(phobos, "Phobos.dll", false)) return fail("无法载入 Phobos.dll");
    }
    if (plan.useTx) {
        if (!CopyDirFiles(Join(root, "TX"), run, created, error, true)) return fail(error);
    }

    {
        std::string path = Join(run, "RA2MD.ini");
        std::string text = ReadTextAuto(path);
        if (!text.empty()) {
            rl::IniDocument ini;
            ini.Parse(text);
            if (options.musicVolume >= 0) {
                if (options.musicVolume == 0) {
                    ini.Set("Audio", "InGameMusic", "no");
                    ini.Set("Audio", "ScoreVolume", "0.000000");
                } else {
                    int percent = std::min(options.musicVolume, 100);
                    char value[32];
                    std::snprintf(value, sizeof(value), "%.6f", percent / 100.0);
                    ini.Set("Audio", "InGameMusic", "yes");
                    ini.Set("Audio", "ScoreVolume", value);
                }
            }
            ini.Set("Options", "SkipAnimation", options.playMovies ? "False" : "True");
            WriteTextAnsi(path, ini.Serialize());
        }
    }

    if (!ApplyVideoProfile(root, run, video))
        warnings.push_back("无法写入窗口/渲染配置，请检查 Run/ddraw.ini 与 RA2MD.ini 的权限");

    if (!WriteTextAnsi(Join(root, "spawn.ini"), plan.spawnIni)) return fail("无法写入 spawn.ini");
    if (!WriteTextAnsi(Join(run, "spawn.ini"), plan.spawnIni)) return fail("无法写入 Run/spawn.ini");

    if (Exists(Join(root, "spawnmap.ini"))) {
        RemoveFile(Join(run, "spawnmap.ini"));
        LinkOrCopy(Join(root, "spawnmap.ini"), Join(run, "spawnmap.ini"), true);
    }

    if (!WriteLines(Join(run, kOverlayManifest), created)) return fail("无法写入覆盖清单");
    PrepareSaves(run, plan);
    return true;
}

void SyncSettingsBack(const std::string& gameRootUtf8) {
    std::string run = Join(gameRootUtf8, "Run");
    if (Exists(Join(run, "RA2MD.ini")))
        LinkOrCopy(Join(run, "RA2MD.ini"), Join(gameRootUtf8, "RA2MD.ini"), true);

    std::vector<std::string> lines = ReadLines(Join(run, kSaveMarker));
    if (lines.empty()) return;
    std::string packFolder = lines.front();
    std::set<std::string> previous(lines.begin() + 1, lines.end());
    std::string savesDir = Join(run, "Saved Games");
    std::string packDir = Join(savesDir, packFolder);
    EnsureDir(packDir);
    std::error_code ec;
    for (fs::directory_iterator it(P(savesDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        std::string stamp = name + "|" + std::to_string(fs::file_size(it->path(), ec2)) + "|" +
                            std::to_string(static_cast<long long>(fs::last_write_time(it->path(), ec2).time_since_epoch().count()));
        std::error_code ec3;
        fs::copy_file(it->path(), P(Join(packDir, name)), fs::copy_options::overwrite_existing, ec3);
        std::error_code ec4;
        fs::remove(it->path(), ec4);
    }
}

} // namespace rlmac
