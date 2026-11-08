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
