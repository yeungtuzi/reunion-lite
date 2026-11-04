// Reunion Lite - minimal INI reader/writer (portable C++17)
// Deliberately small: INI files used by YR are flat key=value sections in
// the system ANSI codepage.  Callers decode bytes to UTF-8 before parsing.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rl {

struct IniEntry {
    std::string key;
    std::string value;
};

struct IniSection {
    std::string name;
    std::vector<IniEntry> entries;
};

class IniDocument {
public:
    void Clear();

    // Parses UTF-8 text.  Lines starting with ';' or '#' are comments.
    // Unknown / continuation lines are ignored.  Duplicate sections are
    // merged, duplicate keys keep the last value.
    void Parse(const std::string& utf8Text);

    const IniSection* FindSection(std::string_view name) const;
    IniSection* FindSection(std::string_view name);

    bool HasSection(std::string_view name) const;
    bool HasKey(std::string_view section, std::string_view key) const;

    std::string Get(std::string_view section, std::string_view key,
                    std::string_view defaultValue = {}) const;
    int GetInt(std::string_view section, std::string_view key, int defaultValue = 0) const;
    bool GetBool(std::string_view section, std::string_view key, bool defaultValue = false) const;

    // Sets a value, creating the section/key when necessary.
    void Set(const std::string& section, const std::string& key, const std::string& value);
    void SetInt(const std::string& section, const std::string& key, int value);
    void SetBool(const std::string& section, const std::string& key, bool value);

    // Merges every section/key of other into this document (other wins).
    void Merge(const IniDocument& other);

    std::vector<std::string> GetKeys(std::string_view section) const;
    const std::vector<IniSection>& Sections() const { return sections_; }

    std::string Serialize() const;

private:
    std::vector<IniSection> sections_;
    std::unordered_map<std::string, std::size_t> index_; // ascii-lower name -> index
};

// ASCII case-insensitive compare / lower (UTF-8 bytes are preserved).
bool IniEquals(std::string_view a, std::string_view b);

} // namespace rl
