#include "campaign.h"
#include "encoding.h"
#include "platform_path.h"
#include "ini.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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

bool ReadAllBytes(const std::string& path, std::vector<std::uint8_t>& out, std::string& err) {
    std::ifstream f(Fs(path), std::ios::binary);
    if (!f) {
        err = "无法打开文件：" + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) {
        err = "无法读取文件大小：" + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    if (!f && !f.eof()) {
        err = "读取文件失败：" + path;
        return false;
    }
    return true;
}

std::vector<FileStamp> ListBattleIniStamps(const fs::path& mapsDir, std::string& error) {
    std::vector<FileStamp> stamps;
    error.clear();
    std::error_code ec;
    if (!fs::is_directory(mapsDir, ec)) {
        error = "找不到地图目录：" + U8(mapsDir);
        return stamps;
    }
    ListIniFileStamps(U8(mapsDir), stamps);
    std::sort(stamps.begin(), stamps.end(), [](const FileStamp& a, const FileStamp& b) {
        return LowerAscii(a.name) < LowerAscii(b.name);
    });
    return stamps;
}

std::string FirstMod(const std::string& list) {
    std::size_t comma = list.find(',');
    std::string first = comma == std::string::npos ? list : list.substr(0, comma);
    while (!first.empty() && (first.front() == ' ' || first.front() == '\t')) first.erase(first.begin());
    while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) first.pop_back();
    return first;
}

std::uint64_t Fnv1a(const std::string& bytes) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------- cache io

