import AppKit
import Foundation

@Observable
@MainActor
final class AppModel {
    var config: AppConfig {
        didSet { scheduleConfigSave() }
    }
    var missions: [MissionDTO] = []
    var packs: [PackDTO] = []
    var issues: [IssueDTO] = []
    var scanMillis: Int = 0
    var search: String = "" {
        didSet { scheduleFilter() }
    }
    var packFilter: String? = nil {
        didSet { scheduleFilter() }
    }
    var showFavoritesOnly: Bool = false {
        didSet { scheduleFilter() }
    }
    private(set) var favoriteCount: Int = 0
    /// Cached favorite ids so row bodies never rebuild a Set from config.
    private(set) var favoriteUIDs: Set<String> = []
    private(set) var filteredMissions: [MissionDTO] = []
    private(set) var packCounts: [String: Int] = [:]
    var selection: String? = nil
    var status: String = "就绪"
    var isScanning = false
    var isLaunching = false
    var showSettings = false
    var lastError: String?
    var logPath: String?
    var warnings: [String] = []
    private(set) var runningProcess: Process?
    private(set) var trainerProcess: Process?
    var lastLaunchDate: Date?

    private var searchKeys: [String: String] = [:]
    private var filterWorkItem: DispatchWorkItem?
    private var saveWorkItem: DispatchWorkItem?

    init() {
        config = AppConfig.load()
        favoriteUIDs = Set(config.favorites ?? [])
        selection = config.lastMissionUID
        status = "加载缓存…"

        // Never touch the game directory on the main thread: the first access
        // may trigger a macOS "Documents" permission prompt, and blocking here
        // would prevent that prompt from ever being shown.
        let root = config.gameRoot
        let hasRoot = FileManager.default.fileExists(atPath: root)
        guard hasRoot else {
            status = "游戏目录不存在"
            return
        }
        DispatchQueue.global(qos: .userInitiated).async {
            let started = CFAbsoluteTimeGetCurrent()
            let scan = CoreClient(root: root)?.scanBinary(force: false)
            DispatchQueue.main.async {
                guard let scan else {
                    self.rescan(force: false)
                    return
                }
                self.apply(scan)
                if scan.missions.isEmpty {
                    self.status = "没有读到任务"
                    self.lastError = "无法读取游戏目录。请在“系统设置 → 隐私与安全性 → 文件与文件夹”中允许 Reunion Lite 访问“文档”。"
                    return
                }
                if ProcessInfo.processInfo.environment["REUNION_DEBUG"] != nil {
                    let ms = (CFAbsoluteTimeGetCurrent() - started) * 1000
                    let line = "startup: scanBinary \(String(format: "%.1f", ms)) ms, \(self.missions.count) missions, cached=\(scan.cached)\n"
                    try? line.write(toFile: "/tmp/rl-startup.txt", atomically: true, encoding: .utf8)
                }
                self.status = scan.cached
                    ? "缓存命中 · \(self.missions.count) 条任务"
                    : "扫描完成 · \(scan.scanMillis) ms"
            }
        }
    }

    var selectedMission: MissionDTO? {
        guard let uid = selection else { return nil }
        return missions.first { $0.uid == uid }
    }

    var launchableCount: Int {
        packCounts.values.reduce(0, +)
    }

    // MARK: - Filtering

