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

    var resolvedTrainerPath: String {
        trainerPath ?? (NSHomeDirectory() + "/Documents/ra2master/红色警戒2修改大师.exe")
    }
}
