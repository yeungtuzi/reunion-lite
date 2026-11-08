#pragma once

#include <string>

#include "core/launch.h"

namespace rlwin {

// Prepares the run directory for VERA20K:
//   * rewrites spawn.ini as UTF-8 (gamemd wants the ANSI codepage, VERA does not)
//   * copies the mission's aimd.ini from "Mod&AI" into the run directory
//   * writes config.toml pointing VERA at the run directory
//   * mirrors the launcher's game speed into VERA.INI
bool PrepareVera20k(const rl::LaunchPlan& plan, const rl::LaunchOptions& options,
                    std::string& error);

// $XDG_DATA_HOME/VERA20k/VERA.INI (defaults to ~/.local/share/VERA20k/VERA.INI).
std::string VeraIniPath();

} // namespace rlwin
