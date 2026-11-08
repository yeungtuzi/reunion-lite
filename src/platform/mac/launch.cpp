#include "platform/mac/launch.h"
#include "core/platform_path.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace rlmac {
namespace {

std::string HomeDir() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string("/tmp");
}

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a.back() == '/' ? a + b : a + "/" + b;
}

std::string Quote(const std::string& value) {
    if (value.find_first_of(" \t\"'") == std::string::npos) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tm);
    return buffer;
}

bool EnsureDir(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return ::mkdir(path.c_str(), 0755) == 0;
}

void AppendEnv(std::map<std::string, std::string>& env, const char* key, const std::string& value) {
    env[key] = value;
}

} // namespace

std::string ToWinePath(const std::string& hostPath) {
    std::string path = hostPath;
    for (char& c : path)
        if (c == '/') c = '\\';
    if (!path.empty() && path.front() == '\\') return "Z:" + path;
    return "Z:\\" + path;
}

std::vector<std::string> SplitCommandLine(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if ((c == ' ' || c == '\t') && !inQuotes) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool BuildLaunchSpec(const rl::LaunchPlan& plan, const WineConfig& config, LaunchSpec& spec,
                     std::string& error) {
    spec = LaunchSpec {};
    error.clear();

    const std::string wine = JoinPath(JoinPath(config.crossOverRoot, "bin"), "wine");
    if (::access(wine.c_str(), X_OK) != 0) {
        error = "找不到 CrossOver wine：" + wine +
                "（请安装 CrossOver 26.x，或用 REUNION_CX_ROOT 指定路径）";
        return false;
    }
    const std::string runDir = rl::MapPlatformPath(plan.runDir);
    const std::string exeName = plan.useAres ? "Syringe.exe" : "gamemd-spawn.exe";
    const std::string exeHost = JoinPath(runDir, exeName);
    struct stat st {};
    if (::stat(exeHost.c_str(), &st) != 0) {
        error = "Run 目录里找不到 " + exeName + "，请先准备 Run";
        return false;
    }

    spec.program = wine;
    spec.workDir = runDir;
    spec.args.push_back(wine);
    spec.args.push_back("--bottle");
    spec.args.push_back(config.bottle);
    // cnc-ddraw must be loaded instead of Wine's builtin ddraw: the builtin one
    // changes the display mode (breaking the macOS desktop layout) and is much
    // slower for a 2D DirectDraw game.
    spec.args.push_back("--dll");
    spec.args.push_back("ddraw=n");
    if (config.noUpdate) spec.args.push_back("--no-update");
    if (config.waitChildren) spec.args.push_back("--wait-children");
    spec.args.push_back("--workdir");
    spec.args.push_back(runDir);
    spec.args.push_back("--cx-app");
    spec.args.push_back(ToWinePath(exeHost));
    if (plan.useAres) spec.args.push_back("gamemd.exe");
    spec.args.push_back("-SPAWN");
    std::vector<std::string> gameArgs = SplitCommandLine(plan.extraArgs);
    // CnCNet spawner flag: adds the campaign game-speed slider to the in-game
    // options menu.  It is normally in ClientDefinitions.ini already, make sure
    // it is present even if that file is customized.
    bool hasSpeedControl = false;
    for (const std::string& argument : gameArgs)
        if (argument == "-speedcontrol") hasSpeedControl = true;
    if (!hasSpeedControl) gameArgs.push_back("-speedcontrol");
    for (const std::string& argument : gameArgs) spec.args.push_back(argument);

    if (!config.lang.empty()) AppendEnv(spec.env, "LANG", config.lang);
    AppendEnv(spec.env, "WINEDEBUG", config.debugChannels.empty() ? "-all" : config.debugChannels);
    // Keep Rosetta's AVX advertisement deterministic (CrossOver sets this too).
    AppendEnv(spec.env, "ROSETTA_ADVERTISE_AVX", "1");

    std::string logDir = config.logDir;
    if (logDir.empty()) logDir = JoinPath(JoinPath(HomeDir(), "Library/Logs"), "ReunionLite");
    EnsureDir(logDir);
    spec.logPath = JoinPath(logDir, "launch-" + Timestamp() + ".log");

    std::ostringstream command;
    command << "cd " << Quote(runDir) << " &&";
    for (const auto& [key, value] : spec.env) command << " " << key << "=" << Quote(value);
    for (const std::string& argument : spec.args) command << " " << Quote(argument);
    spec.commandLine = command.str();
    return true;
}

int RunGame(const LaunchSpec& spec, std::string& error) {
    error.clear();
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("fork 失败：") + std::strerror(errno);
        return -1;
    }
    if (pid == 0) {
        // Child: log file, working directory, environment, exec.
        const int log = ::open(spec.logPath.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (log >= 0) {
            const std::string header = "# " + spec.commandLine + "\n";
            (void)!::write(log, header.data(), header.size());
            ::dup2(log, STDOUT_FILENO);
            ::dup2(log, STDERR_FILENO);
            ::close(log);
        }
        if (::chdir(spec.workDir.c_str()) != 0) {
            std::fprintf(stderr, "chdir %s: %s\n", spec.workDir.c_str(), std::strerror(errno));
            ::_exit(126);
        }
        for (const auto& [key, value] : spec.env) ::setenv(key.c_str(), value.c_str(), 1);
        std::vector<char*> argv;
        argv.reserve(spec.args.size() + 1);
        for (const std::string& argument : spec.args)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        ::execv(spec.program.c_str(), argv.data());
        std::fprintf(stderr, "exec %s: %s\n", spec.program.c_str(), std::strerror(errno));
        ::_exit(127);
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error = std::string("waitpid 失败：") + std::strerror(errno);
            return -1;
        }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

} // namespace rlmac
