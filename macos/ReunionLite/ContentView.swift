import AppKit
import SwiftUI

struct ContentView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        return VStack(spacing: 0) {
            TopBar()
            Divider()
            HSplitView {
                PackSidebar()
                    .frame(minWidth: 190, idealWidth: 220, maxWidth: 280)
                MissionTableView()
                    .frame(minWidth: 520, idealWidth: 660)
                InspectorView()
                    .frame(minWidth: 340, idealWidth: 380, maxWidth: 430)
            }
            Divider()
            StatusBar()
        }
        .frame(minWidth: 1180, minHeight: 720)
        .sheet(isPresented: $model.showSettings) {
            SettingsView().environment(model)
        }
        .alert("出错了", isPresented: Binding(
            get: { model.lastError != nil },
            set: { if !$0 { model.lastError = nil } }
        )) {
            Button("好") { model.lastError = nil }
        } message: {
            Text(model.lastError ?? "")
        }
    }
}

private struct TopBar: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        return HStack(spacing: 10) {
            Button {
                model.chooseGameRoot()
            } label: {
                Label("游戏目录", systemImage: "folder")
            }
            .help("选择游戏目录")

            Text(model.config.gameRoot)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: 300, alignment: .leading)

            Spacer(minLength: 12)

            HStack(spacing: 4) {
                Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                TextField("搜索任务 / 任务包 / 场景 / 阵营", text: $model.search)
                    .textFieldStyle(.plain)
                    .frame(width: 290)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(RoundedRectangle(cornerRadius: 6).fill(Color(nsColor: .textBackgroundColor)))

            Button {
                model.rescan(force: true)
            } label: {
                Label("重新扫描", systemImage: "arrow.clockwise")
            }
            .disabled(model.isScanning)

            Button {
                model.launchTrainer()
            } label: {
                Label("修改器", systemImage: "wrench.and.screwdriver")
            }
            .help("在同一个 CrossOver bottle 里启动《红色警戒2修改大师》")
            .disabled(!model.trainerAvailable)

            Button {
                model.showSettings = true
            } label: {
                Label("设置", systemImage: "gearshape")
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }
}

private struct PackSidebar: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        List {
            row(title: "收藏", count: model.favoriteCount,
                selected: model.showFavoritesOnly, icon: "star.fill", tint: .orange) {
                model.showFavoritesOnly = true
                model.packFilter = nil
            }
            row(title: "全部任务包", count: model.launchableCount,
                selected: !model.showFavoritesOnly && model.packFilter == nil) {
                model.showFavoritesOnly = false
                model.packFilter = nil
            }
            ForEach(model.packs) { pack in
                let count = model.packCounts[pack.name] ?? 0
                if count > 0 {
                    row(title: pack.name, count: count,
                        selected: !model.showFavoritesOnly && model.packFilter == pack.name) {
                        model.showFavoritesOnly = false
                        model.packFilter = pack.name
                    }
                }
            }
        }
        .listStyle(.sidebar)
    }

    private func row(title: String, count: Int, selected: Bool, icon: String? = nil,
                     tint: Color = .secondary, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if let icon {
                    Image(systemName: icon)
                        .font(.caption2)
                        .foregroundStyle(tint)
                }
                Text(title)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .font(.callout)
                Spacer(minLength: 4)
                Text("\(count)")
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
                    .monospacedDigit()
            }
            .contentShape(Rectangle())
            .padding(.vertical, 2)
            .padding(.horizontal, 4)
            .background(
                RoundedRectangle(cornerRadius: 5)
                    .fill(selected ? Color.accentColor.opacity(0.22) : Color.clear)
            )
        }
        .buttonStyle(.plain)
        .listRowInsets(EdgeInsets(top: 1, leading: 6, bottom: 1, trailing: 6))
    }
}

