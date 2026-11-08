#include "core/platform_path.h"

namespace rl {

std::string MapPlatformPath(const std::string& utf8Path) { return utf8Path; }
std::string PlatformDriveMapSummary() { return {}; }
std::uint64_t PlatformFingerprintSalt() { return 0; }

} // namespace rl
