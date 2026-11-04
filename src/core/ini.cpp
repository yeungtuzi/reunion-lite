#include "ini.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace rl {
namespace {

inline char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string LowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = LowerAscii(c);
    return out;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace

bool IniEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
    return true;
}

void IniDocument::Clear() {
    sections_.clear();
    index_.clear();
}

void IniDocument::Parse(const std::string& text) {
    sections_.clear();
    index_.clear();
    index_.reserve(64);

    IniSection* current = nullptr;
    std::size_t pos = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        pos = 3;

    const std::size_t n = text.size();
    while (pos < n) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = n;
        std::string_view line(text.data() + pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = Trim(line);
        if (line.empty()) continue;
        const char first = line.front();
        if (first == ';' || first == '#') continue;

        if (first == '[') {
            std::size_t close = line.find(']');
            if (close == std::string_view::npos) continue;
            std::string name = LowerAscii(Trim(line.substr(1, close - 1)));
            if (name.empty()) continue;
            auto it = index_.find(name);
            if (it != index_.end()) {
                current = &sections_[it->second];
            } else {
                index_.emplace(name, sections_.size());
                sections_.push_back(IniSection{std::string(Trim(line.substr(1, close - 1))), {}});
                current = &sections_.back();
            }
            continue;
        }

        if (current == nullptr) continue;
        std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key(Trim(line.substr(0, eq)));
        std::string value(Trim(line.substr(eq + 1)));
        if (key.empty()) continue;

        bool replaced = false;
        for (auto& entry : current->entries) {
            if (IniEquals(entry.key, key)) {
                entry.value = std::move(value);
                replaced = true;
                break;
            }
        }
        if (!replaced) current->entries.push_back(IniEntry{std::move(key), std::move(value)});
    }
}

const IniSection* IniDocument::FindSection(std::string_view name) const {
    auto it = index_.find(LowerAscii(name));
    return it == index_.end() ? nullptr : &sections_[it->second];
}

IniSection* IniDocument::FindSection(std::string_view name) {
    auto it = index_.find(LowerAscii(name));
    return it == index_.end() ? nullptr : &sections_[it->second];
}

bool IniDocument::HasSection(std::string_view name) const { return FindSection(name) != nullptr; }

bool IniDocument::HasKey(std::string_view section, std::string_view key) const {
    const IniSection* sec = FindSection(section);
    if (!sec) return false;
    for (const auto& entry : sec->entries)
        if (IniEquals(entry.key, key)) return true;
    return false;
}

std::string IniDocument::Get(std::string_view section, std::string_view key,
                             std::string_view defaultValue) const {
    const IniSection* sec = FindSection(section);
    if (!sec) return std::string(defaultValue);
    for (const auto& entry : sec->entries)
        if (IniEquals(entry.key, key)) return entry.value;
    return std::string(defaultValue);
}

int IniDocument::GetInt(std::string_view section, std::string_view key, int defaultValue) const {
    std::string value = Get(section, key, "");
    if (value.empty()) return defaultValue;
    char* end = nullptr;
    long v = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return defaultValue;
    return static_cast<int>(v);
}

bool IniDocument::GetBool(std::string_view section, std::string_view key, bool defaultValue) const {
    std::string value = Get(section, key, "");
    if (value.empty()) return defaultValue;
    if (IniEquals(value, "true") || IniEquals(value, "yes") || IniEquals(value, "1") ||
        IniEquals(value, "on"))
        return true;
    if (IniEquals(value, "false") || IniEquals(value, "no") || IniEquals(value, "0") ||
        IniEquals(value, "off"))
        return false;
    return defaultValue;
}

void IniDocument::Set(const std::string& section, const std::string& key, const std::string& value) {
    std::string lower = LowerAscii(section);
    auto it = index_.find(lower);
    if (it == index_.end()) {
        index_.emplace(lower, sections_.size());
        sections_.push_back(IniSection{section, {}});
        it = index_.find(lower);
    }
    IniSection& sec = sections_[it->second];
    for (auto& entry : sec.entries) {
        if (IniEquals(entry.key, key)) {
            entry.value = value;
            return;
        }
    }
    sec.entries.push_back(IniEntry{key, value});
}

void IniDocument::SetInt(const std::string& section, const std::string& key, int value) {
    Set(section, key, std::to_string(value));
}

void IniDocument::SetBool(const std::string& section, const std::string& key, bool value) {
    Set(section, key, value ? "True" : "False");
}

void IniDocument::Merge(const IniDocument& other) {
    for (const auto& sec : other.sections_)
        for (const auto& entry : sec.entries)
            Set(sec.name, entry.key, entry.value);
}

std::vector<std::string> IniDocument::GetKeys(std::string_view section) const {
    std::vector<std::string> keys;
    const IniSection* sec = FindSection(section);
    if (!sec) return keys;
    keys.reserve(sec->entries.size());
    for (const auto& entry : sec->entries) keys.push_back(entry.key);
    return keys;
}

std::string IniDocument::Serialize() const {
    std::string out;
    out.reserve(4096);
    for (const auto& sec : sections_) {
        out += '[';
        out += sec.name;
        out += "]\r\n";
        for (const auto& entry : sec.entries) {
            out += entry.key;
            out += '=';
            out += entry.value;
            out += "\r\n";
        }
        out += "\r\n";
    }
    return out;
}

} // namespace rl
