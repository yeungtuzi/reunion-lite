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

// Human readable drive map, e.g. "D=/Users/lf/Documents".
std::string PlatformDriveMapSummary();

// Mixed into the campaign cache fingerprint so that changing the drive map
// invalidates a cache built with a different mapping.
std::uint64_t PlatformFingerprintSalt();

} // namespace rl
