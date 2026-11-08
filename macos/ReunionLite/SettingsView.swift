import SwiftUI

struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss

    private let resolutions = ["1024x768", "1280x720", "1920x1080", "2560x1440"]

    var body: some View {
        @Bindable var model = model
        return VStack(spacing: 0) {
            HStack {
                Text("设置").font(.title2).bold()
                Spacer()
                Button("完成") { dismiss() }.keyboardShortcut(.defaultAction)
            }
            .padding(16)
            Divider()

            Form {
                Section("路径") {
                    LabeledContent("游戏目录") {
                        HStack {
                            TextField("", text: $model.config.gameRoot)
                                .textFieldStyle(.roundedBorder)
                            Button("选择…") { model.chooseGameRoot() }
                        }
                    }
                    LabeledContent("CrossOver") {
                        HStack {
                            TextField("", text: $model.config.crossOverRoot)
                                .textFieldStyle(.roundedBorder)
                            Button("选择…") { model.chooseCrossOverRoot() }
                        }
                    }
                    TextField("Bottle 名称", text: $model.config.bottle)
                }

                Section("引擎") {
                    Picker("游戏引擎", selection: Binding(
                        get: { model.config.resolvedEngine },
                        set: { model.config.engine = $0 }
                    )) {
                        Text("CrossOver（原版 gamemd）").tag("crossover")
                        Text("VERA20K（实验）").tag("vera20k")
                    }
                    if model.config.resolvedEngine == "vera20k" {
                        LabeledContent("VERA20K 可执行文件") {
                            HStack {
                                TextField("", text: Binding(
                                    get: { model.config.resolvedVera20kPath },
                                    set: { model.config.vera20kPath = $0 }
                                ))
                                .textFieldStyle(.roundedBorder)
                                Button("选择…") { model.chooseVera20kBinary() }
                            }
                        }
                        Text("实验引擎：不需要 CrossOver/Rosetta，也不会启动修改器；使用 Run/ 作为 ra2_dir，并自动挂载 Mod&AI 里的 AI 根。")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Section("画面") {
                    Picker("窗口分辨率", selection: resolutionBinding) {
                        ForEach(resolutions, id: \.self) { value in
                            Text(value.replacingOccurrences(of: "x", with: " × ")).tag(value)
                        }
                    }
                    Picker("渲染器", selection: $model.config.renderer) {
                        Text("GDI（推荐）").tag("gdi")
                        Text("OpenGL").tag("opengl")
                        Text("Direct3D 9").tag("direct3d9")
                    }
                    Text("游戏以 1:1 或拉伸方式填满窗口；默认 1080p。GDI 在 Wine 下最稳定。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Toggle("启动游戏后隐藏启动器（省电）", isOn: $model.config.hideOnLaunch)
                }

                Section("修改器") {
                    LabeledContent("程序路径") {
                        HStack {
                            TextField("", text: Binding(
                                get: { model.config.resolvedTrainerPath },
                                set: { model.config.trainerPath = $0 }
                            ))
                            .textFieldStyle(.roundedBorder)
                            Button("选择…") { model.chooseTrainer() }
                        }
                    }
                    Toggle("启动游戏时同时启动修改器", isOn: Binding(
                        get: { model.config.autoStartTrainerEnabled },
                        set: { model.config.autoStartTrainer = $0 }
                    ))
                    Text("修改器必须和游戏运行在同一个 CrossOver bottle（RA2）里，才能读写 gamemd 的内存。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Section("客户端默认值") {
                    HStack {
                        Text("从 ReunionLite.ini / CampaignSetting.ini 读取")
                            .font(.callout)
                        Spacer()
                        Button("恢复默认选项") { model.resetToClientDefaults() }
                    }
                    if !CoreClient.drives().isEmpty {
                        Text("盘符映射：\(CoreClient.drives())")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Section("诊断") {
                    HStack {
                        Button("打开日志目录") { model.openLogFolder() }
                        Button("重新扫描") { model.rescan(force: true) }
                    }
                }
            }
            .formStyle(.grouped)
        }
        .frame(width: 560, height: 520)
    }

    private var resolutionBinding: Binding<String> {
        Binding(
            get: { "\(model.config.width)x\(model.config.height)" },
            set: { value in
                let parts = value.split(separator: "x")
                guard parts.count == 2,
                      let width = Int(parts[0]),
                      let height = Int(parts[1]) else { return }
                model.config.width = width
                model.config.height = height
            }
        )
    }
}
