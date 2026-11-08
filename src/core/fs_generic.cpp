#include "campaign.h"

#include "platform_path.h"

#ifndef _WIN32
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace rl {

namespace {

std::string LowerAsciiLocal(std::string v) {
    for (char& c : v)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return v;
}

} // namespace

std::string ResolveDirIgnoreCase(const std::string& rootUtf8, const std::string& relUtf8) {
    std::error_code ec;
    fs::path cur = fs::u8path(rootUtf8);
    std::string rel = relUtf8;
    for (char& c : rel)
        if (c == '\\') c = '/';
    std::size_t i = 0;
    while (i < rel.size()) {
        const std::size_t j = rel.find('/', i);
        const std::string part =
            rel.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = (j == std::string::npos) ? rel.size() : j + 1;
        if (part.empty() || part == ".") continue;
        fs::path next = cur / fs::u8path(part);
        if (!fs::is_directory(next, ec)) {
            bool found = false;
            for (fs::directory_iterator it(cur, ec), end; it != end; it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_directory(ec2)) continue;
                if (LowerAsciiLocal(it->path().filename().u8string()) == LowerAsciiLocal(part)) {
                    next = it->path();
                    found = true;
                    break;
                }
            }
            if (!found) return {};
        }
        cur = next;
    }
    return cur.u8string();
}

std::string ResolvePathIgnoreCase(const std::string& absUtf8) {
    return ResolveDirIgnoreCase("/", absUtf8);
}

bool ListIniFileStamps(const std::string& dirUtf8, std::vector<FileStamp>& out) {
    out.clear();
    std::error_code ec;
    fs::path dir = fs::u8path(dirUtf8);
    if (!fs::is_directory(dir, ec)) return false;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = it->path().filename().u8string();
        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        for (char& c : ext)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (ext != ".ini") continue;
        FileStamp stamp;
        stamp.name = name;
        stamp.size = fs::file_size(it->path(), ec2);
        stamp.mtime = static_cast<std::int64_t>(
            fs::last_write_time(it->path(), ec2).time_since_epoch().count());
        out.push_back(std::move(stamp));
    }
    return true;
}

} // namespace rl
#endif
