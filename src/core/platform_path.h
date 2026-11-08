#pragma once

#include <cstdint>
#include <string>

namespace rl {

// Translates a path coming from a Windows-era configuration file into a path
// that can be used by the host filesystem.
//
// On Windows this is the identity function.  On macOS/Linux it converts
// backslashes to slashes and maps legacy drive letters (D:\ra2master\...)
// through the RL_DRIVE_MAP table.
std::string MapPlatformPath(const std::string& utf8Path);

// Resolves a relative path below root component by component, matching each
// component case-insensitively when the exact name does not exist.  Returns an
// empty string when the directory cannot be resolved.  Linux filesystems are
// case-sensitive while the Reunion client was built on Windows.
std::string ResolveDirIgnoreCase(const std::string& rootUtf8, const std::string& relUtf8);

// Same as above but for an already absolute path (drive letters are mapped by
// MapPlatformPath).  Returns an empty string when a component is missing.
std::string ResolvePathIgnoreCase(const std::string& absUtf8);

// Human readable drive map, e.g. "D=/Users/lf/Documents".
std::string PlatformDriveMapSummary();

// Mixed into the campaign cache fingerprint so that changing the drive map
// invalidates a cache built with a different mapping.
std::uint64_t PlatformFingerprintSalt();

} // namespace rl
