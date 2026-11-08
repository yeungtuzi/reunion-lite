#include "campaign.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace rl {

namespace {

std::wstring WidenUtf8(const std::string& text) {
    if (text.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), len);
    return wide;
}

std::string NarrowUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
}

} // namespace

// NTFS lookups are case-insensitive already; these two only have to answer
// whether the requested directory exists and return its path unchanged.
std::string ResolveDirIgnoreCase(const std::string& rootUtf8, const std::string& relUtf8) {
    std::wstring joined = WidenUtf8(rootUtf8);
    if (joined.empty()) return {};
    if (joined.back() != L'\\' && joined.back() != L'/') joined.push_back(L'\\');
    joined += WidenUtf8(relUtf8);
    const DWORD attrs = GetFileAttributesW(joined.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return NarrowUtf8(joined);
}

std::string ResolvePathIgnoreCase(const std::string& absUtf8) {
    const std::wstring wide = WidenUtf8(absUtf8);
    if (wide.empty()) return {};
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return absUtf8;
}

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