    private func scheduleFilter() {
        filterWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in
            MainActor.assumeIsolated { self?.applyFilter() }
        }
        filterWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }

    private func applyFilter() {
        let needle = search.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        let pack = packFilter
        let favorites = favoriteUIDs
        var result: [MissionDTO] = []
        result.reserveCapacity(missions.count)
        var favoriteHit = 0
        for mission in missions where mission.launchable {
            if favorites.contains(mission.uid) { favoriteHit += 1 }
            if showFavoritesOnly && !favorites.contains(mission.uid) { continue }
            if let pack, mission.pack != pack { continue }
            if !needle.isEmpty {
                let key = searchKeys[mission.uid] ?? ""
                if !key.contains(needle) { continue }
            }
            result.append(mission)
        }
        filteredMissions = result
        favoriteCount = favoriteHit
    }

    func isFavorite(_ mission: MissionDTO) -> Bool {
        favoriteUIDs.contains(mission.uid)
    }

    func toggleFavorite(_ mission: MissionDTO) {
        var favorites = config.favorites ?? []
        if favoriteUIDs.contains(mission.uid) {
            favorites.removeAll { $0 == mission.uid }
            favoriteUIDs.remove(mission.uid)
        } else {
            favorites.append(mission.uid)
            favoriteUIDs.insert(mission.uid)
        }
        config.favorites = favorites
        scheduleFilter()
    }

    private func rebuildSearchIndex() {
        var keys: [String: String] = [:]
        keys.reserveCapacity(missions.count)
        var counts: [String: Int] = [:]
        for mission in missions where mission.launchable {
            // Keyed by uid: Mission.index is not unique across packs.
            keys[mission.uid] = "\(mission.name)\u{1}\(mission.pack)\u{1}\(mission.scenario)\u{1}\(mission.side)\u{1}\(mission.section)".lowercased()
            counts[mission.pack, default: 0] += 1
        }
        searchKeys = keys
        packCounts = counts
    }

    // MARK: - Config persistence

    private func scheduleConfigSave() {
        saveWorkItem?.cancel()
        let snapshot = config
        let work = DispatchWorkItem { snapshot.save() }
        saveWorkItem = work
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + 0.5, execute: work)
    }

    func saveNow() {
        saveWorkItem?.cancel()
        config.save()
    }

    // MARK: - Scanning

    func rescan(force: Bool) {
        guard !isScanning else { return }
        let root = config.gameRoot
        guard FileManager.default.fileExists(atPath: root) else {
            status = "游戏目录不存在"
            lastError = "找不到 \(root)，请先在设置里选择游戏目录"
            return
        }
        let needDefaults = !config.defaultsLoaded
        isScanning = true
        status = "扫描任务中…"
        lastError = nil
        DispatchQueue.global(qos: .userInitiated).async {
            let client = CoreClient(root: root)
            let result = client?.scanBinary(force: force)
            let defaults = needDefaults ? CoreClient.clientDefaults(root: root) : nil
            DispatchQueue.main.async {
                self.isScanning = false
                guard let result else {
                    self.status = "扫描失败"
                    self.lastError = "无法读取游戏目录：\(root)"
                    return
                }
                self.apply(result)
                self.applyDefaultsIfNeeded(defaults)
                self.status = "\(self.launchableCount) 个可启动任务 · 扫描 \(result.scanMillis) ms"
            }
        }
    }

    private func apply(_ scan: BinaryCampaign) {
        missions = scan.missions
        packs = scan.packs
        issues = scan.issues
        scanMillis = scan.scanMillis
        rebuildSearchIndex()
        applyFilter()
        restoreSelectionIfPossible()
    }

    private func applyDefaultsIfNeeded(_ defaults: ClientDefaultsResult?) {
        guard !config.defaultsLoaded, let defaults else { return }
        var options = OptionsConfig()
        options.difficulty = defaults.options.difficulty
        options.credits = defaults.options.credits
        options.speed = 5
        options.autosave = defaults.options.autosave
        options.music = defaults.options.musicVolume
        options.movies = defaults.options.playMovies
        options.satellite = defaults.options.satellite
        options.ares = defaults.options.ares
        options.phobos = defaults.options.phobos
        options.terrain = defaults.options.terrain
        options.autoRepair = defaults.options.autoRepair
        options.corr = defaults.options.corr
        options.reverse = defaults.options.reverse
        config.options = options
        config.defaultsLoaded = true
    }

    private func restoreSelectionIfPossible() {
        if let uid = config.lastMissionUID, missions.contains(where: { $0.uid == uid }) {
            selection = uid
        } else if let legacy = config.lastMission,
                  let match = missions.first(where: { $0.index == legacy }) {
            selection = match.uid
            config.lastMissionUID = match.uid
        } else if selection == nil, let first = filteredMissions.first {
            selection = first.uid
        }
    }

    func select(_ mission: MissionDTO) {
        selection = mission.uid
        config.lastMissionUID = mission.uid
        config.lastMission = mission.index
        if !config.options.modId.isEmpty,
           !mission.mods.contains(where: { $0.id == config.options.modId }) {
            config.options.modId = ""
        }
    }

    // MARK: - Launching

    func launch() {
        guard let mission = selectedMission, !isLaunching, runningProcess == nil else { return }
        if config.resolvedEngine == "vera20k" {
            launchVera20k(mission)
            return
        }
        let configSnapshot = config
        let missionOrdinal = mission.ordinal
        isLaunching = true
        lastError = nil
        warnings = []
        status = "准备 Run…"
        DispatchQueue.global(qos: .userInitiated).async {
            let client = CoreClient(root: configSnapshot.gameRoot)
            let result = client?.launch(
                missionOrdinal: missionOrdinal,
                options: configSnapshot.options,
                width: configSnapshot.width,
                height: configSnapshot.height,
                renderer: configSnapshot.renderer,
                config: configSnapshot
            )
            DispatchQueue.main.async {
                self.handleLaunchResult(result, mission: mission)
            }
        }
    }

    private func handleLaunchResult(_ result: LaunchResult?, mission: MissionDTO) {
        guard let result, result.ok,
              let program = result.program,
              let args = result.args,
              let workDir = result.workDir,
              let logPath = result.logPath else {
            isLaunching = false
            status = "启动失败"
            lastError = result?.error ?? "无法准备启动参数"
            return
        }
        warnings = result.plan?.warnings ?? []
        self.logPath = logPath

        FileManager.default.createFile(atPath: logPath, contents: nil)
        let handle = try? FileHandle(forWritingTo: URL(fileURLWithPath: logPath))
        if let handle, let command = result.command {
            handle.write(Data("# \(command)\n".utf8))
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: program)
        process.arguments = args
        process.currentDirectoryURL = URL(fileURLWithPath: workDir)
        var environment = ProcessInfo.processInfo.environment
        for (key, value) in result.env ?? [:] { environment[key] = value }
        process.environment = environment
        process.standardOutput = handle ?? FileHandle.nullDevice
        process.standardError = handle ?? FileHandle.nullDevice
        process.terminationHandler = { [weak self] finished in
            guard let self else { return }
            DispatchQueue.main.async {
                self.finishLaunch(exitCode: finished.terminationStatus, logPath: logPath)
            }
        }
        do {
            try process.run()
            runningProcess = process
            isLaunching = true
            lastLaunchDate = Date()
            config.lastMissionUID = mission.uid
            config.lastMission = mission.index
            status = "游戏运行中：\(mission.name)"
            if config.autoStartTrainerEnabled {
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
                    _ = self.launchTrainer()
                }
            }
            if config.hideOnLaunch {
                NSApp.hide(nil)
            }
        } catch {
            isLaunching = false
            status = "启动失败"
            lastError = "无法启动 CrossOver：\(error.localizedDescription)"
        }
    }

    // MARK: - VERA20K (experimental engine)

    private func launchVera20k(_ mission: MissionDTO) {
        let snapshot = config
        let ordinal = mission.ordinal
        isLaunching = true
        lastError = nil
        warnings = []
        status = "准备 VERA20K…"
        DispatchQueue.global(qos: .userInitiated).async {
            let client = CoreClient(root: snapshot.gameRoot)
            let result = client?.prepare(missionIndex: ordinal, options: snapshot.options,
                                         width: snapshot.width, height: snapshot.height,
                                         renderer: snapshot.renderer)
            DispatchQueue.main.async {
                guard let result, result.ok, let runDir = result.runDir,
                      let scenario = result.scenario, let gameRoot = result.gameRoot else {
                    self.isLaunching = false
                    self.status = "VERA20K 准备失败"
                    let detail = "ok=\(result?.ok ?? false) err=\(result?.error ?? "nil") run=\(result?.runDir ?? "nil") scenario=\(result?.scenario ?? "nil") root=\(result?.gameRoot ?? "nil")"
                    try? detail.write(toFile: "/tmp/rl-vera-fail.txt", atomically: true, encoding: .utf8)
                    self.lastError = result?.error ?? "无法准备 Run"
                    return
                }
                self.warnings = result.plan?.warnings ?? []
                self.startVera20k(runDir: runDir, scenario: scenario, gameRoot: gameRoot,
                                  mission: mission, config: snapshot)
            }
        }
    }

    private func startVera20k(runDir: String, scenario: String, gameRoot: String,
                              mission: MissionDTO, config snapshot: AppConfig) {
        let executable = snapshot.resolvedVera20kPath
        guard FileManager.default.fileExists(atPath: executable) else {
            isLaunching = false
            status = "找不到 VERA20K"
            lastError = "找不到 VERA20K 可执行文件：\(executable)\n请在设置里指定路径。"
            return
        }
        do {
            try installAiRoot(gameRoot: gameRoot, runDir: runDir, scenario: scenario)
            ensureMoviesArchive(runDir: runDir)
            try writeVeraConfig(runDir: runDir)
            try writeVeraSpawnIni(runDir: runDir)
            applyVeraGameSpeed(snapshot.options.speed)
        } catch {
            isLaunching = false
            status = "VERA20K 准备失败"
            lastError = error.localizedDescription
            return
        }

        let logDir = NSHomeDirectory() + "/Library/Logs/ReunionLite"
        try? FileManager.default.createDirectory(atPath: logDir, withIntermediateDirectories: true)
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let logPath = "\(logDir)/vera-\(stamp).log"
        self.logPath = logPath
        FileManager.default.createFile(atPath: logPath, contents: nil)
        let handle = try? FileHandle(forWritingTo: URL(fileURLWithPath: logPath))

        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = ["-SPAWN", "-SPEEDCONTROL"]
        process.currentDirectoryURL = URL(fileURLWithPath: runDir)
        var environment = ProcessInfo.processInfo.environment
        environment["RA2_DIR"] = runDir
        environment["RA2_SPAWN_INI"] = runDir + "/spawn-vera.ini"
        environment["VERA_TOLERANT_AI"] = "1"
        // Our VERA fork drops to 15 fps when the window is not focused, which
        // also slows the simulation; keep the same pace as focused play.
        environment["VERA_FPS_CAP"] = "60"
        environment["VERA_FPS_CAP_IDLE"] = "15"
        environment["LANG"] = "zh_CN.GBK"
        process.environment = environment
        process.standardOutput = handle ?? FileHandle.nullDevice
        process.standardError = handle ?? FileHandle.nullDevice
        process.terminationHandler = { [weak self] finished in
            DispatchQueue.main.async {
                self?.runningProcess = nil
                self?.isLaunching = false
                self?.status = finished.terminationStatus == 0
                    ? "VERA20K 已退出" : "VERA20K 退出（代码 \(finished.terminationStatus)）"
                NSApp.unhide(nil)
                NSApp.activate(ignoringOtherApps: true)
            }
        }
        do {
            try process.run()
            runningProcess = process
            lastLaunchDate = Date()
            NSRunningApplication(processIdentifier: process.processIdentifier)?
                .activate(options: [.activateIgnoringOtherApps])
            config.lastMissionUID = mission.uid
            config.lastMission = mission.index
            status = "VERA20K 运行中：\(mission.name)"
            if config.hideOnLaunch { NSApp.hide(nil) }
        } catch {
            isLaunching = false
            status = "VERA20K 启动失败"
            lastError = "无法启动 VERA20K：\(error.localizedDescription)"
        }
    }

    private func installAiRoot(gameRoot: String, runDir: String, scenario: String) throws {
        let aiDir = gameRoot + "/Mod&AI"
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: aiDir) else {
            throw NSError(domain: "ReunionLite", code: 1, userInfo: [
                NSLocalizedDescriptionKey: "找不到 Mod&AI 目录，无法选择 AI 根"
            ])
        }
        var fallback: String?
        for name in entries {
            let dir = aiDir + "/" + name
            let aimd = dir + "/aimd.ini"
            guard FileManager.default.fileExists(atPath: aimd) else { continue }
            if fallback == nil { fallback = aimd }
            let files = (try? FileManager.default.contentsOfDirectory(atPath: dir)) ?? []
            if files.contains(where: { $0.caseInsensitiveCompare(scenario) == .orderedSame }) {
                try? FileManager.default.removeItem(atPath: runDir + "/aimd.ini")
                try FileManager.default.copyItem(atPath: aimd, toPath: runDir + "/aimd.ini")
                return
            }
        }
        guard let fallback else {
            throw NSError(domain: "ReunionLite", code: 2, userInfo: [
                NSLocalizedDescriptionKey: "找不到可用的 aimd.ini（Mod&AI 里没有 AI 包），VERA20K 无法载入任务"
            ])
        }
        try? FileManager.default.removeItem(atPath: runDir + "/aimd.ini")
        try FileManager.default.copyItem(atPath: fallback, toPath: runDir + "/aimd.ini")
    }

    /// Reunion's merged asset set omits the base game's movie archive; VERA's
    /// archive mount requires it to exist, so give it a valid empty MIX.
    private func ensureMoviesArchive(runDir: String) {
        let path = runDir + "/movies03.mix"
        guard !FileManager.default.fileExists(atPath: path) else { return }
        try? Data([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]).write(to: URL(fileURLWithPath: path))
    }

    /// The gamemd spawn.ini is written in the system ANSI codepage (GBK) for
    /// the Wine path, but VERA requires its launch file to be UTF-8.
    private func writeVeraSpawnIni(runDir: String) throws {
        let source = runDir + "/spawn.ini"
        let data = try Data(contentsOf: URL(fileURLWithPath: source))
        let gb18030 = String.Encoding(rawValue: CFStringConvertEncodingToNSStringEncoding(
            CFStringEncoding(CFStringEncodings.GB_18030_2000.rawValue)))
        var text = String(data: data, encoding: gb18030)
            ?? String(data: data, encoding: .utf8)
            ?? ""
        try text.write(toFile: runDir + "/spawn-vera.ini", atomically: true, encoding: .utf8)
    }

    /// VERA reads its campaign pace from VERA.INI's [Options] GameSpeed (the
    /// spawn.ini value alone is not enough for the campaign screen), so mirror
    /// the launcher's chosen speed into the engine profile before starting.
    private func applyVeraGameSpeed(_ speed: Int) {
        let path = NSHomeDirectory() + "/Library/Application Support/VERA20k/VERA.INI"
        guard var text = try? String(contentsOfFile: path, encoding: .utf8) else { return }
        var lines = text.components(separatedBy: "\n")
        var section = ""
        for index in lines.indices {
            let line = lines[index].trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("[") && line.hasSuffix("]") {
                section = String(line.dropFirst().dropLast())
                continue
            }
            if section == "Options" && line.hasPrefix("GameSpeed=") {
                lines[index] = "GameSpeed=\(6 - max(0, min(6, speed)))"
            }
        }
        text = lines.joined(separator: "\n")
        try? text.write(toFile: path, atomically: true, encoding: .utf8)
    }

    private func writeVeraConfig(runDir: String) throws {
        let settings = AppConfig.supportDirectory.appendingPathComponent("vera20k-settings").path
        try? FileManager.default.createDirectory(atPath: settings, withIntermediateDirectories: true)
        let text = """
        [paths]
        ra2_dir = "\(runDir)"
        settings_dir = "\(settings)"

        [ini]
        profile = "yr"

        [gameplay]
        input_delay_ticks = 15
        pause_on_focus_loss = false
        """
        try text.write(toFile: runDir + "/config.toml", atomically: true, encoding: .utf8)
    }

    private func finishLaunch(exitCode: Int32, logPath: String) {
        runningProcess = nil
        isLaunching = false
        status = exitCode == 0 ? "游戏已退出" : "游戏退出（代码 \(exitCode)）"
        NSApp.unhide(nil)
        NSApp.activate(ignoringOtherApps: true)
        guard config.resolvedEngine != "vera20k" else { return }
        let root = config.gameRoot
        DispatchQueue.global(qos: .utility).async {
            let client = CoreClient(root: root)
            client?.sync()
        }
    }

    func stopGame() {
        runningProcess?.terminate()
    }

    // MARK: - Trainer (RA2Master)

    var trainerAvailable: Bool {
        FileManager.default.fileExists(atPath: config.resolvedTrainerPath)
    }

    var trainerRunning: Bool {
        trainerProcess?.isRunning == true
    }

    private func winePath(_ hostPath: String) -> String {
        "Z:" + hostPath.replacingOccurrences(of: "/", with: "\\")
    }

    @discardableResult
    func launchTrainer() -> Bool {
        guard !trainerRunning else { return true }
        let trainer = config.resolvedTrainerPath
        guard FileManager.default.fileExists(atPath: trainer) else {
            lastError = "找不到修改器：\(trainer)\n可在设置里指定路径。"
            return false
        }
        let wine = config.crossOverRoot + "/bin/wine"
        guard FileManager.default.fileExists(atPath: wine) else {
            lastError = "找不到 CrossOver wine：\(wine)"
            return false
        }
        let dir = (trainer as NSString).deletingLastPathComponent
        let process = Process()
        process.executableURL = URL(fileURLWithPath: wine)
        process.arguments = [
            "--bottle", config.bottle,
            "--no-update",
            "--workdir", dir,
            "--cx-app", winePath(trainer),
        ]
        var environment = ProcessInfo.processInfo.environment
        environment["LANG"] = "zh_CN.GBK"
        process.environment = environment
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            trainerProcess = process
            status = "修改器已启动"
            return true
        } catch {
            lastError = "无法启动修改器：\(error.localizedDescription)"
            return false
        }
    }

    // MARK: - Dialogs

    func chooseGameRoot() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: config.gameRoot)
        panel.prompt = "选择"
        panel.message = "选择包含 Maps、Mod&AI、Run 和 gamemd.exe 的游戏目录"
        if panel.runModal() == .OK, let url = panel.url {
            config.gameRoot = url.path
            config.defaultsLoaded = false
            missions = []
            filteredMissions = []
            rescan(force: true)
        }
    }

    func chooseCrossOverRoot() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.prompt = "选择"
        panel.message = "选择 CrossOver.app/Contents/SharedSupport/CrossOver 目录"
        if panel.runModal() == .OK, let url = panel.url {
            config.crossOverRoot = url.path
        }
    }

    func chooseTrainer() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: (config.resolvedTrainerPath as NSString).deletingLastPathComponent)
        panel.prompt = "选择"
        panel.message = "选择修改器（红色警戒2修改大师.exe）"
        if panel.runModal() == .OK, let url = panel.url {
            config.trainerPath = url.path
        }
    }

    func chooseVera20kBinary() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = URL(fileURLWithPath: (config.resolvedVera20kPath as NSString).deletingLastPathComponent)
        panel.prompt = "选择"
        panel.message = "选择 VERA20K 可执行文件"
        if panel.runModal() == .OK, let url = panel.url {
            config.vera20kPath = url.path
        }
    }

    func openLog() {
        guard let logPath else { return }
        NSWorkspace.shared.open(URL(fileURLWithPath: logPath))
    }

    func openLogFolder() {
        NSWorkspace.shared.open(URL(fileURLWithPath: NSHomeDirectory() + "/Library/Logs/ReunionLite"))
    }

    func resetToClientDefaults() {
        let root = config.gameRoot
        DispatchQueue.global(qos: .userInitiated).async {
            let defaults = CoreClient.clientDefaults(root: root)
            DispatchQueue.main.async {
                guard let defaults else { return }
                var options = OptionsConfig()
                options.difficulty = defaults.options.difficulty
                options.credits = defaults.options.credits
                options.speed = 5
                options.autosave = defaults.options.autosave
                options.music = defaults.options.musicVolume
                options.movies = defaults.options.playMovies
                options.satellite = defaults.options.satellite
                options.ares = defaults.options.ares
                options.phobos = defaults.options.phobos
                options.terrain = defaults.options.terrain
                options.autoRepair = defaults.options.autoRepair
                options.corr = defaults.options.corr
                options.reverse = defaults.options.reverse
                self.config.options = options
                self.status = "已恢复客户端默认选项"
            }
        }
    }
}
