#pragma once

#include <string>
#include <vector>

#include "campaign.h"

namespace rl {

struct ModInfo {
    std::string id;
    std::string name;
    std::string filePath;   // relative to game root
    std::string iniPath;
    bool cpVisible = true;
    bool yr = true;
};

// Reads Mod&AI\Mod&AI*.ini.  Cheap (a few hundred small files at most).
std::vector<ModInfo> LoadModRegistry(const std::string& gameRootUtf8);

struct LaunchOptions {
    std::string modId;          // empty => mission default
    int difficulty = 1;         // 0 easy, 1 medium, 2 hard
    int credits = -1;           // -1 = leave map value untouched
    int gameSpeed = 2;          // written as-is to spawn.ini
    int autosave = 0;           // 0 off, 1 = 5 min, 2 = 10 min, 3 = 20 min
    int musicVolume = -1;       // -1 keep game setting, 0 off, else percent
    bool playMovies = true;     // show briefing/loading screen and mission videos
    bool satellite = false;
    bool ares = false;
    bool phobos = false;
    bool terrain = false;
    bool autoRepair = true;
    bool corr = true;
    bool reverse = false;
};

struct SiblingMap {
    std::string scenario;
    std::string sourcePath;
};

struct LaunchPlan {
    std::string gameRoot;
    std::string runDir;
    std::string modId;
    std::string modName;
    std::string modDir;
    std::string modGamePath;     // value written to spawn.ini "Game="
    std::string missionDir;
    std::string scenarioName;    // actual casing on disk
    std::string mapSourcePath;   // empty when the map lives inside a MIX
    bool hasLooseMap = false;
    std::vector<std::string> modFiles;      // relative to modDir, '\' separated
    std::vector<std::string> modCsfFiles;   // relative to modDir
    std::vector<std::string> missionFiles;  // relative to missionDir
    std::vector<std::string> missionCsfFiles;
    std::vector<std::string> mapCodeInis;   // absolute UTF-8 paths
    std::vector<SiblingMap> siblingMaps;    // rest of the pack, for next-mission
    std::vector<std::string> warnings;
    bool useAres = false;
    bool usePhobos = false;
    bool useTx = false;
    bool satellite = false;
    bool useRa2Dict = false;
    std::string spawnIni;       // UTF-8 text; write as system ANSI
    std::string extraArgs;      // from ClientDefinitions.ini
    std::string error;
};

bool BuildLaunchPlan(const std::string& gameRootUtf8, const Mission& mission,
                     const LaunchOptions& options, LaunchPlan& plan,
                     const std::vector<Mission>* allMissions = nullptr);

// Applies map-code INIs, credits and the two-way unit replacement to the map
// file, returns bytes in the system ANSI codepage (ready for Run\<scenario>).
bool BuildPatchedMap(const LaunchPlan& plan, const LaunchOptions& options,
                     std::vector<std::uint8_t>& outBytes, std::string& error);

// Ordered list of mods offered for a mission (resolved against the registry).
std::vector<ModInfo> ModsForMission(const std::vector<ModInfo>& registry, const Mission& mission);

} // namespace rl

