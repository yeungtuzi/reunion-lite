#include "campaign.h"

#ifndef _WIN32
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace rl {

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
