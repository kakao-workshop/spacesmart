import AppKit
import ServiceManagement

@main struct SpaceSmartMain {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var statusItem: NSStatusItem!
    var bridge: BluetoothBridge?
    lazy var customPanel = CustomPanel()
    lazy var lightPanel = LightPanel()
    enum Page: Int, CaseIterable {
        case overview, light, custom, agents, settings
        var title: String { ["개요", "조명", "커스텀", "AI 연동", "설정"][rawValue] }
        var symbol: String { ["square.grid.2x2", "lightbulb", "rectangle.split.3x1", "sparkles", "gearshape"][rawValue] }
    }
    let installer = HookInstaller()
    var pages: [Page: NSView] = [:]
    var navigation: [NSButton] = []
    var selectedPage: Page = .overview
    let content = NSView()
    let sidebarStatus = NSTextField(wrappingLabelWithString: "연결 전")
    let overviewHint = NSTextField(wrappingLabelWithString: "")
    let demoButton = NSButton(title: "Codex 표시 시험", target: nil, action: nil)
    let devicePicker = NSPopUpButton()
    let connectionLabel = NSTextField(wrappingLabelWithString: "Bluetooth 준비 중")
    let metricsLabel = NSTextField(labelWithString: "")
    let eventLabel = NSTextField(wrappingLabelWithString: "AI 연동은 선택 사항입니다. PC 상태와 시계는 바로 사용할 수 있습니다.")
    let connectButton = NSButton(title: "연결", target: nil, action: nil)
    let loginCheck = NSButton(checkboxWithTitle: "Mac 로그인 시 자동 실행", target: nil, action: nil)
    var preview: Bool { CommandLine.arguments.contains("--preview") }
    var smoke: Bool { CommandLine.arguments.contains("--smoke-test") }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.appearance = NSAppearance(named: .darkAqua)
        let menu = NSMenu()
        for page in Page.allCases {
            let item = menu.addItem(withTitle: page == .overview ? "SpaceSmart 열기 · 개요" : page.title, action: #selector(navigate(_:)), keyEquivalent: String(page.rawValue + 1))
            item.tag = page.rawValue
        }
        menu.addItem(.separator())
        menu.addItem(withTitle: "설치 가이드", action: #selector(openGuide), keyEquivalent: "")
        menu.addItem(withTitle: "설정…", action: #selector(openSettings), keyEquivalent: ",")
        menu.addItem(.separator())
        menu.addItem(withTitle: "SpaceSmart 종료", action: #selector(quit), keyEquivalent: "q")
        menu.items.forEach { $0.target = self }
        let mainMenu = NSMenu(), appItem = NSMenuItem(title: "SpaceSmart", action: nil, keyEquivalent: "")
        appItem.submenu = menu.copy() as? NSMenu
        mainMenu.addItem(appItem)
        let editItem = NSMenuItem(title: "편집", action: nil, keyEquivalent: "")
        let edit = NSMenu(title: "편집")
        for (title, action, key) in [("실행 취소", "undo:", "z"), ("잘라내기", "cut:", "x"), ("복사", "copy:", "c"), ("붙여넣기", "paste:", "v"), ("전체 선택", "selectAll:", "a")] {
            edit.addItem(withTitle: title, action: Selector(action), keyEquivalent: key)
        }
        let redo = edit.insertItem(withTitle: "다시 실행", action: NSSelectorFromString("redo:"), keyEquivalent: "z", at: 1)
        redo.keyEquivalentModifierMask = [.command, .shift]
        editItem.submenu = edit; mainMenu.addItem(editItem); NSApp.mainMenu = mainMenu
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        statusItem.button?.image = NSImage(systemSymbolName: "display", accessibilityDescription: "SpaceSmart")
        statusItem.menu = menu
        buildWindow(); showWindow(); refresh()
        if preview {
            connectionLabel.stringValue = "연결 전 · 전원을 켠 뒤 내 SpaceSmart를 선택하세요."
            devicePicker.addItem(withTitle: "기기를 찾으면 여기에 표시됩니다")
        } else {
            bridge = BluetoothBridge()
            bridge?.onChange = { [weak self] in self?.refresh() }
            lightPanel.bind(bridge?.light)
            refresh()
            if smoke {
                DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
                    if let id = self?.bridge?.devices.keys.first { self?.bridge?.connect(id) }
                }
                DispatchQueue.main.asyncAfter(deadline: .now() + 18) { [weak self] in
                    guard let self else { return }
                    let report: [String: Any] = ["connected":self.bridge?.connected ?? false, "status":self.bridge?.status ?? "", "metrics":self.bridge?.metrics.lastPC ?? ""]
                    try? privateDirectory(self.installer.paths.support)
                    if let data = try? JSONSerialization.data(withJSONObject: report, options: .prettyPrinted) { try? privateWrite(data, to: self.installer.paths.support.appendingPathComponent("smoke-test.json")) }
                    NSApp.terminate(nil)
                }
            }
        }
    }
    func buildWindow() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 984, height: 720), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "SpaceSmart"; window.isReleasedWhenClosed = false
        window.contentMinSize = NSSize(width: 984, height: 620); window.center()
        window.backgroundColor = NSColor(calibratedWhite: 0.055, alpha: 1)
        let root = WorkspaceCanvas(height: 720); root.frame.size.width = 984; root.autoresizingMask = [.width, .height]
        window.contentView = root
        let sidebar = WorkspaceCanvas(height: 720); sidebar.frame.size.width = 184
        sidebar.autoresizingMask = [.height]; sidebar.wantsLayer = true
        sidebar.layer?.backgroundColor = NSColor(calibratedWhite: 0.085, alpha: 1).cgColor
        root.addSubview(sidebar)
        WorkspaceUI.label(sidebar, "SpaceSmart", 20, 26, 150, 30, size: 19, bold: true)
        WorkspaceUI.label(sidebar, "내 책상과 연결", 22, 59, 140, size: 11, dim: true)
        for page in Page.allCases {
            let button = NSButton(title: "  " + page.title, target: self, action: #selector(navigate(_:)))
            button.image = NSImage(systemSymbolName: page.symbol, accessibilityDescription: nil)
            button.imagePosition = .imageLeading; button.imageScaling = .scaleProportionallyDown
            button.alignment = .left; button.isBordered = false; button.font = .systemFont(ofSize: 14, weight: .medium)
            button.tag = page.rawValue; button.wantsLayer = true; button.layer?.cornerRadius = 8
            button.setAccessibilityLabel(page.title); button.toolTip = "⌘" + String(page.rawValue + 1)
            WorkspaceUI.place(sidebar, button, 12, CGFloat(112 + page.rawValue * 48), 160, 40)
            navigation.append(button)
        }
        WorkspaceUI.place(sidebar, sidebarStatus, 20, 600, 144, 48)
        sidebarStatus.font = .systemFont(ofSize: 11); sidebarStatus.textColor = .secondaryLabelColor
        sidebarStatus.autoresizingMask = [.minYMargin]
        let footer = WorkspaceUI.label(sidebar, "Mac 1.3.0", 20, 674, 145, size: 11, dim: true)
        footer.autoresizingMask = [.minYMargin]
        WorkspaceUI.place(root, content, 184, 0, 800, 720); content.autoresizingMask = [.width, .height]
        lightPanel.onConnect = { [weak self] in self?.select(.overview) }
        let views: [Page: NSView] = [.overview: buildOverview(), .light: lightPanel.view, .custom: customPanel.view, .agents: buildAgents(), .settings: buildSettings()]
        for (page, view) in views {
            let surface: NSView
            if page == .light {
                view.frame = content.bounds; view.autoresizingMask = [.width, .height]; surface = view
            } else { surface = WorkspaceUI.scroll(view, frame: content.bounds) }
            content.addSubview(surface); pages[page] = surface
        }
        select(.overview)
    }
    func select(_ page: Page) {
        window.makeFirstResponder(nil)
        selectedPage = page
        pages.forEach { $0.value.isHidden = $0.key != page }
        for button in navigation {
            let selected = button.tag == page.rawValue
            button.contentTintColor = selected ? WorkspaceUI.accent : .secondaryLabelColor
            button.layer?.backgroundColor = selected ? NSColor(calibratedWhite: 0.17, alpha: 1).cgColor : NSColor.clear.cgColor
            button.setAccessibilityValue(selected ? "선택됨" : "")
        }
        window.title = "SpaceSmart · " + page.title
    }
    @objc func navigate(_ sender: AnyObject) {
        let tag = (sender as? NSButton)?.tag ?? (sender as? NSMenuItem)?.tag ?? 0
        select(Page(rawValue: tag) ?? .overview); showWindow()
    }
    func buildOverview() -> NSView {
        let view = WorkspaceCanvas(height: 680)
        WorkspaceUI.heading(view, "개요", "기기를 연결하고, 오늘 필요한 기능으로 바로 이동하세요.")
        WorkspaceUI.card(view, 124, 210)
        WorkspaceUI.label(view, "디바이스 연결", 44, 144, 500, size: 16, bold: true)
        WorkspaceUI.place(view, connectionLabel, 44, 180, 710, 42); connectionLabel.font = .systemFont(ofSize: 13)
        WorkspaceUI.place(view, devicePicker, 40, 230, 426, 30); devicePicker.setAccessibilityLabel("연결할 SpaceSmart")
        WorkspaceUI.place(view, connectButton, 474, 228, 130, 32); connectButton.bezelStyle = .rounded
        connectButton.target = self; connectButton.action = #selector(connect)
        WorkspaceUI.button(view, "다시 찾기", 612, 228, 146, target: self, action: #selector(rescan))
        WorkspaceUI.place(view, overviewHint, 44, 276, 706, 42); overviewHint.font = .systemFont(ofSize: 12); overviewHint.textColor = .secondaryLabelColor
        WorkspaceUI.card(view, 350, 98)
        WorkspaceUI.label(view, "Mac 상태 · 2초마다 전송", 44, 367, 700, size: 13, bold: true)
        WorkspaceUI.place(view, metricsLabel, 44, 400, 706, 30)
        metricsLabel.font = .monospacedDigitSystemFont(ofSize: 16, weight: .medium)
        WorkspaceUI.label(view, "바로 가기", 28, 478, 700, size: 16, bold: true)
        for (page, title, x) in [(Page.light, "조명 조절", CGFloat(24)), (.custom, "디스플레이 구성", 280), (.agents, "AI 작업 연동", 536)] {
            WorkspaceUI.button(view, title, x, 516, 240, target: self, action: #selector(navigate(_:)), tag: page.rawValue)
        }
        WorkspaceUI.label(view, "어느 화면에서든 왼쪽 메뉴로 이동할 수 있습니다.  ⌘1–5로도 전환하세요.", 30, 574, 730, 44, size: 12, dim: true)
        WorkspaceUI.label(view, "창을 닫아도 연결은 유지됩니다. 메뉴 막대의 SpaceSmart 아이콘으로 다시 여세요.", 30, 625, 730, 36, size: 11, dim: true)
        return view
    }
    func buildAgents() -> NSView {
        let view = WorkspaceCanvas(height: 750)
        WorkspaceUI.heading(view, "AI 연동", "사용하는 앱만 연결하세요. 프롬프트와 응답 본문은 수집하지 않습니다.")
        WorkspaceUI.card(view, 124, 126)
        WorkspaceUI.label(view, "Claude · Code 탭", 44, 144, 450, size: 16, bold: true)
        WorkspaceUI.label(view, "연동 설치 후 Claude의 새 Code 작업에서 확인하세요.", 44, 183, 450, 42, size: 12, dim: true)
        WorkspaceUI.button(view, "Claude 연동 설치", 548, 174, 208, target: self, action: #selector(installHooks(_:)), tag: 0)
        WorkspaceUI.card(view, 266, 158)
        WorkspaceUI.label(view, "Codex", 44, 286, 450, size: 16, bold: true)
        WorkspaceUI.label(view, "설치 → 훅 검토 → /hooks에서 직접 신뢰 설정", 44, 324, 700, size: 12, dim: true)
        WorkspaceUI.button(view, "1. Codex 연동 설치", 40, 369, 230, target: self, action: #selector(installHooks(_:)), tag: 1)
        WorkspaceUI.button(view, "2. Codex 훅 검토 열기", 282, 369, 264, target: self, action: #selector(reviewHooks))
        WorkspaceUI.place(view, eventLabel, 30, 442, 738, 52); eventLabel.font = .systemFont(ofSize: 12); eventLabel.textColor = WorkspaceUI.accent
        WorkspaceUI.card(view, 508, 130)
        WorkspaceUI.label(view, "디바이스 표시 확인", 44, 528, 440, size: 15, bold: true)
        WorkspaceUI.label(view, "기기를 연결하고 ‘AI 작업’ 화면을 여세요.\n36초 동안 작업 중 → 입력 대기 → 응답 완료를 표시합니다.", 44, 562, 468, 52, size: 12, dim: true)
        WorkspaceUI.place(view, demoButton, 548, 560, 208, 32); demoButton.bezelStyle = .rounded
        demoButton.target = self; demoButton.action = #selector(demo)
        WorkspaceUI.button(view, "AI 연동 해제…", 24, 670, 180, target: self, action: #selector(removeHooks))
        WorkspaceUI.label(view, "SpaceSmart가 설치한 연동만 해제합니다.", 220, 676, 530, size: 12, dim: true)
        return view
    }
    func buildSettings() -> NSView {
        let view = WorkspaceCanvas(height: 680)
        WorkspaceUI.heading(view, "설정", "실행 방식, 도움말과 저장된 데이터를 관리하세요.")
        WorkspaceUI.card(view, 124, 126)
        WorkspaceUI.label(view, "앱 실행", 44, 144, 700, size: 16, bold: true)
        WorkspaceUI.place(view, loginCheck, 42, 186, 680, 28); loginCheck.target = self; loginCheck.action = #selector(toggleLogin)
        loginCheck.state = SMAppService.mainApp.status == .enabled ? .on : .off
        WorkspaceUI.card(view, 266, 156)
        WorkspaceUI.label(view, "도움말과 데이터", 44, 286, 700, size: 16, bold: true)
        WorkspaceUI.label(view, "설치·기기 조작은 가이드에서, 학습 기록과 설정 백업은 데이터 폴더에서 확인합니다.", 44, 322, 700, 38, size: 12, dim: true)
        WorkspaceUI.button(view, "설치 가이드", 40, 370, 220, target: self, action: #selector(openGuide))
        WorkspaceUI.button(view, "데이터 폴더 열기", 272, 370, 220, target: self, action: #selector(openData))
        WorkspaceUI.label(view, "SpaceSmart 1.3.0", 30, 458, 720, size: 16, bold: true)
        WorkspaceUI.label(view, "macOS 13 이상 · Apple Silicon / Intel\n조명 기능은 펌웨어 v2.3 이상에서 사용할 수 있습니다.\n센서와 LED 시험은 디바이스에서 직접 사용할 수 있습니다.", 30, 496, 720, 78, size: 13, dim: true)
        WorkspaceUI.label(view, "앱을 완전히 종료하려면 메뉴 막대 → SpaceSmart 종료 또는 ⌘Q를 누르세요.", 30, 604, 730, 44, size: 12, dim: true)
        return view
    }
    func refresh() {
        let connected = bridge?.connected == true
        sidebarStatus.stringValue = connected ? "● 기기 연결됨\nPC 상태 전송 중" : "○ 연결 전\n개요에서 기기 연결"
        sidebarStatus.textColor = connected ? WorkspaceUI.accent : .secondaryLabelColor
        overviewHint.stringValue = connected ? "다음 실행부터 저장한 기기에 자동으로 연결합니다. 조명과 화면 구성을 선택하세요." : "보드 전원을 켜고 약 10초 기다려 주세요. 다른 Mac에 연결 중이면 먼저 연결을 끊으세요."
        demoButton.isEnabled = connected
        customPanel.connected = connected
        lightPanel.connected = connected; lightPanel.refresh()
        guard let bridge else {
            metricsLabel.stringValue = "연결하면 사용량이 표시됩니다."
            connectButton.isEnabled = false
            return
        }
        connectionLabel.stringValue = bridge.status
        let selected = devicePicker.selectedItem?.representedObject as? String
        let ids = bridge.devices.keys.sorted(by: { $0.uuidString < $1.uuidString })
        let displayed = devicePicker.itemArray.compactMap { $0.representedObject as? String }
        if ids.map(\.uuidString) != displayed || devicePicker.numberOfItems == 0 {
            devicePicker.removeAllItems()
            for id in ids {
                devicePicker.addItem(withTitle: "SpaceSmart · " + id.uuidString.suffix(8))
                devicePicker.lastItem?.representedObject = id.uuidString
            }
            if ids.isEmpty { devicePicker.addItem(withTitle: "검색 중… 전원과 Bluetooth를 확인하세요") }
            let preferred = bridge.peripheral?.identifier.uuidString ?? selected
            if let item = devicePicker.itemArray.first(where: { $0.representedObject as? String == preferred }) { devicePicker.select(item) }
        }
        let connecting = bridge.peripheral != nil && !connected
        connectButton.title = connected ? "연결 끊기" : connecting ? "연결 취소" : "연결"
        connectButton.isEnabled = connected || connecting || !ids.isEmpty
        devicePicker.isEnabled = !connected && !connecting
        let fields = bridge.metrics.lastPC.split(separator: " ")
        if connected && fields.count == 5 { metricsLabel.stringValue = "CPU \(fields[1])%   메모리 \(fields[2])%   ↓ \(fields[3]) KiB/s   ↑ \(fields[4]) KiB/s" }
        else { metricsLabel.stringValue = "연결하면 사용량이 표시됩니다." }
        statusItem.button?.toolTip = bridge.status
    }
    @objc func connect() {
        guard let bridge else { return }
        if bridge.peripheral != nil { bridge.disconnect(); return }
        guard let value = devicePicker.selectedItem?.representedObject as? String, let id = UUID(uuidString: value) else { alert("기기가 아직 없습니다", "보드 전원을 켜고 약 10초 기다린 뒤 다시 찾기를 누르세요. 다른 Mac에 연결돼 있다면 먼저 연결을 종료하세요."); return }
        bridge.connect(id)
    }
    @objc func rescan() {
        bridge?.disconnect(forget: true); bridge?.enabled = true; bridge?.scan()
    }
    @objc func demo() {
        guard bridge?.connected == true else { alert("먼저 기기를 연결하세요", "연결된 뒤 디바이스의 AI 작업 화면을 선택하세요."); return }
        bridge?.metrics.demoStarted = Date().timeIntervalSince1970
        eventLabel.stringValue = "36초 시험: 작업 중 → 입력 대기 → 응답 완료. 각 12초 후 실제 상태로 복원합니다."
    }
    @objc func installHooks(_ sender: NSButton) {
        let app = sender.tag == 0 ? "claude" : "codex"
        do {
            guard let helper = Bundle.main.url(forResource: "SpaceSmartAgent", withExtension: nil) else { throw SetupError.message("앱 내부 파일이 없습니다. 배포본을 다시 복사하세요.") }
            try installer.installHelper(from: helper); try installer.apply(app: app, install: true)
            eventLabel.stringValue = app == "codex" ? "Codex 연동 설치 완료. ‘Codex 훅 검토 열기’에서 /hooks를 입력해 새 훅을 검토·신뢰하세요." : "Claude 연동 설치 완료. 새 Code 작업에서 확인하세요. 필요하면 작업 저장 후 앱을 다시 여세요."
        } catch { alert("연동 설정 확인", error.localizedDescription) }
    }
    @objc func reviewHooks() {
        var candidates: [URL] = []
        if let app = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.openai.codex") { candidates.append(app.appendingPathComponent("Contents/Resources/codex-cli/bin/codex")) }
        for app in ["/Applications/Codex.app", "/Applications/ChatGPT.app", installer.paths.home.appendingPathComponent("Applications/Codex.app").path] { candidates.append(URL(fileURLWithPath: app + "/Contents/Resources/codex-cli/bin/codex")) }
        candidates += ["/opt/homebrew/bin/codex", "/usr/local/bin/codex", installer.paths.home.appendingPathComponent(".local/bin/codex").path].map { URL(fileURLWithPath: $0) }
        guard let cli = candidates.first(where: { FileManager.default.isExecutableFile(atPath: $0.path) }) else { alert("Codex CLI를 찾지 못했습니다", "Codex를 설치·업데이트한 뒤 다시 시도하세요. 이미 CLI를 사용 중이면 터미널에서 codex를 열고 /hooks로 새 SpaceSmart 훅을 검토하세요."); return }
        do {
            try privateDirectory(installer.paths.support)
            let file = installer.paths.support.appendingPathComponent("Review-Codex-Hooks.command")
            let script = "#!/bin/zsh\nprint '이 창에서 /hooks를 입력하고 SpaceSmartAgent hook codex 명령을 검토·신뢰하세요.'\nexec " + shellQuote(cli.path) + " --no-alt-screen -C " + shellQuote(installer.paths.home.path) + "\n"
            try privateWrite(Data(script.utf8), to: file); try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: file.path)
            NSWorkspace.shared.open(file)
        } catch { alert("검토 창을 열 수 없습니다", error.localizedDescription) }
    }
    @objc func removeHooks() {
        let question = NSAlert(); question.messageText = "SpaceSmart의 AI 연동을 해제할까요?"; question.informativeText = "다른 훅과 앱 설정은 유지합니다. PC 상태 전송은 계속 사용할 수 있습니다."
        question.addButton(withTitle: "연동 해제"); question.addButton(withTitle: "취소")
        guard question.runModal() == .alertFirstButtonReturn else { return }
        do { try installer.apply(app: "claude", install: false); try installer.apply(app: "codex", install: false); eventLabel.stringValue = "SpaceSmart AI 훅을 제거했습니다. 기존 설정 백업은 데이터 폴더에 있습니다." }
        catch { alert("일부 설정을 제거하지 못했습니다", error.localizedDescription) }
    }
    @objc func toggleLogin() {
        do {
            if loginCheck.state == .on { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
            if SMAppService.mainApp.status == .requiresApproval { alert("로그인 항목 승인", "시스템 설정 → 일반 → 로그인 항목에서 SpaceSmart를 허용하세요.") }
        } catch { alert("자동 실행 설정", "앱을 응용 프로그램 폴더에 복사한 뒤 다시 시도하세요.\n" + error.localizedDescription) }
        loginCheck.state = SMAppService.mainApp.status == .enabled ? .on : .off
    }
    @objc func openGuide() { if let url = Bundle.main.url(forResource: "SpaceSmart-설치가이드", withExtension: "pdf") { NSWorkspace.shared.open(url) } }
    @objc func openData() { try? privateDirectory(installer.paths.support); NSWorkspace.shared.open(installer.paths.support) }
    @objc func openSettings() { select(.settings); showWindow() }
    @objc func showWindow() { window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true) }
    @objc func quit() { NSApp.terminate(nil) }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool { showWindow(); return true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard customPanel.dirty else { return .terminateNow }
        let question = NSAlert(); question.messageText = "저장하지 않은 커스텀 변경 사항이 있습니다"
        question.informativeText = "계속 편집하거나, 변경 사항을 버리고 앱을 종료할 수 있습니다."
        question.addButton(withTitle: "계속 편집"); question.addButton(withTitle: "저장하지 않고 종료")
        if question.runModal() == .alertSecondButtonReturn { return .terminateNow }
        select(.custom); showWindow(); return .terminateCancel
    }
    func applicationWillTerminate(_ notification: Notification) { bridge?.disconnect() }
    func alert(_ title: String, _ message: String) { let alert = NSAlert(); alert.messageText = title; alert.informativeText = message; alert.runModal() }
}
