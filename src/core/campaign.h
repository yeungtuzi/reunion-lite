#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rl {

struct MissionPack {
    std::string id;
    std::string name;
    std::string longDescription;
    std::string filePath;      // e.g. Maps\CP\1958...
    std::string sides;
    std::string difficulty;
    std::string modList;       // comma separated mod ids
    std::string defaultMod;
    int ares = 0;
    int phobos = 0;
    int tx = 0;
    bool yr = true;
    bool other = false;
    bool buildOffAlly = false;
    std::string sourceFile;
};

struct Mission {
    std::string packId;
    std::string packName;
    std::string packFilePath;  // folders can be inherited from the pack
    std::string section;       // section name inside the battle ini
    std::string name;          // Description
    std::string description;   // LongDescription, '@' means newline
    std::string scenario;      // e.g. SOV02SMD.MAP (empty for headers)
    std::string path;          // mission folder, backslash separated
    std::string modList;       // comma separated allowed mods
    std::string defaultMod;
    std::string side;          // SideName, e.g. Allied / Soviet
    std::string difficulty;
    std::string sourceFile;    // battle ini file name
    std::string badReason;     // non-empty => hidden from the list
    int index = 0;             // position inside [Battles]
    int sideValue = 0;
    int ares = 0;
    int phobos = 0;
    int tx = 0;
    bool buildOffAlly = false;
    bool yr = true;
    bool other = false;
    bool enabled = true;
    bool alwaysNormal = false;

    bool Launchable() const { return !scenario.empty() && badReason.empty(); }
};

struct ScanIssue {
    std::string file;
    std::string message;
};

struct Campaign {
    std::vector<MissionPack> packs;
    std::vector<Mission> missions;
    std::vector<ScanIssue> issues;
    std::int64_t scanUnixTime = 0;
    std::uint64_t scanMillis = 0;
};

struct FileStamp {
    std::string name;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
};

struct Fingerprint {
    std::vector<FileStamp> files;
    std::uint64_t hash = 0;
};

// Platform fast path: one directory pass returning name/size/write-time.
bool ListIniFileStamps(const std::string& dirUtf8, std::vector<FileStamp>& out);

// Scans Maps\CP for Battle*.ini and computes a cheap change fingerprint.
Fingerprint FingerprintMapsFolder(const std::string& gameRootUtf8);

// Full (slow) scan.  Mutates access times only; safe to run on a worker thread.
Campaign ScanCampaigns(const std::string& gameRootUtf8);

// Cache is a compact binary file, versioned and tied to the fingerprint.
bool LoadCampaignCache(const std::string& cachePathUtf8, const Fingerprint& fingerprint,
                       Campaign& out);
bool SaveCampaignCache(const std::string& cachePathUtf8, const Fingerprint& fingerprint,
                       const Campaign& campaign);

// Resolves/creates the cache path (Client\campaign.cache below the game root).
std::string DefaultCachePath(const std::string& gameRootUtf8);

// Validates the mission's files on disk.  Returns empty when launchable,
// otherwise a human readable reason (UTF-8 Chinese).
std::string ValidateMission(const std::string& gameRootUtf8, const Mission& mission);

std::string NormalizeRelPath(std::string path); // '/' -> '\', collapse "..", trim
bool FileExistsUtf8(const std::string& pathUtf8);
bool DirectoryExistsUtf8(const std::string& pathUtf8);
std::string FindFileIgnoreCase(const std::string& dirUtf8, const std::string& fileName);

} // namespace rl
