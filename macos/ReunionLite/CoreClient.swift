import Foundation
import ReunionCore

/// Thin wrapper around the C ABI exported by the vendored core.
final class CoreClient {
    private var context: OpaquePointer?

    init?(root: String) {
        guard let created = root.withCString({ rl_create($0) }) else { return nil }
        context = created
    }

    deinit {
        if let context { rl_destroy(context) }
    }

    func scan(force: Bool) -> ScanResult? {
        guard let context else { return nil }
        return decode(rl_scan_json(context, force ? 1 : 0))
    }

    /// Fast path: fingerprint + binary campaign cache in the game directory,
    /// serialized to a compact buffer.  No JSON and no full directory walk.
    func scanBinary(force: Bool) -> BinaryCampaign? {
        guard let context else { return nil }
        var size: UInt = 0
        guard let buffer = rl_scan_binary(context, force ? 1 : 0, &size), size > 0 else { return nil }
        defer { free(buffer) }
        let data = Data(bytes: buffer, count: Int(size))
        return BinaryCampaign.decode(data)
    }

    func launch(missionOrdinal: Int, options: OptionsConfig, width: Int, height: Int,
                renderer: String, config: AppConfig) -> LaunchResult? {
        guard let context else { return nil }

        let modC = dup(options.modId)
        let rendererC = dup(renderer)
        let rootC = dup(config.crossOverRoot)
        let bottleC = dup(config.bottle)
        let langC = dup("zh_CN.GBK")
        defer {
            free(modC); free(rendererC); free(rootC); free(bottleC); free(langC)
        }

        var cOptions = rl_options(
            mod_id: modC.map { UnsafePointer($0) },
            difficulty: Int32(options.difficulty),
            credits: Int32(options.credits),
            game_speed: Int32(options.speed),
            autosave: Int32(options.autosave),
            music_volume: Int32(options.music),
            play_movies: options.movies ? 1 : 0,
            satellite: options.satellite ? 1 : 0,
            ares: options.ares ? 1 : 0,
            phobos: options.phobos ? 1 : 0,
            terrain: options.terrain ? 1 : 0,
            auto_repair: options.autoRepair ? 1 : 0,
            corr: options.corr ? 1 : 0,
            reverse: options.reverse ? 1 : 0
        )
        var cVideo = rl_video(
            width: Int32(width),
            height: Int32(height),
            renderer: rendererC.map { UnsafePointer($0) }
        )
        var cWine = rl_wine(
            crossover_root: rootC.map { UnsafePointer($0) },
            bottle: bottleC.map { UnsafePointer($0) },
            lang: langC.map { UnsafePointer($0) },
            log_dir: nil
        )

        return decode(rl_launch_json(context, Int32(missionOrdinal), &cOptions, &cVideo, &cWine))
    }

    func prepare(missionIndex: Int, options: OptionsConfig, width: Int, height: Int,
                 renderer: String) -> PrepareResult? {
        guard let context else { return nil }
        let modC = dup(options.modId)
        let rendererC = dup(renderer)
        defer { free(modC); free(rendererC) }
        var cOptions = rl_options(
            mod_id: modC.map { UnsafePointer($0) },
            difficulty: Int32(options.difficulty),
            credits: Int32(options.credits),
            game_speed: Int32(options.speed),
            autosave: Int32(options.autosave),
            music_volume: Int32(options.music),
            play_movies: options.movies ? 1 : 0,
            satellite: options.satellite ? 1 : 0,
            ares: options.ares ? 1 : 0,
            phobos: options.phobos ? 1 : 0,
            terrain: options.terrain ? 1 : 0,
            auto_repair: options.autoRepair ? 1 : 0,
            corr: options.corr ? 1 : 0,
            reverse: options.reverse ? 1 : 0
        )
        var cVideo = rl_video(
            width: Int32(width),
            height: Int32(height),
            renderer: rendererC.map { UnsafePointer($0) }
        )
        guard let raw = rl_prepare_json(context, Int32(missionIndex), &cOptions, &cVideo) else { return nil }
        defer { rl_free_string(raw) }
        let data = Data(bytes: raw, count: Int(strlen(raw)))
        do {
            return try JSONDecoder().decode(PrepareResult.self, from: data)
        } catch {
            try? data.write(to: URL(fileURLWithPath: "/tmp/rl-prepare-raw.json"))
            try? "prepare decode error: \(error)\n".write(toFile: "/tmp/rl-prepare-error.txt", atomically: true, encoding: .utf8)
            return nil
        }
    }

    func sync() {
        guard let context else { return }
        if let raw = rl_sync_json(context) { rl_free_string(raw) }
    }

    static func clientDefaults(root: String) -> ClientDefaultsResult? {
        guard let raw = root.withCString({ rl_client_defaults_json($0) }) else { return nil }
        defer { rl_free_string(raw) }
        let data = Data(bytes: raw, count: Int(strlen(raw)))
        return try? JSONDecoder().decode(ClientDefaultsResult.self, from: data)
    }

    static func drives() -> String {
        guard let raw = rl_drives_json() else { return "" }
        defer { rl_free_string(raw) }
        guard let result = try? JSONDecoder().decode([String: String].self,
                                                     from: Data(bytes: raw, count: Int(strlen(raw)))),
              let drives = result["drives"] else { return "" }
        return drives
    }

    private func decode<T: Decodable>(_ raw: UnsafeMutablePointer<CChar>?) -> T? {
        guard let raw else { return nil }
        defer { rl_free_string(raw) }
        let data = Data(bytes: raw, count: Int(strlen(raw)))
        return try? JSONDecoder().decode(T.self, from: data)
    }

    private func dup(_ value: String?) -> UnsafeMutablePointer<CChar>? {
        guard let value, !value.isEmpty else { return nil }
        return strdup(value)
    }
}
