#include "campaign.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace rl {

bool ListIniFileStamps(const std::string& dirUtf8, std::vector<FileStamp>& out) {
    out.clear();
    int wideLen =
        MultiByteToWideChar(CP_UTF8, 0, dirUtf8.data(), static_cast<int>(dirUtf8.size()), nullptr, 0);
    if (wideLen <= 0) return false;
    std::wstring pattern(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dirUtf8.data(), static_cast<int>(dirUtf8.size()),
                        pattern.data(), wideLen);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
    pattern += L"*.ini";

    WIN32_FIND_DATAW fd{};
    HANDLE handle = FindFirstFileW(pattern.c_str(), &fd);
    if (handle == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        int nameLen = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
        if (nameLen <= 1) continue;
        FileStamp stamp;
        stamp.name.resize(static_cast<std::size_t>(nameLen - 1));
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, stamp.name.data(), nameLen, nullptr,
                            nullptr);
        stamp.size = (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        stamp.mtime = static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
            fd.ftLastWriteTime.dwLowDateTime);
        out.push_back(std::move(stamp));
    } while (FindNextFileW(handle, &fd));
    FindClose(handle);
    return true;
}

} // namespace rl
