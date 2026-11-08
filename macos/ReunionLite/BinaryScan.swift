import Foundation

struct BinaryCampaign {
    let scanMillis: Int
    let cached: Bool
    let issues: [IssueDTO]
    let packs: [PackDTO]
    let missions: [MissionDTO]

    static func decode(_ data: Data) -> BinaryCampaign? {
        var reader = ByteReader(data: data)
        guard reader.string(4) == "RLMB", reader.u32() == 1 else { return nil }
        let scanMillis = Int(reader.u32())
        let cached = reader.u8() != 0

        let issueCount = Int(reader.u32())
        var issues: [IssueDTO] = []
        issues.reserveCapacity(issueCount)
        for _ in 0..<issueCount {
            issues.append(IssueDTO(file: reader.str(), message: reader.str()))
        }

        let packCount = Int(reader.u32())
        var packs: [PackDTO] = []
        packs.reserveCapacity(packCount)
        for _ in 0..<packCount {
            packs.append(PackDTO(id: reader.str(), name: reader.str()))
        }

        let missionCount = Int(reader.u32())
        var missions: [MissionDTO] = []
        missions.reserveCapacity(missionCount)
        for ordinal in 0..<missionCount {
            let index = Int(reader.u32())
            let name = reader.str()
            let description = reader.str()
            let pack = reader.str()
            let packId = reader.str()
            let scenario = reader.str()
            let path = reader.str()
            let side = reader.str()
            let sideValue = Int(reader.u32())
            let difficulty = reader.str()
            let modList = reader.str()
            let defaultMod = reader.str()
            let section = reader.str()
            let sourceFile = reader.str()
            let badReason = reader.str()
            let flags = reader.u8()
            let modCount = Int(reader.u32())
            var mods: [ModDTO] = []
            mods.reserveCapacity(modCount)
            for _ in 0..<modCount {
                mods.append(ModDTO(id: reader.str(), name: reader.str()))
            }
            missions.append(MissionDTO(
                ordinal: ordinal, index: index, name: name, description: description, pack: pack,
                packId: packId, scenario: scenario, path: path, side: side,
                sideValue: sideValue, difficulty: difficulty, modList: modList,
                defaultMod: defaultMod, section: section, sourceFile: sourceFile,
                badReason: badReason,
                ares: flags & 1 != 0, phobos: flags & 2 != 0, tx: flags & 4 != 0,
                yr: flags & 8 != 0, buildOffAlly: flags & 16 != 0,
                alwaysNormal: flags & 32 != 0, launchable: flags & 64 != 0,
                mods: mods
            ))
        }
        return BinaryCampaign(scanMillis: scanMillis, cached: cached,
                             issues: issues, packs: packs, missions: missions)
    }
}

private struct ByteReader {
    let data: Data
    var offset = 0

    init(data: Data) { self.data = data }

    mutating func u8() -> UInt8 {
        guard offset < data.count else { return 0 }
        defer { offset += 1 }
        return data[offset]
    }

    mutating func u16() -> UInt16 {
        let lo = UInt16(u8())
        let hi = UInt16(u8())
        return lo | (hi << 8)
    }

    mutating func u32() -> UInt32 {
        let b0 = UInt32(u8())
        let b1 = UInt32(u8())
        let b2 = UInt32(u8())
        let b3 = UInt32(u8())
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
    }

    mutating func string(_ count: Int) -> String? {
        guard offset + count <= data.count else { return nil }
        defer { offset += count }
        return String(decoding: data[offset..<(offset + count)], as: UTF8.self)
    }

    mutating func str() -> String {
        let length = Int(u16())
        guard offset + length <= data.count else {
            offset = data.count
            return ""
        }
        defer { offset += length }
        return String(decoding: data[offset..<(offset + length)], as: UTF8.self)
    }
}
