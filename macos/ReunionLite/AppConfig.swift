import Foundation

struct OptionsConfig: Codable {
    var modId: String = ""
    var difficulty: Int = 1
    var credits: Int = 100000
    var speed: Int = 5
    var autosave: Int = 0
    var music: Int = 50
    var movies: Bool = true
    var satellite: Bool = false
    var ares: Bool = false
    var phobos: Bool = false
    var terrain: Bool = false
    var autoRepair: Bool = true
    var corr: Bool = true
    var reverse: Bool = false
}

struct AppConfig: Codable {
    var gameRoot: String = NSHomeDirectory() + "/Documents/ra2reunion"
    var crossOverRoot: String = "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
    var bottle: String = "RA2"
    var width: Int = 1920
    var height: Int = 1080
    var renderer: String = "gdi"
    var lastMission: Int? = nil          // legacy (per-pack index)
    var lastMissionUID: String? = nil
    var hideOnLaunch: Bool = true
    var defaultsLoaded: Bool = false
    var favorites: [String]? = nil
    var engine: String? = nil          // "crossover" (default) | "vera20k"
    var vera20kPath: String? = nil
    var trainerPath: String? = nil
    var autoStartTrainer: Bool? = nil

    var autoStartTrainerEnabled: Bool { autoStartTrainer ?? true }
    var options: OptionsConfig = OptionsConfig()

    static var supportDirectory: URL {
        let url = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ReunionLite", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    static var configURL: URL { supportDirectory.appendingPathComponent("config.json") }

    static func load() -> AppConfig {
        guard let data = try? Data(contentsOf: configURL),
              let config = try? JSONDecoder().decode(AppConfig.self, from: data) else {
            return AppConfig()
        }
        return config
    }

    func save() {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(self) else { return }
        try? data.write(to: AppConfig.configURL, options: .atomic)
    }

    var resolutionLabel: String { "\(width) × \(height)" }

    var resolvedEngine: String { engine ?? "crossover" }

    var resolvedVera20kPath: String {
        if let vera20kPath, !vera20kPath.isEmpty { return vera20kPath }
        // A bundled copy next to ReunionLite.app wins, then the dev build and
        // the installed trainer build.
        let sibling = Bundle.main.bundleURL.deletingLastPathComponent()
            .appendingPathComponent("VERA20K.app/Contents/MacOS/VERA20K").path
        let candidates = [
            sibling,
            NSHomeDirectory() + "/vscode/vera20k/target/release/vera20k",
            "/Applications/VERA20K-trainer.app/Contents/MacOS/VERA20K-trainer",
            "/Applications/VERA20K.app/Contents/MacOS/VERA20K",
        ]
        return candidates.first { FileManager.default.fileExists(atPath: $0) } ?? candidates[0]
    }

    var resolvedTrainerPath: String {
        trainerPath ?? (NSHomeDirectory() + "/Documents/ra2master/红色警戒2修改大师.exe")
    }
}
