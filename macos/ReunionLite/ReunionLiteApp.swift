import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(windowBecameKey(_:)),
            name: NSWindow.didBecomeKeyNotification,
            object: nil
        )
    }

    @objc private func windowBecameKey(_ note: Notification) {
        guard let window = note.object as? NSWindow, window.title == "Reunion Lite" else { return }
        if window.frameAutosaveName.isEmpty {
            // AppKit persists position + size under this name and restores it
            // automatically on the next launch.
            window.setFrameAutosaveName("ReunionLiteMainWindow")
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}

@main
struct ReunionLiteApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    private let model = AppModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(model)
        }
        .defaultSize(width: 1280, height: 820)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("游戏") {
                Button("启动所选任务") { model.launch() }
                    .keyboardShortcut(.return, modifiers: [.command])
                    .disabled(model.selectedMission == nil || model.isLaunching)
                Button("重新扫描") { model.rescan(force: true) }
                    .keyboardShortcut("r", modifiers: [.command])
                Divider()
                Button("设置…") { model.showSettings = true }
                    .keyboardShortcut(",", modifiers: [.command])
                Button("打开日志") { model.openLog() }
                    .disabled(model.logPath == nil)
            }
        }
    }
}
