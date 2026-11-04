#include "encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rl {
namespace {

// Returns true when the buffer is well-formed UTF-8 (BOM allowed).
bool IsValidUtf8(const std::uint8_t* data, std::size_t size, std::size_t& bomSize) {
    bomSize = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) bomSize = 3;
    bool hasNonAscii = bomSize != 0;
    std::size_t i = bomSize;
    while (i < size) {
        std::uint8_t c = data[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        hasNonAscii = true;
        int extra = 0;
        std::uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else {
            return false;
        }
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= size) return false;
            std::uint8_t cc = data[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000))
            return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    (void)hasNonAscii;
    return true;
}

} // namespace

std::string DecodeAnsiToUtf8(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return {};
    std::size_t bomSize = 0;
    if (IsValidUtf8(bytes.data(), bytes.size(), bomSize)) {
        return std::string(reinterpret_cast<const char*>(bytes.data()) + bomSize,
                           bytes.size() - bomSize);
    }
    const int size = static_cast<int>(bytes.size());
    const char* data = reinterpret_cast<const char*>(bytes.data());
    int wideLen = MultiByteToWideChar(936, 0, data, size, nullptr, 0);
    if (wideLen <= 0) return std::string(data, bytes.size());
    std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(936, 0, data, size, wide.data(), wideLen);
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return {};
    std::string out(static_cast<std::size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, out.data(), utf8Len, nullptr, nullptr);
    return out;
}

std::vector<std::uint8_t> EncodeUtf8ToAnsi(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        wideLen);
    int ansiLen = WideCharToMultiByte(936, 0, wide.data(), wideLen, nullptr, 0, "?", nullptr);
    if (ansiLen <= 0) return {};
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ansiLen));
    WideCharToMultiByte(936, 0, wide.data(), wideLen, reinterpret_cast<char*>(out.data()), ansiLen,
                        "?", nullptr);
    return out;
}

} // namespace rl
