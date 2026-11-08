#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/launch.h"

namespace rlmac {

// Window/video profile applied on every launch.  The game itself is left in
// "fullscreen" mode (Video.Windowed=False): that lets gamemd switch to its
// 16-bit mode, while cnc-ddraw turns the actual presentation into a window.
struct VideoConfig {
    int width = 1920;   // default 1080p window; stretching is allowed
    int height = 1080;
    std::string renderer = "gdi";  // 2D game: GDI is the reliable path under Wine
};

// Creates/refreshes Run\ with base files, mod overlay, mission files and the
// patched map, writes spawn.ini, and prepares the per-pack save directory.
bool PrepareRun(const rl::LaunchPlan& plan, const rl::LaunchOptions& options,
                std::string& error, std::vector<std::string>& warnings,
                const VideoConfig& video = VideoConfig{});

// Copies Run\RA2MD.ini back to the game root and moves new saves into the
// per-pack folder.  Call after the game process exits.
void SyncSettingsBack(const std::string& gameRootUtf8);

// Replaces copied base files in Run/ with hard links when the sizes match.
// Returns the number of bytes reclaimed.
std::uint64_t CompactBaseHardLinks(const rl::LaunchPlan& plan, std::string& error);

} // namespace rlmac
