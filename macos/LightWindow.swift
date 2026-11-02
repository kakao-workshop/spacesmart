import AppKit

final class LightPanel: NSObject {
    let view = WorkspaceCanvas(height: 720)
    let tabs = NSSegmentedControl(labels: ["직접 조절", "자동 조절", "연결 설정"], trackingMode: .selectOne, target: nil, action: nil)
    var sections: [NSScrollView] = []
    var controller: LightController?
    var connected = false
    var onConnect: (() -> Void)?
    let stateLabel = NSTextField(wrappingLabelWithString: "")
    let messageLabel = NSTextField(wrappingLabelWithString: "")
    let learningLabel = NSTextField(wrappingLabelWithString: "")
    let diagnostics = NSTextField(wrappingLabelWithString: "")
    let levelsLabel = NSTextField(labelWithString: "")
    let idField = NSTextField(string: "")
    let profileField = NSComboBox()
    let enable = NSButton(checkboxWithTitle: "조명 제어 사용", target: nil, action: nil)
    let verified = NSButton(checkboxWithTitle: "설치 후 실제 조명 동작을 확인함", target: nil, action: nil)
    let automatic = NSButton(checkboxWithTitle: "학습한 선호로 밝기 자동 조절", target: nil, action: nil)
    let bri = NSSlider(value: 7, minValue: 0, maxValue: 15, target: nil, action: nil)
    let temp = NSSlider(value: 0, minValue: 0, maxValue: 15, target: nil, action: nil)
    var deviceControls: [NSControl] = []
    var manualControls: [NSControl] = []
    var connectButton: NSButton!
    var csvButton: NSButton!
    override init() {
        super.init()
        WorkspaceUI.heading(view, "조명", "MJGJD01YL · 자주 쓰는 조절부터, 내 환경에 맞춘 자동 조절까지.")
        WorkspaceUI.card(view, 124, 80)
        WorkspaceUI.place(view, stateLabel, 44, 144, 510, 48); stateLabel.font = .systemFont(ofSize: 13)
        connectButton = WorkspaceUI.button(view, "기기 연결", 578, 146, 176, target: self, action: #selector(goConnect))
        WorkspaceUI.place(view, messageLabel, 30, 214, 738, 44); messageLabel.font = .systemFont(ofSize: 12); messageLabel.textColor = WorkspaceUI.accent
        tabs.target = self; tabs.action = #selector(changeTab); tabs.selectedSegment = 0
        for i in 0..<3 { tabs.setWidth(236, forSegment: i) }
        WorkspaceUI.place(view, tabs, 28, 267, 744, 32)
        buildManual(); buildAutomatic(); buildSetup(); changeTab(); refresh()
    }
    func bind(_ controller: LightController?) {
        self.controller = controller
        controller?.onChange = { [weak self] in self?.refresh() }
        refresh()
    }
    func section(height: CGFloat) -> WorkspaceCanvas {
        let panel = WorkspaceCanvas(height: height)
        let scroll = WorkspaceUI.scroll(panel, frame: NSRect(x: 0, y: 316, width: 800, height: 404))
        view.addSubview(scroll); sections.append(scroll); return panel
    }
    @discardableResult func commandButton(_ panel: NSView, _ title: String, _ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ selector: Selector, _ tag: Int = 0, manual: Bool = false) -> NSButton {
        let button = WorkspaceUI.button(panel, title, x, y, width, target: self, action: selector, tag: tag)
        if manual { manualControls.append(button) } else { deviceControls.append(button) }
        return button
    }
    func buildManual() {
        let panel = section(height: 396)
        WorkspaceUI.card(panel, 4, 272)
        WorkspaceUI.label(panel, "전원과 밝기", 44, 24, 420, size: 16, bold: true)
        commandButton(panel, "전원 전환", 472, 16, 132, #selector(action(_:)), 0, manual: true)
        commandButton(panel, "기본 조명 복원", 612, 16, 146, #selector(action(_:)), 1, manual: true)
        WorkspaceUI.label(panel, "밝기", 44, 87, 90, size: 14)
        WorkspaceUI.label(panel, "색온도", 44, 149, 90, size: 14)
        for (slider, y, selector, name) in [(bri, CGFloat(86), #selector(brightnessChanged), "조명 밝기"), (temp, CGFloat(148), #selector(temperatureChanged), "조명 색온도")] {
            slider.numberOfTickMarks = 16; slider.allowsTickMarkValuesOnly = true; slider.isContinuous = false
            slider.target = self; slider.action = selector; slider.setAccessibilityLabel(name)
            WorkspaceUI.place(panel, slider, 124, y, 418, 28); manualControls.append(slider)
        }
        commandButton(panel, "−", 562, 80, 88, #selector(step(_:)), -1, manual: true)
        commandButton(panel, "+", 660, 80, 98, #selector(step(_:)), 1, manual: true)
        commandButton(panel, "따뜻하게", 562, 142, 88, #selector(step(_:)), -2, manual: true)
        commandButton(panel, "차갑게", 660, 142, 98, #selector(step(_:)), 2, manual: true)
        WorkspaceUI.place(panel, levelsLabel, 44, 202, 710, 26); levelsLabel.font = .systemFont(ofSize: 12)
        WorkspaceUI.label(panel, "밝기 0은 최소 밝기입니다. 끄려면 ‘전원 전환’을 누르세요.", 44, 237, 706, size: 11, dim: true)
        WorkspaceUI.label(panel, "표시값은 송신·리모컨 기록에 따른 추정입니다. 실제 전원이 다르면 ‘연결 설정’에서 맞추세요.", 30, 300, 730, 44, size: 12, dim: true)
        WorkspaceUI.button(panel, "조명 연결 설정", 24, 350, 204, target: self, action: #selector(goSetup))
    }
    func buildAutomatic() {
        let panel = section(height: 522)
        WorkspaceUI.card(panel, 4, 168)
        WorkspaceUI.label(panel, "내 환경의 학습 상태", 44, 24, 700, size: 16, bold: true)
        WorkspaceUI.place(panel, learningLabel, 44, 63, 706, 66); learningLabel.font = .systemFont(ofSize: 13)
        WorkspaceUI.place(panel, automatic, 42, 131, 690, 28); automatic.target = self; automatic.action = #selector(toggleAutomatic)
        WorkspaceUI.card(panel, 188, 144)
        WorkspaceUI.label(panel, "설치 위치", 44, 207, 700, size: 15, bold: true)
        WorkspaceUI.place(panel, profileField, 44, 244, 416, 28); profileField.stringValue = "기본 위치"; profileField.setAccessibilityLabel("학습할 설치 위치")
        commandButton(panel, "위치 선택 / 새로 만들기", 472, 240, 284, #selector(selectProfile))
        WorkspaceUI.label(panel, "센서나 조명을 옮기면 새 위치 이름을 입력하고 선택하세요. 위치별로 따로 학습합니다.", 44, 290, 700, 34, size: 11, dim: true)
        WorkspaceUI.label(panel, "자동 조절은 언제 켤 수 있나요?", 30, 355, 730, size: 15, bold: true)
        WorkspaceUI.label(panel, "실물 동작 확인 · 유효 조절 20개 · 사용 시간 3회 · 밝기 3단계 · 예측 일관성이 필요합니다.\n재실 중 안정된 조도에서 밝기만 한 단계씩 조절합니다. 수동 조작 후 10분간 기다립니다.", 30, 389, 730, 56, size: 12, dim: true)
        csvButton = WorkspaceUI.button(panel, "기록 CSV 저장", 24, 462, 204, target: self, action: #selector(exportCSV))
        WorkspaceUI.label(panel, "앱 종료·연결 끊김·기기 재부팅 시 자동 조절은 꺼집니다.", 244, 467, 520, 42, size: 11, dim: true)
    }
    func buildSetup() {
        let panel = section(height: 654)
        WorkspaceUI.card(panel, 4, 164)
        WorkspaceUI.label(panel, "1. 조명 제어 켜기 · 리모컨 등록", 44, 24, 700, size: 16, bold: true)
        WorkspaceUI.place(panel, enable, 42, 65, 420, 28); enable.target = self; enable.action = #selector(toggleEnabled)
        commandButton(panel, "리모컨 ID 등록", 534, 61, 222, #selector(learn))
        WorkspaceUI.label(panel, "등록을 누른 뒤 60초 안에 원래 리모컨을 두 번 조작하세요.\n원래 리모컨과 같은 ID로 제어하므로 함께 사용할 수 있습니다.", 44, 109, 704, 48, size: 12, dim: true)
        WorkspaceUI.card(panel, 184, 174)
        WorkspaceUI.label(panel, "2. 실제 전원 맞추기 · 동작 확인", 44, 204, 700, size: 16, bold: true)
        commandButton(panel, "현재 켜짐으로 맞춤", 40, 244, 258, #selector(action(_:)), 2)
        commandButton(panel, "현재 꺼짐으로 맞춤", 310, 244, 258, #selector(action(_:)), 3)
        WorkspaceUI.place(panel, verified, 42, 298, 710, 28); verified.target = self; verified.action = #selector(toggleVerified)
        WorkspaceUI.label(panel, "‘직접 조절’에서 전원·밝기·색온도가 실제로 동작하는지 확인한 뒤 선택하세요.", 44, 329, 704, size: 11, dim: true)
        WorkspaceUI.card(panel, 374, 158)
        WorkspaceUI.label(panel, "고급 · ID 직접 입력과 새 페어링", 44, 394, 700, size: 14, bold: true)
        idField.placeholderString = "6자리 ID"; idField.setAccessibilityLabel("리모컨 ID 직접 입력")
        WorkspaceUI.place(panel, idField, 44, 435, 160, 26)
        commandButton(panel, "ID 저장", 214, 431, 140, #selector(saveID))
        commandButton(panel, "새로 페어링…", 366, 431, 208, #selector(pair))
        WorkspaceUI.label(panel, "기존 리모컨 등록이 어려울 때 사용하세요. 다른 ID로 페어링하면 원래 리모컨 연결이 바뀝니다.", 44, 482, 700, 36, size: 11, dim: true)
        WorkspaceUI.place(panel, diagnostics, 30, 554, 730, 80); diagnostics.font = .monospacedDigitSystemFont(ofSize: 11, weight: .regular); diagnostics.textColor = .secondaryLabelColor
    }
    @objc func goConnect() { onConnect?() }
    @objc func goSetup() { tabs.selectedSegment = 2; changeTab() }
    @objc func changeTab() {
        view.window?.makeFirstResponder(nil)
        for (i, section) in sections.enumerated() { section.isHidden = i != tabs.selectedSegment }
        // Keep the section switcher visible while each section scrolls independently.
    }
    func refresh() {
        guard let c = controller, let s = c.status else {
            stateLabel.stringValue = connected ? "기기 연결됨 · 조명 데이터 대기 중\n데이터가 계속 오지 않으면 펌웨어 v2.3 이상인지 확인하세요." : "조명 데이터 대기 중\n기기를 연결하세요. 조명 기능은 펌웨어 v2.3 이상이 필요합니다."
            connectButton.title = connected ? "연결 확인" : "기기 연결"
            messageLabel.stringValue = controller?.message.isEmpty == false ? controller!.message : "처음 사용한다면 ‘연결 설정’에서 리모컨을 등록하세요."
            deviceControls.forEach { $0.isEnabled = false }; manualControls.forEach { $0.isEnabled = false }
            [enable, verified, automatic, csvButton!].forEach { $0.isEnabled = false; $0.state = .off }
            profileField.isEnabled = false; idField.isEnabled = false
            levelsLabel.stringValue = "밝기 — · 색온도 —"
            learningLabel.stringValue = "기기를 연결하면 위치별 학습 상태를 확인할 수 있습니다."
            diagnostics.stringValue = "연결 후 리모컨 ID와 무선 송수신 상태가 표시됩니다."
            connectButton.isHidden = false
            return
        }
        let fresh = Date().timeIntervalSince(s.received) < 10
        deviceControls.forEach { $0.isEnabled = fresh }
        manualControls.forEach { $0.isEnabled = fresh && s.enabled && s.radio && s.remoteID != "000000" }
        enable.isEnabled = fresh; verified.isEnabled = fresh && s.remoteID != "000000"
        profileField.isEnabled = fresh; idField.isEnabled = fresh; csvButton.isEnabled = true
        enable.state = s.enabled ? .on : .off; verified.state = s.verified ? .on : .off; automatic.state = c.automatic ? .on : .off
        let r = c.assessment
        automatic.isEnabled = c.automatic || (fresh && s.enabled && s.radio && s.verified && r.ready && s.power == 1 && s.brightness >= 0 && s.temperature >= 0)
        let power = s.power < 0 ? "전원 미확인" : s.power == 1 ? "켜짐 추정" : "꺼짐 추정"
        stateLabel.stringValue = "\(fresh ? power : "조명 데이터가 끊겼습니다") · 조도 \(s.lux < 0 ? "—" : String(format: "%.1f", s.lux)) lx\n\(s.enabled ? "조명 제어 켜짐" : "조명 제어 꺼짐") · \(c.automatic ? "자동 조절 켜짐" : "수동 조절")"
        connectButton.title = "연결 확인"; connectButton.isHidden = fresh
        diagnostics.stringValue = "\(s.radio ? "nRF24 준비" : "nRF24 미연결") · 리모컨 ID \(s.remoteID)\n수신 \(s.rx) / 송신 \(s.tx) · 미전달 기록 초과 \(s.dropped)\n\(s.learning ? "60초 안에 리모컨을 두 번 조작하세요." : "") \(s.busy ? "무선 전송 중" : "")"
        if NSApp.currentEvent?.type != .leftMouseDragged { if s.brightness >= 0 { bri.integerValue = s.brightness }; if s.temperature >= 0 { temp.integerValue = s.temperature } }
        levelsLabel.stringValue = "밝기 \(s.brightness < 0 ? "미확인" : "\(s.brightness)/15") · 색온도 \(s.temperature < 0 ? "미확인" : "약 \(2700+s.temperature*3800/15) K")"
        let p = c.profile
        learningLabel.stringValue = "\(p?.name ?? "기본 위치") · 전체 기록 \(p?.observations.count ?? 0)개\n\(r.reason)\n\(s.holdSeconds > 0 ? "수동 조작 우선: \(s.holdSeconds)초 후 자동 조절 가능" : "재실·안정된 조도·학습한 주변 환경에서 동작")"
        if profileField.currentEditor() == nil {
            let names = c.store.archive.profiles.filter { $0.device == c.device && $0.remoteID == s.remoteID }.map { $0.name }
            profileField.removeAllItems(); profileField.addItems(withObjectValues: names)
            profileField.stringValue = p?.name ?? "기본 위치"
        }
        messageLabel.stringValue = c.store.loadError ?? (c.message.isEmpty ? "‘직접 조절’로 밝기를 맞추면 조정 전후의 조도가 학습 기록에 저장됩니다." : c.message)
    }
    @objc func toggleEnabled(){_ = controller?.send("enable \(enable.state == .on ? 1:0)")}
    @objc func toggleVerified(){_ = controller?.send("verify \(verified.state == .on ? 1:0)")}
    @objc func learn(){_ = controller?.send("learn")}
    @objc func saveID(){let value=idField.stringValue.trimmingCharacters(in:.whitespacesAndNewlines);guard value.count==6,let id=UInt32(value,radix:16),id>0 else{notice("ID는 0이 아닌 6자리 16진수입니다.");return};_ = controller?.send("id \(value)")}
    @objc func pair(){
        guard let s=controller?.status,s.remoteID != "000000",s.enabled else{notice("먼저 리모컨 ID를 등록하거나 ID를 저장하고 조명 제어를 켜 주세요.");return}
        let a=NSAlert();a.messageText="조명과 새로 연결";a.informativeText="기존 리모컨과 다른 ID를 사용하면 기존 리모컨 연결이 바뀝니다. 조명의 전원을 다시 연결한 뒤 20초 안에 실행하세요.";a.addButton(withTitle:"페어링 실행");a.addButton(withTitle:"취소");if a.runModal() == .alertFirstButtonReturn{_ = controller?.send("pair")}
    }
    @objc func action(_ b:NSButton){let ops=["toggle","reset","mark 1","mark 0"];_ = controller?.send(ops[b.tag])}
    @objc func step(_ b:NSButton){_ = controller?.send("\(abs(b.tag)==1 ? "bri":"temp") \(b.tag>0 ? 1 : -1)")}
    @objc func brightnessChanged(){_ = controller?.send("brightness \(bri.integerValue)")}
    @objc func temperatureChanged(){_ = controller?.send("temperature \(temp.integerValue)")}
    @objc func toggleAutomatic(){controller?.setAutomatic(automatic.state == .on);refresh()}
    @objc func selectProfile(){do{try controller?.selectProfile(profileField.stringValue)}catch{notice(error.localizedDescription)}}
    @objc func exportCSV(){guard let c=controller,let s=c.status else{return};let panel=NSSavePanel();panel.nameFieldStringValue="SpaceSmart-조명기록.csv";if panel.runModal() == .OK,let url=panel.url{do{try c.store.csv(device:c.device,remote:s.remoteID).write(to:url,atomically:true,encoding:.utf8)}catch{notice(error.localizedDescription)}}}
    func notice(_ message:String){let a=NSAlert();a.messageText=message;a.runModal()}
}