private struct MissionTableView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        VStack(spacing: 0) {
            if model.isScanning && model.missions.isEmpty {
                VStack(spacing: 10) {
                    ProgressView()
                    Text("正在扫描任务…").foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if model.filteredMissions.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "magnifyingglass")
                        .font(.system(size: 28))
                        .foregroundStyle(.tertiary)
                    Text(model.missions.isEmpty ? "还没有扫描到任务" : "没有匹配的任务")
                        .foregroundStyle(.secondary)
                    if model.missions.isEmpty {
                        Button("选择游戏目录") { model.chooseGameRoot() }
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                header
                Divider()
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(model.filteredMissions) { mission in
                            MissionRow(mission: mission,
                                       selected: model.selection == mission.uid,
                                       favorite: model.isFavorite(mission))
                                .contentShape(Rectangle())
                                .onTapGesture {
                                    model.selection = mission.uid
                                    model.select(mission)
                                }
                                .contextMenu {
                                    Button(model.isFavorite(mission) ? "取消收藏" : "收藏") {
                                        model.toggleFavorite(mission)
                                    }
                                    Divider()
                                    Button("启动此任务") {
                                        model.selection = mission.uid
                                        model.select(mission)
                                        model.launch()
                                    }
                                }
                            Divider().opacity(0.35)
                        }
                    }
                }
            }
        }
    }

    private var header: some View {
        HStack(spacing: 8) {
            Text("任务名称").frame(maxWidth: .infinity, alignment: .leading)
            Text("任务包").frame(width: 130, alignment: .leading)
            Text("阵营").frame(width: 55, alignment: .leading)
            Text("难度").frame(width: 55, alignment: .leading)
            Text("场景").frame(width: 100, alignment: .leading)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 12)
        .padding(.vertical, 5)
        .background(Color(nsColor: .controlBackgroundColor))
    }
}

private struct MissionRow: View {
    let mission: MissionDTO
    let selected: Bool
    let favorite: Bool

    var body: some View {
        HStack(spacing: 8) {
            Group {
                if favorite {
                    Image(systemName: "star.fill")
                        .font(.caption2)
                        .foregroundStyle(.orange)
                } else {
                    Color.clear
                }
            }
            .frame(width: 12)
            Text(mission.name)
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
            Text(mission.pack)
                .lineLimit(1)
                .foregroundStyle(.secondary)
                .frame(width: 130, alignment: .leading)
            Text(mission.side)
                .lineLimit(1)
                .frame(width: 55, alignment: .leading)
            Text(mission.difficulty)
                .lineLimit(1)
                .frame(width: 55, alignment: .leading)
            Text(mission.scenario)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .frame(width: 100, alignment: .leading)
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .padding(.vertical, 3)
        .background(selected ? Color.accentColor.opacity(0.22) : Color.clear)
    }
}

private struct InspectorView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        VStack(spacing: 0) {
            if let mission = model.selectedMission {
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        header(mission)
                        Divider()
                        GameOptionsPanel(mission: mission)
                        if !model.warnings.isEmpty {
                            warningBox
                        }
                    }
                    .padding(16)
                }
                Divider()
                launchBar(mission)
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "gamecontroller")
                        .font(.system(size: 30))
                        .foregroundStyle(.tertiary)
                    Text("选择一个任务").foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func header(_ mission: MissionDTO) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text(mission.name).font(.title3).bold()
                Spacer(minLength: 4)
                Button {
                    model.toggleFavorite(mission)
                } label: {
                    Image(systemName: model.isFavorite(mission) ? "star.fill" : "star")
                        .foregroundStyle(model.isFavorite(mission) ? Color.orange : Color.secondary)
                }
                .buttonStyle(.plain)
                .help(model.isFavorite(mission) ? "取消收藏" : "收藏")
            }
            HStack(spacing: 6) {
                tag(mission.pack)
                if !mission.side.isEmpty { tag(mission.side) }
                tag(mission.difficulty)
                if mission.ares { tag("Ares") }
                if mission.phobos { tag("Phobos") }
                if mission.tx { tag("TX") }
            }
            Text(mission.scenario).font(.caption).foregroundStyle(.secondary)
            if !mission.shortDescription.isEmpty {
                Text(mission.shortDescription)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(.top, 2)
            }
        }
    }

    private func tag(_ text: String) -> some View {
        Text(text)
            .font(.caption2)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Capsule().fill(Color.secondary.opacity(0.15)))
    }

    private var warningBox: some View {
        VStack(alignment: .leading, spacing: 4) {
            Label("提示", systemImage: "exclamationmark.triangle")
                .font(.caption).foregroundStyle(.orange)
            ForEach(model.warnings, id: \.self) { warning in
                Text(warning).font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color.orange.opacity(0.08)))
    }

    private func launchBar(_ mission: MissionDTO) -> some View {
        VStack(spacing: 6) {
            Button {
                model.launch()
            } label: {
                HStack {
                    if model.isLaunching && model.runningProcess == nil {
                        ProgressView().controlSize(.small)
                    } else {
                        Image(systemName: "play.fill")
                    }
                    Text(model.runningProcess == nil ? "启动游戏" : "游戏运行中…")
                }
                .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(model.isLaunching || model.isScanning)

            if model.runningProcess != nil {
                Button("结束游戏") { model.stopGame() }
                    .buttonStyle(.link)
            }

            HStack {
                Text(model.status)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                Spacer()
                if let log = model.logPath {
                    Button("日志") { NSWorkspace.shared.open(URL(fileURLWithPath: log)) }
                        .buttonStyle(.link)
                        .font(.caption)
                }
            }
        }
        .padding(12)
    }
}