struct Writer {
    std::vector<std::uint8_t> data;
    void U8(std::uint8_t v) { data.push_back(v); }
    void U32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }
    void U64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) data.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }
    void I64(std::int64_t v) { U64(static_cast<std::uint64_t>(v)); }
    void Str(const std::string& s) {
        U32(static_cast<std::uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    }
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    bool U8(std::uint8_t& v) {
        if (pos_ + 1 > size_) return false;
        v = data_[pos_++];
        return true;
    }
    bool U32(std::uint32_t& v) {
        if (pos_ + 4 > size_) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data_[pos_++]) << (i * 8);
        return true;
    }
    bool U64(std::uint64_t& v) {
        if (pos_ + 8 > size_) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(data_[pos_++]) << (i * 8);
        return true;
    }
    bool I64(std::int64_t& v) {
        std::uint64_t u = 0;
        if (!U64(u)) return false;
        v = static_cast<std::int64_t>(u);
        return true;
    }
    bool Str(std::string& s) {
        std::uint32_t n = 0;
        if (!U32(n)) return false;
        if (pos_ + n > size_) return false;
        s.assign(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return true;
    }
    bool Bytes(std::size_t n, const std::uint8_t*& p) {
        if (pos_ + n > size_) return false;
        p = data_ + pos_;
        pos_ += n;
        return true;
    }
    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

void WritePack(Writer& w, const MissionPack& p) {
    w.Str(p.id);
    w.Str(p.name);
    w.Str(p.longDescription);
    w.Str(p.filePath);
    w.Str(p.sides);
    w.Str(p.difficulty);
    w.Str(p.modList);
    w.Str(p.defaultMod);
    w.U32(static_cast<std::uint32_t>(p.ares));
    w.U32(static_cast<std::uint32_t>(p.phobos));
    w.U32(static_cast<std::uint32_t>(p.tx));
    w.U8(p.yr ? 1 : 0);
    w.U8(p.other ? 1 : 0);
    w.U8(p.buildOffAlly ? 1 : 0);
    w.Str(p.sourceFile);
}

bool ReadPack(Reader& r, MissionPack& p) {
    std::uint32_t a = 0, ph = 0, tx = 0;
    std::uint8_t yr = 1, other = 0, bo = 0;
    if (!r.Str(p.id) || !r.Str(p.name) || !r.Str(p.longDescription) || !r.Str(p.filePath) ||
        !r.Str(p.sides) || !r.Str(p.difficulty) || !r.Str(p.modList) || !r.Str(p.defaultMod) ||
        !r.U32(a) || !r.U32(ph) || !r.U32(tx) || !r.U8(yr) || !r.U8(other) || !r.U8(bo) ||
        !r.Str(p.sourceFile))
        return false;
    p.ares = static_cast<int>(a);
    p.phobos = static_cast<int>(ph);
    p.tx = static_cast<int>(tx);
    p.yr = yr != 0;
    p.other = other != 0;
    p.buildOffAlly = bo != 0;
    return true;
}

void WriteMission(Writer& w, const Mission& m) {
    w.Str(m.packId);
    w.Str(m.packName);
    w.Str(m.packFilePath);
    w.Str(m.section);
    w.Str(m.name);
    w.Str(m.description);
    w.Str(m.scenario);
    w.Str(m.path);
    w.Str(m.modList);
    w.Str(m.defaultMod);
    w.Str(m.side);
    w.Str(m.difficulty);
    w.Str(m.sourceFile);
    w.Str(m.badReason);
    w.U32(static_cast<std::uint32_t>(m.index));
    w.U32(static_cast<std::uint32_t>(m.sideValue));
    w.U32(static_cast<std::uint32_t>(m.ares));
    w.U32(static_cast<std::uint32_t>(m.phobos));
    w.U32(static_cast<std::uint32_t>(m.tx));
    w.U8(m.buildOffAlly ? 1 : 0);
    w.U8(m.yr ? 1 : 0);
    w.U8(m.other ? 1 : 0);
    w.U8(m.enabled ? 1 : 0);
    w.U8(m.alwaysNormal ? 1 : 0);
}

bool ReadMission(Reader& r, Mission& m) {
    std::uint32_t idx = 0, side = 0, a = 0, ph = 0, tx = 0;
    std::uint8_t bo = 0, yr = 1, other = 0, en = 1, an = 0;
    if (!r.Str(m.packId) || !r.Str(m.packName) || !r.Str(m.packFilePath) || !r.Str(m.section) ||
        !r.Str(m.name) || !r.Str(m.description) || !r.Str(m.scenario) || !r.Str(m.path) ||
        !r.Str(m.modList) || !r.Str(m.defaultMod) || !r.Str(m.side) || !r.Str(m.difficulty) ||
        !r.Str(m.sourceFile) || !r.Str(m.badReason) || !r.U32(idx) || !r.U32(side) || !r.U32(a) ||
        !r.U32(ph) || !r.U32(tx) || !r.U8(bo) || !r.U8(yr) || !r.U8(other) || !r.U8(en) ||
        !r.U8(an))
        return false;
    m.index = static_cast<int>(idx);
    m.sideValue = static_cast<int>(side);
    m.ares = static_cast<int>(a);
    m.phobos = static_cast<int>(ph);
    m.tx = static_cast<int>(tx);
    m.buildOffAlly = bo != 0;
    m.yr = yr != 0;
    m.other = other != 0;
    m.enabled = en != 0;
    m.alwaysNormal = an != 0;
    return true;
}

} // namespace

std::string NormalizeRelPath(std::string path) {
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(path.begin());
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
    std::string out;
    out.reserve(path.size());
    bool lastSlash = false;
    for (char c : path) {
        if (c == '\\') {
            if (lastSlash) continue;
            lastSlash = true;
        } else {
            lastSlash = false;
        }
        out.push_back(c);
    }
    if (!out.empty() && out.back() == '\\') out.pop_back();
    if (out.rfind(".\\", 0) == 0) out.erase(0, 2);
    return out;
}

bool FileExistsUtf8(const std::string& pathUtf8) {
    std::error_code ec;
    return fs::is_regular_file(Fs(pathUtf8), ec);
}

bool DirectoryExistsUtf8(const std::string& pathUtf8) {
    std::error_code ec;
    return fs::is_directory(Fs(pathUtf8), ec);
}

