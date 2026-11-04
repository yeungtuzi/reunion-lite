#include "winutil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>

#include "core/encoding.h"

namespace fs = std::filesystem;

namespace rlwin {

std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::string Utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
}

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '\\' && out.back() != '/') out.push_back('\\');
    out += b;
    return out;
}

bool FileExists(const std::string& pathUtf8) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(pathUtf8), ec);
}

bool DirExists(const std::string& pathUtf8) {
    std::error_code ec;
    return fs::is_directory(fs::u8path(pathUtf8), ec);
}

bool ReadFileBytes(const std::string& pathUtf8, std::vector<std::uint8_t>& out) {
    std::FILE* f = _wfopen(Wide(pathUtf8).c_str(), L"rb");
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    __int64 size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    bool ok = size == 0 || std::fread(out.data(), 1, static_cast<std::size_t>(size), f) ==
                               static_cast<std::size_t>(size);
    std::fclose(f);
    return ok;
}

bool WriteFileBytes(const std::string& pathUtf8, const void* data, std::size_t size) {
    std::FILE* f = _wfopen(Wide(pathUtf8).c_str(), L"wb");
    if (!f) return false;
    bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    std::fclose(f);
    return ok;
}

bool WriteFileTextAnsi(const std::string& pathUtf8, const std::string& utf8Text) {
    std::vector<std::uint8_t> bytes = rl::EncodeUtf8ToAnsi(utf8Text);
    if (bytes.empty() && !utf8Text.empty()) return false;
    return WriteFileBytes(pathUtf8, bytes.data(), bytes.size());
}

std::string ReadFileTextAuto(const std::string& pathUtf8) {
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(pathUtf8, bytes)) return {};
    return rl::DecodeAnsiToUtf8(bytes);
}

bool EnsureDirectory(const std::string& pathUtf8) {
    std::error_code ec;
    fs::create_directories(fs::u8path(pathUtf8), ec);
    return fs::is_directory(fs::u8path(pathUtf8), ec);
}

bool LinkOrCopy(const std::string& sourceUtf8, const std::string& targetUtf8, bool forceCopy) {
    std::wstring source = Wide(sourceUtf8);
    std::wstring target = Wide(targetUtf8);
    if (source.empty() || target.empty()) return false;
    if (FileExists(targetUtf8)) return true;
    if (!forceCopy && CreateHardLinkW(target.c_str(), source.c_str(), nullptr)) return true;
    return CopyFileW(source.c_str(), target.c_str(), FALSE) != 0;
}

std::string GetModuleDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return {};
        if (len < path.size()) {
            path.resize(len);
            break;
        }
        path.resize(path.size() * 2);
    }
    std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    return Utf8(path);
}

std::string ExeDirectoryUtf8() { return GetModuleDirectory(); }

} // namespace rlwin