private struct GameOptionsPanel: View {
    @Environment(AppModel.self) private var model
    let mission: MissionDTO

    var body: some View {
        @Bindable var model = model
        return VStack(alignment: .leading, spacing: 10) {
            Text("游戏选项").font(.headline)

            Picker("模组", selection: $model.config.options.modId) {
                Text("默认（\(mission.defaultMod.isEmpty ? "YR" : mission.defaultMod)）").tag("")
                ForEach(mission.mods) { mod in
                    Text(mod.name).tag(mod.id)
                }
            }

            Picker("难度", selection: $model.config.options.difficulty) {
                Text("简单").tag(0)
                Text("中等").tag(1)
                Text("困难").tag(2)
            }
            .pickerStyle(.segmented)

            Picker("初始资金", selection: $model.config.options.credits) {
                Text("保持地图").tag(-1)
                Text("10,000").tag(10000)
                Text("50,000").tag(50000)
                Text("100,000").tag(100000)
            }

            Picker("游戏速度", selection: $model.config.options.speed) {
                ForEach(0...6, id: \.self) { value in
                    Text("\(value)").tag(value)
                }
            }
            .pickerStyle(.segmented)

            Picker("自动存档", selection: $model.config.options.autosave) {
                Text("关闭").tag(0)
                Text("5 分钟").tag(1)
                Text("10 分钟").tag(2)
                Text("20 分钟").tag(3)
            }

            Picker("游戏音乐", selection: $model.config.options.music) {
                Text("保持").tag(-1)
                Text("关闭").tag(0)
                Text("30%").tag(30)
                Text("50%").tag(50)
                Text("70%").tag(70)
                Text("100%").tag(100)
            }

            Divider()

            Toggle("播放简报 / 过场", isOn: $model.config.options.movies)
            Toggle("透明迷雾", isOn: $model.config.options.satellite)
            Toggle("地形扩展", isOn: $model.config.options.terrain)
            Toggle("自动维修", isOn: $model.config.options.autoRepair)
            Toggle("数值修复", isOn: $model.config.options.corr)
            Toggle("军备反转", isOn: $model.config.options.reverse)

            DisclosureGroup("扩展平台") {
                VStack(alignment: .leading, spacing: 6) {
                    Toggle("Ares（Syringe）", isOn: $model.config.options.ares)
                    Toggle("Phobos", isOn: $model.config.options.phobos)
                }
                .padding(.top, 4)
            }
            .font(.callout)
        }
        .font(.callout)
    }
}

private struct StatusBar: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        HStack(spacing: 8) {
            if model.isScanning || model.isLaunching {
                ProgressView().controlSize(.small)
            }
            Text(model.status).font(.caption).foregroundStyle(.secondary)
            Spacer()
            Text("\(model.filteredMissions.count) / \(model.missions.count) 个任务")
                .font(.caption)
                .foregroundStyle(.tertiary)
            Text(model.config.resolutionLabel)
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
    }
}
