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
            if config.hideOnLaunch {
                NSApp.hide(nil)
            }
        } catch {
            isLaunching = false
            status = "启动失败"
            lastError = "无法启动 CrossOver：\(error.localizedDescription)"
        }
    }

    private func finishLaunch(exitCode: Int32, logPath: String) {
        runningProcess = nil
        isLaunching = false
        status = exitCode == 0 ? "游戏已退出" : "游戏退出（代码 \(exitCode)）"
        NSApp.unhide(nil)
        NSApp.activate(ignoringOtherApps: true)
        let root = config.gameRoot
        DispatchQueue.global(qos: .utility).async {
            let client = CoreClient(root: root)
            client?.sync()
        }
    }

    func stopGame() {
        runningProcess?.terminate()
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