std::string FindFileIgnoreCase(const std::string& dirUtf8, const std::string& fileName) {
    fs::path dir = Fs(dirUtf8);
    std::error_code ec;
    if (fileName.empty()) return {};
    fs::path direct = dir / Fs(fileName);
    if (fs::is_regular_file(direct, ec)) return fileName;
    if (!fs::is_directory(dir, ec)) return {};
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (IniEquals(U8(it->path().filename()), fileName)) return U8(it->path().filename());
    }
    return {};
}

Fingerprint FingerprintMapsFolder(const std::string& gameRootUtf8) {
    Fingerprint fp;
    fs::path mapsDir = Fs(gameRootUtf8) / "Maps" / "CP";
    std::string error;
    std::vector<FileStamp> stamps = ListBattleIniStamps(mapsDir, error);
    fp.files = std::move(stamps);
    std::string hashInput;
    hashInput.reserve(fp.files.size() * 48);
    for (const FileStamp& stamp : fp.files) {
        hashInput += stamp.name;
        hashInput.push_back('\0');
        hashInput += std::to_string(stamp.size);
        hashInput.push_back('\0');
        hashInput += std::to_string(stamp.mtime);
        hashInput.push_back('\0');
    }
    fp.hash = Fnv1a(hashInput);
#ifndef _WIN32
    fp.hash ^= PlatformFingerprintSalt();
#endif
    if (fp.files.empty()) fp.hash = 0;
    return fp;
}

std::string ValidateMission(const std::string& gameRootUtf8, const Mission& m) {
    if (!m.enabled) return "任务已被禁用";
    if (m.scenario.empty()) return "缺少场景文件";
    if (m.path.empty()) return "缺少任务目录";
    fs::path root = Fs(gameRootUtf8);
    fs::path missionDir = root / Fs(m.path);
    std::error_code ec;
    if (!fs::is_directory(missionDir, ec)) {
        const std::string resolved = ResolvePathIgnoreCase(U8(missionDir));
        if (resolved.empty()) return "任务目录不存在：" + m.path;
        missionDir = Fs(resolved);
    }
    if (FindFileIgnoreCase(U8(missionDir), m.scenario).empty())
        return "场景文件不存在：" + m.scenario;
    return {};
}

