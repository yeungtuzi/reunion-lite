#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/launch.h"

namespace rlmac {

// CrossOver 26.x on Apple Silicon: the wine wrapper is a Perl script that
// selects the bottle and starts wineloader (x86_64 under Rosetta, win32on64
// for the 32-bit game).
struct WineConfig {
    std::string crossOverRoot = "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver";
    std::string bottle = "RA2";
    std::string lang = "zh_CN.GBK";
    std::string debugChannels;  // empty => WINEDEBUG=-all
    bool waitChildren = true;
    bool noUpdate = true;
    std::string logDir;  // empty => ~/Library/Logs/ReunionLite
};

struct LaunchSpec {
    std::string program;
    std::vector<std::string> args;
    std::string workDir;  // host path (Run/)
    std::map<std::string, std::string> env;
    std::string logPath;
    std::string commandLine;  // printable, for logs / dry-run
};

// Builds the CrossOver wine invocation for a prepared Run/ directory.
bool BuildLaunchSpec(const rl::LaunchPlan& plan, const WineConfig& config, LaunchSpec& spec,
                     std::string& error);

// Runs the game and waits for the whole process tree (wine --wait-children).
// Returns the exit code, or -1 on spawn failure (error is set).
int RunGame(const LaunchSpec& spec, std::string& error);

// Converts a host path to the Wine Z: drive form (Z:\Users\...).
std::string ToWinePath(const std::string& hostPath);

// Splits a Windows-style command line (ClientDefinitions ExtraCommandLineParams).
std::vector<std::string> SplitCommandLine(const std::string& line);

} // namespace rlmac
