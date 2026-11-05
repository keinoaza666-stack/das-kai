// SPDX-License-Identifier: AGPL-3.0-only
// Adapted from DawAudioStreamer's installer/macos (yoruhinot) for DAS Kai:
// DAS Kai names and texts, Japanese first like the plug-in window, and help
// links that point to the DAS Kai repository instead of the original project.
import AppKit
import Darwin

@main
final class SetupApp: NSObject, NSApplicationDelegate {
    static let repository = "https://github.com/keinoaza666-stack/das-kai"
    private var window: NSWindow!
    private var language: NSSegmentedControl!
    private var titleLabel: NSTextField!
    private var body: NSTextField!
    private var installButton: NSButton!
    private var removeButton: NSButton!
    private var guideButton: NSButton!
    // Shown alone at the bottom right once installing or uninstalling finished,
    // like the last page of a standard installer.
    private var doneButton: NSButton!
    private var progress: NSProgressIndicator!
    private var busy = false
    // Japanese first, like the DAS Kai plug-in window; 日本語 / English switches it.
    private var japanese = true
    private let fm = FileManager.default
    private func t(_ ja: String, _ en: String) -> String { japanese ? ja : en }

    static func main() {
        let app = NSApplication.shared
        let delegate = SetupApp()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        withExtendedLifetime(delegate) { app.run() }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let menu = NSMenu()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit DAS Kai Setup", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        let menuItem = NSMenuItem()
        menuItem.submenu = appMenu
        menu.addItem(menuItem)
        NSApp.mainMenu = menu
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 570, height: 460),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "DAS Kai Setup"
        window.isReleasedWhenClosed = false
        let content = window.contentView!
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 20
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 30),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -30),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 26)
        ])
        language = NSSegmentedControl(labels: ["日本語", "English"], trackingMode: .selectOne, target: self, action: #selector(changeLanguage))
        language.selectedSegment = japanese ? 0 : 1
        stack.addArrangedSubview(language)
        titleLabel = NSTextField(wrappingLabelWithString: "")
        titleLabel.font = .boldSystemFont(ofSize: 23)
        titleLabel.setContentCompressionResistancePriority(.required, for: .vertical)
        stack.addArrangedSubview(titleLabel)
        titleLabel.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        body = NSTextField(wrappingLabelWithString: "")
        body.font = .systemFont(ofSize: 14)
        body.setContentCompressionResistancePriority(.required, for: .vertical)
        stack.addArrangedSubview(body)
        body.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        progress = NSProgressIndicator()
        progress.style = .bar
        progress.isIndeterminate = true
        progress.isHidden = true
        stack.addArrangedSubview(progress)
        progress.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        // Buttons sit at the bottom: help on the left, actions on the right with
        // the main one last, as in macOS's own installers.
        installButton = NSButton(title: "", target: self, action: #selector(install))
        installButton.keyEquivalent = "\r"
        removeButton = NSButton(title: "", target: self, action: #selector(uninstall))
        let actions = NSStackView(views: [removeButton, installButton])
        actions.spacing = 12
        actions.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(actions)
        guideButton = NSButton(title: "", target: self, action: #selector(guide))
        guideButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(guideButton)
        doneButton = NSButton(title: "", target: self, action: #selector(finish))
        doneButton.isHidden = true
        doneButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(doneButton)
        NSLayoutConstraint.activate([
            actions.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            actions.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -22),
            guideButton.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            guideButton.centerYAnchor.constraint(equalTo: actions.centerYAnchor),
            doneButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            doneButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -22),
            doneButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 96),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: actions.topAnchor, constant: -16)
        ])
        refresh()
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func changeLanguage() { japanese = language.selectedSegment == 0; refresh() }
    private func refresh() {
        titleLabel.stringValue = t("DAS Kai をこの Mac に追加", "Add DAS Kai to this Mac")
        body.stringValue = t("DAW と OBS を終了してから進めてください。\n\nこのユーザーに、DAS Kai の VST3・AU と OBS プラグインを入れます。音声デバイスや DAW の設定は変えません。元の DawAudioStreamer が入っていても、そのまま残ります。",
                             "Close your DAW and OBS before continuing.\n\nInstalls the DAS Kai VST3, AU and OBS plugins for this user. Audio devices and DAW settings are not changed. An installed original DawAudioStreamer is left as it is.")
        installButton.title = t("インストール / 更新", "Install / Update")
        removeButton.title = t("アンインストール…", "Uninstall…")
        guideButton.title = t("使い方・困ったとき", "Guide & help")
        doneButton.title = t("完了", "Done")
    }
    @objc private func finish() { NSApp.terminate(nil) }
    @objc private func guide() {
        NSWorkspace.shared.open(URL(string: Self.repository + "#readme")!)
    }
    @objc private func install() { run(install: true) }
    @objc private func uninstall() {
        let alert = NSAlert()
        alert.messageText = t("DAS Kai をアンインストールしますか？", "Uninstall DAS Kai?")
        alert.informativeText = t("このユーザーの DAS Kai(VST3・AU・OBS プラグイン)だけを削除します。DAW のプロジェクトと OBS のシーンは残ります。先に DAW と OBS を終了してください。",
                                 "Removes only this user's DAS Kai (VST3, AU and OBS plugin). DAW projects and OBS scenes are kept. Close your DAW and OBS first.")
        alert.addButton(withTitle: t("キャンセル", "Cancel"))
        alert.addButton(withTitle: t("アンインストール", "Uninstall"))
        if alert.runModal() == .alertSecondButtonReturn { run(install: false) }
    }

    private func run(install: Bool) {
        guard !busy else { return }
        guard geteuid() != 0 else {
            showFailure(SetupIssue(kind: "root", path: fm.homeDirectoryForCurrentUser, detail: "Run Setup from Finder as your normal user, not root."))
            return
        }
        // Do not attempt to kill hosts or save/close the user's projects automatically.
        if NSWorkspace.shared.runningApplications.contains(where: { $0.bundleIdentifier == "com.obsproject.obs-studio" }) {
            showFailure(SetupIssue(kind: "running", path: URL(fileURLWithPath: "/Applications/OBS.app"), detail: "OBS is running."))
            return
        }
        busy = true
        for button in [installButton, removeButton, guideButton] { button?.isEnabled = false }
        language.isEnabled = false
        window.standardWindowButton(.closeButton)?.isEnabled = false
        titleLabel.stringValue = t("処理中…", "Working…")
        body.stringValue = t("確認と処理が終わるまで、この画面を閉じないでください。", "Keep this window open until checks and changes finish.")
        progress.isHidden = false
        progress.startAnimation(nil)
        let core = SetupCore(home: fm.homeDirectoryForCurrentUser,
                             payload: Bundle.main.resourceURL!.appendingPathComponent("payload"),
                             verify: Self.verifySignature)
        DispatchQueue.global(qos: .userInitiated).async {
            let result = Result { try core.run(install: install) }
            DispatchQueue.main.async {
                self.busy = false
                for button in [self.installButton, self.removeButton, self.guideButton] { button?.isEnabled = true }
                self.language.isEnabled = true
                self.window.standardWindowButton(.closeButton)?.isEnabled = true
                self.progress.stopAnimation(nil)
                self.progress.isHidden = true
                switch result {
                case .success(let completion): self.showSuccess(install: install, warnings: completion.warnings)
                case .failure(let error):
                    self.showFailure(error as? SetupIssue ?? SetupIssue(kind: "operation", path: core.home, detail: error.localizedDescription))
                }
            }
        }
    }

    private static func verifySignature(_ url: URL) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
        process.arguments = ["--verify", "--deep", "--strict", url.path]
        // Drain while the process runs; no pipe-buffer deadlock on diagnostic output.
        let pipe = Pipe()
        process.standardError = pipe
        process.standardOutput = FileHandle.nullDevice
        try process.run()
        let diagnostics = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw NSError(domain: "CodeSignature", code: Int(process.terminationStatus),
                          userInfo: [NSLocalizedDescriptionKey: String(decoding: diagnostics, as: UTF8.self)])
        }
    }

    private func showSuccess(install: Bool, warnings: [String]) {
        // Nothing is left to choose: only Done remains, at the bottom right.
        for control in [installButton, removeButton, guideButton] as [NSControl] {
            control.isHidden = true
            control.isEnabled = false
        }
        installButton.keyEquivalent = ""
        language.isHidden = true
        doneButton.isHidden = false
        doneButton.keyEquivalent = "\r"
        window.makeFirstResponder(doneButton)
        titleLabel.stringValue = install ? t("インストール完了", "Installation complete") : t("アンインストール完了", "Uninstall complete")
        body.stringValue = install
            ? t("DAW と OBS を開き直してください。\n1. DAW のマスターの最後に DAS Kai を1個挿します。\n2. OBS の「ソース」に「DAS Kai（DAW）」を追加します。設定は不要です。\n3. DAW を再生し、プラグイン画面の OBS 欄が「受信中」になることを確かめます。\n\nDAW に出ない時は、プラグインを再スキャンしてください。",
                "Reopen your DAW and OBS.\n1. Insert one DAS Kai at the end of your DAW master.\n2. In OBS Sources, add DAS Kai (DAW). No settings are needed.\n3. Play your DAW and check that the OBS row in the plugin window shows OK.\n\nIf DAS Kai is missing in your DAW, rescan plugins.")
            : t("DAS Kai のプラグインを削除しました。\nDAW のプロジェクトと OBS のシーンは変えていません。\nSetup が不要なら、ゴミ箱へ移せます。",
                "DAS Kai has been removed.\nDAW projects and OBS scenes were not changed.\nYou can move Setup to the Trash if you no longer need it.")
        if !warnings.isEmpty {
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = t("処理は終わりましたが、後片付けが残っています", "Completed, but cleanup needs attention")
            alert.informativeText = t("一時ファイルを削除できませんでした。もう一度実行する前に、この内容を DAS Kai の GitHub ページでお知らせください。\n", "Some temporary files could not be removed. Report these details on the DAS Kai GitHub page before running Setup again.\n") + warnings.joined(separator: "\n")
            alert.addButton(withTitle: "OK")
            alert.addButton(withTitle: t("詳細をコピー", "Copy details"))
            if alert.runModal() == .alertSecondButtonReturn { copy(warnings.joined(separator: "\n")) }
        }
    }

    private func showFailure(_ issue: SetupIssue) {
        titleLabel.stringValue = t("完了していません", "Setup did not complete")
        let advice: String
        switch issue.kind {
        case "permission":
            advice = t("インストール先に書き込めません。入っているプラグインは変えていません。［Finder で表示］から［情報を見る］の［共有とアクセス権］を確かめてください。変えられない時は、この Mac の管理者に相談してください。",
                       "Cannot write to the destination. Installed plugins were not changed. Reveal it in Finder and check Get Info > Sharing & Permissions. Ask the Mac's administrator if you cannot change access.")
        case "payload":
            advice = t("必要なファイルが足りないか、確かめられません。GitHub のリリースページから ZIP をダウンロードし直し、全体を展開してください。入っているプラグインは変えていません。",
                       "Required files are missing or could not be verified. Download the ZIP again from the GitHub releases page and extract all of it. Installed plugins were not changed.")
        case "path":
            advice = t("インストール先がふつうのフォルダーではないため、止めました。リンクや入っているファイルは変えていません。DAS Kai の GitHub ページでお知らせください。",
                       "The destination is not a regular folder, so Setup stopped. Links and installed files were not changed. Report it on the DAS Kai GitHub page.")
        case "recovery":
            advice = t("元に戻すためのファイルが残っています。もう一度実行したり手で消したりせず、［詳細をコピー］して DAS Kai の GitHub ページでお知らせください。",
                       "Recovery files remain. Do not retry or delete them by hand. Copy the details and report them on the DAS Kai GitHub page.")
        case "busy": advice = t("別の Setup を終了してから、もう一度お試しください。", "Close other copies of Setup and try again.")
        case "root": advice = t("ふつうのユーザーで、Finder から Setup を開いてください。", "Open Setup from Finder as your normal user.")
        case "running": advice = t("OBS を終了してから、もう一度お試しください。", "Quit OBS and try again.")
        default:
            advice = t("ファイルの更新に失敗しました。変えたプラグインは、処理の前の状態に戻しました。DAW と OBS を終了し、空き容量とフォルダーのアクセス権を確かめてから、もう一度お試しください。",
                       "Updating files failed. Changed plugins were restored to their previous state. Close your DAW and OBS, check free space and folder access, then retry.")
        }
        body.stringValue = advice
        let details = "\(issue.kind)\n\(issue.path.path)\n\(issue.detail)\n" + issue.recovery.map(\.path).joined(separator: "\n")
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = titleLabel.stringValue
        alert.informativeText = advice + "\n\n" + issue.path.path
        alert.addButton(withTitle: t("閉じる", "Close"))
        alert.addButton(withTitle: t("Finder で表示", "Show in Finder"))
        alert.addButton(withTitle: t("詳細をコピー", "Copy details"))
        switch alert.runModal() {
        case .alertSecondButtonReturn:
            var folder = issue.path
            while !fm.fileExists(atPath: folder.path) && folder.path != "/" { folder.deleteLastPathComponent() }
            NSWorkspace.shared.activateFileViewerSelecting([folder])
        case .alertThirdButtonReturn: copy(details)
        default: break
        }
    }
    private func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply { busy ? .terminateCancel : .terminateNow }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