Campaign ScanCampaigns(const std::string& gameRootUtf8) {
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();

    Campaign campaign;
    campaign.packs.reserve(1600);
    campaign.missions.reserve(4000);
    fs::path root = Fs(gameRootUtf8);
    fs::path mapsDir = root / "Maps" / "CP";
    std::string listError;
    std::vector<FileStamp> stamps = ListBattleIniStamps(mapsDir, listError);
    if (!listError.empty()) {
        campaign.issues.push_back(ScanIssue{"Maps/CP", listError});
        return campaign;
    }

    std::unordered_map<std::string, std::size_t> packIndex; // ascii-lower id -> index
    std::unordered_map<std::string, bool> dirCache;         // mission folder -> exists
    dirCache.reserve(1600);

    for (const FileStamp& stamp : stamps) {
        const std::string& fileName = stamp.name;
        fs::path filePath = mapsDir / Fs(fileName);
        std::vector<std::uint8_t> bytes;
        std::string err;
        if (!ReadAllBytes(U8(filePath), bytes, err)) {
            campaign.issues.push_back(ScanIssue{fileName, err});
            continue;
        }
        std::string text = DecodeAnsiToUtf8(bytes);
        IniDocument ini;
        ini.Parse(text);
        if (!ini.HasSection("Battles") && !ini.HasSection("MissionPack")) continue;

        // ---- mission packs declared by this file
        for (const std::string& key : ini.GetKeys("MissionPack")) {
            std::string packId = ini.Get("MissionPack", key, "");
            if (packId.empty()) continue;
            if (!ini.HasSection(packId)) {
                campaign.issues.push_back(
                    ScanIssue{fileName, "任务包段落不存在：" + packId});
                continue;
            }
            std::string lowerId = LowerAscii(packId);
            if (packIndex.find(lowerId) != packIndex.end()) continue;

            MissionPack pack;
            pack.id = packId;
            pack.sourceFile = fileName;
            pack.name = ini.Get(packId, "Description", ini.Get(packId, "Name", packId));
            pack.longDescription =
                ini.Get(packId, "LongDescription", ini.Get(packId, "Description", pack.name));
            pack.filePath = NormalizeRelPath(ini.Get(packId, "Mission", packId));
            pack.sides = ini.Get(packId, "SideName", "");
            pack.difficulty = ini.Get(packId, "Difficulty", "中等");
            pack.modList = ini.Get(packId, "Mod", "");
            pack.defaultMod = ini.Get(packId, "DefaultMod", FirstMod(pack.modList));
            pack.ares = ini.GetInt(packId, "Ares", 0);
            pack.phobos = ini.GetInt(packId, "Phobos", 0);
            pack.tx = ini.GetInt(packId, "TX", 0);
            pack.yr = ini.GetBool(packId, "YR", true);
            pack.other = ini.GetBool(packId, "Other", false);
            pack.buildOffAlly = ini.GetBool(packId, "BuildOffAlly", false);

            packIndex.emplace(lowerId, campaign.packs.size());
            campaign.packs.push_back(std::move(pack));
        }

        // ---- missions
        int index = 0;
        for (const std::string& key : ini.GetKeys("Battles")) {
            const int missionIndex = index++;
            std::string section = ini.Get("Battles", key, "");
            if (section.empty()) continue;
            if (!ini.HasSection(section)) continue;

            Mission m;
            m.section = section;
            m.index = missionIndex;
            m.sourceFile = fileName;
            m.scenario = ini.Get(section, "Scenario", "");
            if (m.scenario.empty()) continue; // header entry, not a playable mission

            m.packId = ini.Get(section, "MissionPack", "");
            const MissionPack* pack = nullptr;
            auto pit = packIndex.find(LowerAscii(m.packId));
            if (pit != packIndex.end()) pack = &campaign.packs[pit->second];
            if (!pack && campaign.packs.size() == 1) pack = &campaign.packs.back();

            if (pack) {
                m.packName = pack->name;
                m.packFilePath = pack->filePath;
            }
            m.name = ini.Get(section, "Description", pack ? pack->name : section);
            m.description =
                ini.Get(section, "LongDescription", pack ? pack->longDescription : m.name);
            m.path = NormalizeRelPath(
                ini.Get(section, "Mission", pack ? pack->filePath : std::string()));
            m.modList = ini.Get(section, "Mod", pack ? pack->modList : std::string());
            m.defaultMod =
                ini.Get(section, "DefaultMod",
                        pack ? pack->defaultMod : FirstMod(m.modList));
            m.side = ini.Get(section, "SideName", pack ? pack->sides : std::string());
            m.difficulty =
                ini.Get(section, "Difficulty", pack ? pack->difficulty : std::string("中等"));
            m.ares = ini.GetInt(section, "Ares", pack ? pack->ares : 0);
            m.phobos = ini.GetInt(section, "Phobos", pack ? pack->phobos : 0);
            m.tx = ini.GetInt(section, "TX", pack ? pack->tx : 0);
            m.sideValue = ini.GetInt(section, "Side", 0);
            m.buildOffAlly =
                ini.GetBool(section, "BuildOffAlly", pack ? pack->buildOffAlly : false);
            m.yr = ini.GetBool(section, "YR", pack ? pack->yr : true);
            m.other = ini.GetBool(section, "Other", pack ? pack->other : false);
            m.enabled = ini.GetBool(section, "Enabled", true);
            m.alwaysNormal = ini.GetBool(section, "PlayerAlwaysOnNormalDifficulty", false);

            if (!m.enabled) {
                m.badReason = "任务已被禁用";
            } else if (m.path.empty()) {
                m.badReason = "缺少任务目录";
            } else {
                auto it = dirCache.find(m.path);
                bool exists;
                if (it == dirCache.end()) {
                    std::error_code ec;
                    exists = fs::is_directory(root / Fs(m.path), ec);
                    if (!exists)
                        exists = !ResolvePathIgnoreCase(U8(root / Fs(m.path))).empty();
                    dirCache.emplace(m.path, exists);
                } else {
                    exists = it->second;
                }
                if (!exists) m.badReason = "任务目录不存在：" + m.path;
            }
            campaign.missions.push_back(std::move(m));
        }
    }

    campaign.scanMillis = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    campaign.scanUnixTime = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    return campaign;
}

