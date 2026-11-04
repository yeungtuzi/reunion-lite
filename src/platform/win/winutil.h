#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rlwin {

std::wstring Wide(const std::string& utf8);
std::string Utf8(const std::wstring& wide);

std::string ExeDirectoryUtf8();
std::string JoinPath(const std::string& a, const std::string& b);

bool FileExists(const std::string& pathUtf8);
bool DirExists(const std::string& pathUtf8);
bool ReadFileBytes(const std::string& pathUtf8, std::vector<std::uint8_t>& out);
bool WriteFileBytes(const std::string& pathUtf8, const void* data, std::size_t size);
bool WriteFileTextAnsi(const std::string& pathUtf8, const std::string& utf8Text);
std::string ReadFileTextAuto(const std::string& pathUtf8);

// Hard-link when possible (same NTFS volume, no privileges), copy otherwise.
bool LinkOrCopy(const std::string& sourceUtf8, const std::string& targetUtf8, bool forceCopy);
bool EnsureDirectory(const std::string& pathUtf8);

std::string GetModuleDirectory();

} // namespace rlwin

