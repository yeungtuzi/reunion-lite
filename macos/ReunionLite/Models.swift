import Foundation

struct ModDTO: Codable, Identifiable, Hashable {
    let id: String
    let name: String
}

struct MissionDTO: Codable, Identifiable, Hashable {
    let ordinal: Int
    let index: Int
    let name: String
    let description: String
    let pack: String
    let packId: String
    let scenario: String
    let path: String
    let side: String
    let sideValue: Int
    let difficulty: String
    let modList: String
    let defaultMod: String
    let section: String
    let sourceFile: String
    let badReason: String
    let ares: Bool
    let phobos: Bool
    let tx: Bool
    let yr: Bool
    let buildOffAlly: Bool
    let alwaysNormal: Bool
    let launchable: Bool
    let mods: [ModDTO]

    var id: String { uid }

    /// Globally unique: `index` is only the position inside one Battle*.ini
    /// and repeats across packs, `sourceFile|section` does not.
    var uid: String { "\(sourceFile)|\(section)" }

    var shortDescription: String {
        description.replacingOccurrences(of: "@", with: "\n")
    }
}

struct PackDTO: Codable, Identifiable, Hashable {
    let id: String
    let name: String
    var key: String { name }
}

struct IssueDTO: Codable, Hashable {
    let file: String
    let message: String
}

struct ScanResult: Codable {
    let ok: Bool
    let gameRoot: String
    let scanMillis: Int
    let missions: [MissionDTO]
    let packs: [PackDTO]
    let issues: [IssueDTO]
}

struct PlanInfo: Codable {
    let modId: String
    let modName: String
    let scenario: String
    let runDir: String
    let ares: Bool
    let phobos: Bool
    let tx: Bool
    let warnings: [String]
}

struct LaunchResult: Codable {
    let ok: Bool
    let error: String?
    let mission: MissionDTO?
    let plan: PlanInfo?
    let program: String?
    let workDir: String?
    let logPath: String?
    let command: String?
    let args: [String]?
    let env: [String: String]?
}

struct ClientDefaultsResult: Codable {
    struct Options: Codable {
        let difficulty: Int
        let credits: Int
        let gameSpeed: Int
        let autosave: Int
        let musicVolume: Int
        let playMovies: Bool
        let satellite: Bool
        let ares: Bool
        let phobos: Bool
        let terrain: Bool
        let autoRepair: Bool
        let corr: Bool
        let reverse: Bool
    }
    let ok: Bool
    let found: Bool
    let lastMission: String
    let options: Options
    let drives: String
}