std::string DefaultCachePath(const std::string& gameRootUtf8) {
    fs::path dir = Fs(gameRootUtf8) / "Client";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return U8(dir / "campaign.cache");
}

bool SaveCampaignCache(const std::string& cachePathUtf8, const Fingerprint& fingerprint,
                       const Campaign& campaign) {
    Writer w;
    w.Str("RLCA");
    w.U32(3);
    w.U64(fingerprint.hash);
    w.I64(campaign.scanUnixTime);
    w.U32(static_cast<std::uint32_t>(fingerprint.files.size()));
    for (const auto& f : fingerprint.files) {
        w.Str(f.name);
        w.U64(f.size);
        w.I64(f.mtime);
    }
    w.U32(static_cast<std::uint32_t>(campaign.packs.size()));
    for (const auto& p : campaign.packs) WritePack(w, p);
    w.U32(static_cast<std::uint32_t>(campaign.missions.size()));
    for (const auto& m : campaign.missions) WriteMission(w, m);
    w.U32(static_cast<std::uint32_t>(campaign.issues.size()));
    for (const auto& i : campaign.issues) {
        w.Str(i.file);
        w.Str(i.message);
    }

    fs::path target = Fs(cachePathUtf8);
    fs::path tmp = Fs(cachePathUtf8 + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(w.data.data()),
                static_cast<std::streamsize>(w.data.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::remove(target, ec);
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool LoadCampaignCache(const std::string& cachePathUtf8, const Fingerprint& fingerprint,
                       Campaign& out) {
    std::vector<std::uint8_t> bytes;
    std::string err;
    if (!ReadAllBytes(cachePathUtf8, bytes, err)) return false;
    Reader r(bytes.data(), bytes.size());

    std::string magic;
    std::uint32_t version = 0;
    std::uint64_t hash = 0;
    std::int64_t scanTime = 0;
    if (!r.Str(magic) || magic != "RLCA" || !r.U32(version) || version != 3 || !r.U64(hash) ||
        !r.I64(scanTime))
        return false;
    if (hash != fingerprint.hash) return false;

    std::uint32_t fileCount = 0;
    if (!r.U32(fileCount) || fileCount != fingerprint.files.size()) return false;
    for (const auto& expected : fingerprint.files) {
        std::string name;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        if (!r.Str(name) || !r.U64(size) || !r.I64(mtime)) return false;
        if (name != expected.name || size != expected.size || mtime != expected.mtime)
            return false;
    }

    Campaign campaign;
    campaign.scanUnixTime = scanTime;
    std::uint32_t count = 0;
    if (!r.U32(count) || count > 100000) return false;
    campaign.packs.resize(count);
    for (auto& p : campaign.packs)
        if (!ReadPack(r, p)) return false;

    if (!r.U32(count) || count > 200000) return false;
    campaign.missions.resize(count);
    for (auto& m : campaign.missions)
        if (!ReadMission(r, m)) return false;

    if (!r.U32(count) || count > 100000) return false;
    campaign.issues.resize(count);
    for (auto& i : campaign.issues)
        if (!r.Str(i.file) || !r.Str(i.message)) return false;

    out = std::move(campaign);
    return true;
}

} // namespace rl



