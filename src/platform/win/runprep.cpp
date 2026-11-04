#include "runprep.h"
#include "winutil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include "core/campaign.h"
#include "core/ini.h"

namespace fs = std::filesystem;

namespace rlwin {
namespace {

const char* kOverlayManifest = ".rl_overlay";
const char* kBaseManifest = ".rl_base";
const char* kSaveMarker = ".rl_saves";

std::vector<std::string> ReadLines(const std::string& path);
bool WriteLines(const std::string& path, const std::vector<std::string>& lines);

bool IsSaveFile(const std::string& name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext == ".sav";
}

bool MoveReplace(const std::string& from, const std::string& to) {
    return MoveFileExW(Wide(from).c_str(), Wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

// Saves are kept per mission-pack folder.  Before launch the pack's saves are
// copied into the live "Saved Games" directory, after launch all saves are
// moved back.  This makes continuing a pack from any mission (and carrying
// state between its missions) work without mixing different packs' saves.
void PrepareSaves(const std::string& run, const rl::LaunchPlan& plan) {
    std::string savesDir = JoinPath(run, "Saved Games");
    if (!EnsureDirectory(savesDir)) return;
    std::string packFolder = fs::u8path(plan.missionDir).filename().u8string();
    if (packFolder.empty()) packFolder = "Other";
    std::string packDir = JoinPath(savesDir, packFolder);
    EnsureDirectory(packDir);

    // Remember what was already in the live directory, then bring this pack's
    // saves in.  On exit only new/changed saves are moved into the pack folder,
    // so saves of other packs are never lost or mixed.
    std::vector<std::string> markerLines;
    markerLines.push_back(packFolder);
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(savesDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        auto size = fs::file_size(it->path(), ec2);
        auto mtime = fs::last_write_time(it->path(), ec2).time_since_epoch().count();
        markerLines.push_back(name + "|" + std::to_string(size) + "|" + std::to_string(mtime));
    }

    for (fs::directory_iterator it(fs::u8path(packDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        CopyFileW(it->path().c_str(), Wide(JoinPath(savesDir, name)).c_str(), FALSE);
    }

    WriteLines(JoinPath(run, kSaveMarker), markerLines);
}

std::string ParentDir(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

bool WriteLines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (const auto& line : lines) f << line << "\r\n";
    return static_cast<bool>(f);
}

void DeleteFileIfExists(const std::string& path) {
    std::wstring w = Wide(path);
    if (!w.empty()) DeleteFileW(w.c_str());
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

void CopyRenderer(const std::string& root, const std::string& run) {
    std::string renderer = "CnC_DDraw_Experimental";
    std::string settings = ReadFileTextAuto(JoinPath(root, "RA2MD.ini"));
    if (!settings.empty()) {
        rl::IniDocument ini;
        ini.Parse(settings);
        renderer = ini.Get("Compatibility", "Renderer", renderer);
    }
    std::string renderDir = JoinPath(root, "Resources/Render/" + renderer);
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(renderDir), ec)) return;
    for (fs::directory_iterator it(fs::u8path(renderDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string ext = it->path().extension().u8string();
        std::string target = JoinPath(run, "ddraw" + ext);
        if (FileExists(target)) continue;
        CopyFileW(it->path().c_str(), Wide(target).c_str(), FALSE);
    }
}

bool EnsureBase(const std::string& root, const std::string& run, std::string& error) {
    if (!EnsureDirectory(run)) {
        error = "无法创建 Run 目录：" + run;
        return false;
    }
    std::string marker = JoinPath(run, kBaseManifest);
    bool markerPresent = FileExists(marker);
    bool baseOk = markerPresent && FileExists(JoinPath(run, "gamemd.exe")) &&
                  FileExists(JoinPath(run, "ra2.mix")) && FileExists(JoinPath(run, "language.mix")) &&
                  FileExists(JoinPath(run, "gamemd-spawn.exe"));
    if (baseOk) return true;

    for (const auto& item : kBaseFiles) {
        std::string source = JoinPath(root, item.sourceRel);
        std::string target = JoinPath(run, item.targetRel);
        if (!FileExists(source)) continue;
        if (FileExists(target)) continue;
        if (!LinkOrCopy(source, target, false)) {
            error = "复制基础文件失败：" + std::string(item.sourceRel);
            return false;
        }
    }

    std::string spawnSource = JoinPath(root, "gamemd-spawn.exe");
    std::string spawnTarget = JoinPath(run, "gamemd-spawn.exe");
    if (!FileExists(spawnTarget)) {
        if (!FileExists(spawnSource) || !CopyFileW(Wide(spawnSource).c_str(),
                                                   Wide(spawnTarget).c_str(), FALSE)) {
            error = "缺少 gamemd-spawn.exe";
            return false;
        }
    }

    std::string ra2md = JoinPath(run, "RA2MD.ini");
    if (!FileExists(ra2md)) {
        std::string source = JoinPath(root, "RA2MD.ini");
        if (FileExists(source)) CopyFileW(Wide(source).c_str(), Wide(ra2md).c_str(), FALSE);
    }

    std::string spawnmap = JoinPath(run, "spawnmap.ini");
    if (!FileExists(spawnmap)) {
        std::string source = JoinPath(root, "spawnmap.ini");
        if (FileExists(source)) {
            CopyFileW(Wide(source).c_str(), Wide(spawnmap).c_str(), FALSE);
        } else {
            std::string text = "[Map]\r\nSize=0,0,50,50\r\nLocalSize=0,0,50,50\r\n";
            WriteFileBytes(spawnmap, text.data(), text.size());
        }
    }

    if (!FileExists(JoinPath(run, "ddraw.dll"))) CopyRenderer(root, run);

    EnsureDirectory(JoinPath(run, "Saved Games"));
    std::vector<std::string> lines = {"ReunionLite base"};
    WriteLines(marker, lines);
    return true;
}

// Music files in Run are generated by the original client and are not part of
// the base set we can recreate.  Keep a hard-link copy so a mission pack that
// ships its own thememd.* cannot permanently destroy the base soundtrack.
void BackupMusicFiles(const std::string& run) {
    std::string keep = JoinPath(run, ".rl_base_keep");
    EnsureDirectory(keep);
    for (const char* name : {"thememd.mix", "thememd.ini"}) {
        std::string source = JoinPath(run, name);
        std::string target = JoinPath(keep, name);
        if (FileExists(source) && !FileExists(target))
            LinkOrCopy(source, target, false);
    }
}

void RestoreMusicFiles(const std::string& run) {
    std::string keep = JoinPath(run, ".rl_base_keep");
    for (const char* name : {"thememd.mix", "thememd.ini"}) {
        std::string source = JoinPath(keep, name);
        std::string target = JoinPath(run, name);
        if (FileExists(source) && !FileExists(target)) LinkOrCopy(source, target, false);
    }
}

bool ClearOverlay(const std::string& run) {
    std::string manifest = JoinPath(run, kOverlayManifest);
    std::vector<std::string> files = ReadLines(manifest);
    for (const std::string& rel : files) DeleteFileIfExists(JoinPath(run, rel));
    DeleteFileIfExists(manifest);
    RestoreMusicFiles(run);
    return true;
}

// Loose .MAP / .CSF files are always generated per launch.  Anything left over
// from the original client (often a symlink into Resources\MissionCache) must
// not shadow the current mission pack.
void ClearStaleRuntimeFiles(const std::string& run) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(run), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        std::string lower = name;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        bool staleMap = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".map";
        bool staleCsf = lower == "ra2md.csf" || lower == "ra2.csf";
        if (staleMap || staleCsf) DeleteFileW(it->path().c_str());
    }
}

bool CopyDirFiles(const std::string& sourceDir, const std::string& run,
                  std::vector<std::string>& created, std::string& error, bool onlyMix = false) {
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(sourceDir), ec)) return true;
    for (fs::directory_iterator it(fs::u8path(sourceDir), ec), end; it != end; it.increment(ec)) {
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
        std::string target = JoinPath(run, name);
        DeleteFileIfExists(target);
        if (!LinkOrCopy(it->path().u8string(), target, false)) {
            error = "写入文件失败：" + name;
            return false;
        }
        created.push_back(name);
    }
    return true;
}

} // namespace

bool PrepareRun(const rl::LaunchPlan& plan, const rl::LaunchOptions& options, std::string& error,
                std::vector<std::string>& warnings) {
    error.clear();
    warnings.clear();
    const std::string& root = plan.gameRoot;
    const std::string& run = plan.runDir;

    if (!EnsureBase(root, run, error)) return false;
    BackupMusicFiles(run);
    ClearOverlay(run);
    // A previous overlay could have shadowed a base file; restore it now.
    if (!EnsureBase(root, run, error)) return false;
    ClearStaleRuntimeFiles(run);

    std::vector<std::string> created;
    auto fail = [&](const std::string& message) {
        for (const auto& rel : created) DeleteFileIfExists(JoinPath(run, rel));
        error = message;
        return false;
    };

    auto addFile = [&](const std::string& source, const std::string& rel, bool forceCopy) -> bool {
        std::string target = JoinPath(run, rel);
        std::string parent = ParentDir(target);
        if (!parent.empty() && !EnsureDirectory(parent)) return false;
        DeleteFileIfExists(target);
        if (!LinkOrCopy(source, target, forceCopy)) return false;
        created.push_back(rel);
        return true;
    };

    // ---- selected mod overlay
    for (const std::string& rel : plan.modFiles) {
        std::string source = JoinPath(plan.modDir, rel);
        if (!addFile(source, rel, false)) return fail("无法载入模组文件：" + rel);
    }
    for (const std::string& rel : plan.modCsfFiles) {
        std::string source = JoinPath(plan.modDir, rel);
        std::string name = rel;
        std::size_t slash = name.find_last_of('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!addFile(source, name, true)) return fail("无法载入模组 CSF：" + name);
    }

    // ---- mission specific files
    for (const std::string& rel : plan.missionFiles) {
        std::string source = JoinPath(plan.missionDir, rel);
        if (!addFile(source, rel, false)) return fail("无法载入任务文件：" + rel);
    }
    for (const std::string& rel : plan.missionCsfFiles) {
        std::string source = JoinPath(plan.missionDir, rel);
        std::string name = rel;
        std::size_t slash = name.find_last_of('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!addFile(source, name, true)) return fail("无法载入任务 CSF：" + name);
    }

    // ---- sibling scenarios (campaign continuation)
    for (const auto& sibling : plan.siblingMaps) {
        rl::LaunchPlan tmp = plan;
        tmp.mapSourcePath = sibling.sourcePath;
        tmp.scenarioName = sibling.scenario;
        std::vector<std::uint8_t> bytes;
        std::string mapError;
        if (!rl::BuildPatchedMap(tmp, options, bytes, mapError)) {
            if (!ReadFileBytes(sibling.sourcePath, bytes)) continue;
        }
        std::string target = JoinPath(run, sibling.scenario);
        if (!WriteFileBytes(target, bytes.data(), bytes.size())) continue;
        created.push_back(sibling.scenario);
    }

    // ---- patched scenario
    if (plan.hasLooseMap) {
        std::vector<std::uint8_t> mapBytes;
        std::string mapError;
        if (!rl::BuildPatchedMap(plan, options, mapBytes, mapError)) {
            // Fall back to a raw copy: a slightly unpatched map is better than
            // refusing to launch, but tell the user about it.
            if (!rl::FileExistsUtf8(plan.mapSourcePath) ||
                !ReadFileBytes(plan.mapSourcePath, mapBytes)) {
                return fail("无法读取地图：" + plan.scenarioName);
            }
            warnings.push_back(mapError + "（已使用原始地图）");
        }
        std::string target = JoinPath(run, plan.scenarioName);
        DeleteFileIfExists(target);
        if (!WriteFileBytes(target, mapBytes.data(), mapBytes.size()))
            return fail("无法写入场景文件：" + plan.scenarioName);
        created.push_back(plan.scenarioName);
    } else {
        warnings.push_back("地图在 MIX 档案中，直接由游戏读取");
    }

    if (plan.satellite) {
        std::string shroud = JoinPath(root, "Resources/shroud.shp");
        if (rl::FileExistsUtf8(shroud)) {
            if (!addFile(shroud, "shroud.shp", true)) return fail("无法载入透明迷雾资源");
        }
    }

    // ---- extension platforms
    if (plan.useAres) {
        if (!CopyDirFiles(JoinPath(root, "Ares"), run, created, error))
            return fail(error);
        std::string syringe = JoinPath(root, "Ares/Syringe.exe");
        if (rl::FileExistsUtf8(syringe)) {
            if (!addFile(syringe, "Syringe.exe", true)) return fail("无法载入 Syringe.exe");
        }
    }
    if (plan.usePhobos) {
        std::string phobos = JoinPath(root, "Phobos/Phobos.dll");
        if (rl::FileExistsUtf8(phobos)) {
            if (!addFile(phobos, "Phobos.dll", false)) return fail("无法载入 Phobos.dll");
        }
    }
    if (plan.useTx) {
        if (!CopyDirFiles(JoinPath(root, "TX"), run, created, error, true)) return fail(error);
    }

    // ---- in-game music volume + briefing/movie playback (RA2MD.ini)
    {
        std::string path = JoinPath(run, "RA2MD.ini");
        std::string text = ReadFileTextAuto(path);
        if (!text.empty()) {
            rl::IniDocument ini;
            ini.Parse(text);
            if (options.musicVolume >= 0) {
                if (options.musicVolume == 0) {
                    ini.Set("Audio", "InGameMusic", "no");
                    ini.Set("Audio", "ScoreVolume", "0.000000");
                } else {
                    int percent = options.musicVolume;
                    if (percent > 100) percent = 100;
                    char value[32];
                    snprintf(value, sizeof(value), "%.6f", percent / 100.0);
                    ini.Set("Audio", "InGameMusic", "yes");
                    ini.Set("Audio", "ScoreVolume", value);
                }
            }
            ini.Set("Options", "SkipAnimation", options.playMovies ? "False" : "True");
            WriteFileTextAnsi(path, ini.Serialize());
        }
    }

    // ---- spawn settings (root + Run, ANSI on disk)
    if (!WriteFileTextAnsi(JoinPath(root, "spawn.ini"), plan.spawnIni))
        return fail("无法写入 spawn.ini");
    if (!WriteFileTextAnsi(JoinPath(run, "spawn.ini"), plan.spawnIni))
        return fail("无法写入 Run\\spawn.ini");

    std::string spawnmapSource = JoinPath(root, "spawnmap.ini");
    if (rl::FileExistsUtf8(spawnmapSource)) {
        std::string target = JoinPath(run, "spawnmap.ini");
        DeleteFileIfExists(target);
        CopyFileW(Wide(spawnmapSource).c_str(), Wide(target).c_str(), FALSE);
    }

    if (!WriteLines(JoinPath(run, kOverlayManifest), created))
        return fail("无法写入覆盖清单");

    PrepareSaves(run, plan);
    return true;
}

bool StartGame(const rl::LaunchPlan& plan, HANDLE& process, std::string& error) {
    process = nullptr;
    std::string run = plan.runDir;
    std::string exe;
    std::wstring arguments;

    if (plan.useAres) {
        exe = JoinPath(run, "Syringe.exe");
        arguments = L"\"gamemd.exe\" -SPAWN ";
    } else {
        exe = JoinPath(run, "gamemd-spawn.exe");
        arguments = L"-SPAWN ";
    }
    if (!rl::FileExistsUtf8(exe)) {
        error = "找不到游戏启动器：" + exe;
        return false;
    }
    arguments += Wide(plan.extraArgs);

    std::wstring exeW = Wide(exe);
    std::wstring cmd = L"\"" + exeW + L"\" " + arguments;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    DWORD flags = plan.useAres ? CREATE_NO_WINDOW : 0;
    BOOL ok = CreateProcessW(exeW.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, flags, nullptr,
                             Wide(run).c_str(), &si, &pi);
    if (!ok) {
        error = "无法启动游戏进程（错误码 " + std::to_string(GetLastError()) + "）";
        return false;
    }
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    return true;
}

void SyncSettingsBack(const std::string& gameRootUtf8) {
    std::string run = JoinPath(gameRootUtf8, "Run");
    std::string source = JoinPath(run, "RA2MD.ini");
    std::string target = JoinPath(gameRootUtf8, "RA2MD.ini");
    if (rl::FileExistsUtf8(source)) CopyFileW(Wide(source).c_str(), Wide(target).c_str(), FALSE);

    std::string marker = JoinPath(run, kSaveMarker);
    std::vector<std::string> lines = ReadLines(marker);
    if (lines.empty()) return;
    std::string packFolder = lines.front();
    std::set<std::string> previous(lines.begin() + 1, lines.end());
    std::string savesDir = JoinPath(run, "Saved Games");
    std::string packDir = JoinPath(savesDir, packFolder);
    EnsureDirectory(packDir);
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(savesDir), ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (!IsSaveFile(name)) continue;
        auto size = fs::file_size(it->path(), ec2);
        auto mtime = fs::last_write_time(it->path(), ec2).time_since_epoch().count();
        std::string key = name + "|" + std::to_string(size) + "|" + std::to_string(mtime);
        if (previous.find(key) != previous.end()) continue;
        MoveReplace(it->path().u8string(), JoinPath(packDir, name));
    }
    DeleteFileIfExists(marker);
}

} // namespace rlwin



