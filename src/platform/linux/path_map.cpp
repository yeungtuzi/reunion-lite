#include "core/platform_path.h"

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>

namespace rl {
namespace {

bool IsDirectory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string HomeDir() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
}

std::string ToSlashes(std::string path) {
    for (char& c : path)
        if (c == '\\') c = '/';
    return path;
}

// RL_DRIVE_MAP="D=/Users/lf/Documents;E=/Volumes/Games"
// Entries may use '=' or ':' after the drive letter and name the *drive root*:
// "D:\ra2master\..." with D=/Users/lf/Documents resolves to
// /Users/lf/Documents/ra2master/...
//
// The default (when D: is not configured) is ~/Documents, because the Reunion
// client installed the Lindlark packs under D:\ra2master there.
std::vector<std::pair<char, std::string>> LoadMappings() {
    static std::vector<std::pair<char, std::string>> mappings;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* env = std::getenv("RL_DRIVE_MAP");
        if (env && *env) {
            std::string spec(env);
            std::size_t start = 0;
            while (start <= spec.size()) {
                std::size_t end = spec.find(';', start);
                std::string entry =
                    spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
                start = end == std::string::npos ? spec.size() + 1 : end + 1;
                if (entry.size() >= 4 && std::isalpha(static_cast<unsigned char>(entry[0])) &&
                    entry[1] == ':' && (entry[2] == '=' || entry[2] == ':')) {
                    mappings.emplace_back(static_cast<char>(std::toupper(entry[0])),
                                          ToSlashes(entry.substr(3)));
                }
            }
        }
        bool hasD = false;
        for (const auto& item : mappings) hasD = hasD || item.first == 'D';
        if (!hasD) {
            std::string fallback = HomeDir() + "/Documents";
            if (IsDirectory(fallback + "/ra2master")) mappings.emplace_back('D', fallback);
        }
    });
    return mappings;
}

} // namespace

std::string MapPlatformPath(const std::string& utf8Path) {
    std::string path = ToSlashes(utf8Path);
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        const char drive = static_cast<char>(std::toupper(path[0]));
        for (const auto& item : LoadMappings()) {
            if (item.first != drive) continue;
            std::string rest = path.substr(2);
            while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
            if (rest.empty()) return item.second;
            return item.second + "/" + rest;
        }
    }
    return path;
}

std::uint64_t PlatformFingerprintSalt() {
    const std::string summary = PlatformDriveMapSummary();
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a 64
    for (unsigned char c : summary) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string PlatformDriveMapSummary() {
    std::string out;
    for (const auto& item : LoadMappings()) {
        if (!out.empty()) out += ";";
        out += item.first;
        out += "=";
        out += item.second;
    }
    return out;
}

} // namespace rl
