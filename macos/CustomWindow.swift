import AppKit

final class CustomPanel: NSObject, NSTextFieldDelegate {
    let view = WorkspaceCanvas(height: 770)
    var selectors: [NSPopUpButton] = [], titles: [NSTextField] = [], left: [NSTextField] = [], right: [NSTextField] = []
    let feedback = NSTextField(wrappingLabelWithString: "")
    let saveButton = NSButton(title: "변경 사항 저장", target: nil, action: nil)
    var dirty = false
    var saved = false
    var connected = false { didSet { updateFeedback() } }
    override init() {
        super.init()
        WorkspaceUI.heading(view, "커스텀", "디바이스에 표시할 카드 3개를 구성하세요. 위에서부터 순서대로 표시됩니다.")
        for i in 0..<3 {
            let y = CGFloat(124 + i * 162)
            WorkspaceUI.card(view, y, 146)
            WorkspaceUI.label(view, "카드 \(i+1)", 44, y+19, 90, size: 15, bold: true)
            let picker = NSPopUpButton()
            picker.addItems(withTitles: CustomSource.allCases.map(\.label)); picker.tag = i; picker.target = self; picker.action = #selector(changed(_:))
            picker.setAccessibilityLabel("카드 \(i+1) 데이터")
            WorkspaceUI.place(view, picker, 136, y+16, 316, 30); selectors.append(picker)
            titles.append(field("제목 · 비우면 기본 제목", x: 466, y: y+18, w: 286, label: "카드 \(i+1) 제목"))
            WorkspaceUI.label(view, "텍스트", 44, y+65, 80, size: 12, dim: true)
            left.append(field("하단 왼쪽", x: 140, y: y+63, w: 296, label: "카드 \(i+1) 왼쪽 텍스트"))
            right.append(field("하단 오른쪽", x: 452, y: y+63, w: 300, label: "카드 \(i+1) 오른쪽 텍스트"))
            WorkspaceUI.label(view, "직접 입력은 ‘사용자 텍스트’에서만 가능합니다. 나머지 데이터는 자동 갱신됩니다.", 44, y+105, 700, 30, size: 11, dim: true)
        }
        WorkspaceUI.label(view, "각 텍스트는 한글 약 8자까지 표시됩니다. 초과한 글자는 저장 시 줄어듭니다.", 30, 620, 730, 34, size: 12, dim: true)
        WorkspaceUI.place(view, feedback, 30, 666, 492, 66); feedback.font = .systemFont(ofSize: 12); feedback.textColor = WorkspaceUI.accent
        WorkspaceUI.place(view, saveButton, 550, 670, 224, 34); saveButton.bezelStyle = .rounded
        saveButton.target = self; saveButton.action = #selector(save)
        loadSaved()
    }
    func field(_ placeholder: String, x: CGFloat, y: CGFloat, w: CGFloat, label: String) -> NSTextField {
        let field = NSTextField(); field.placeholderString = placeholder; field.delegate = self
        field.setAccessibilityLabel(label); WorkspaceUI.place(view, field, x, y, w, 26)
        return field
    }
    func loadSaved() {
        for (i, slot) in CustomData.load().enumerated() {
            selectors[i].selectItem(at: CustomSource.allCases.firstIndex(of: slot.source) ?? 0)
            titles[i].stringValue = slot.title; left[i].stringValue = slot.left; right[i].stringValue = slot.right
            updateFields(i)
        }
        dirty = false; updateFeedback()
    }
    func updateFields(_ i: Int) {
        let kind = CustomSource.allCases[selectors[i].indexOfSelectedItem]
        titles[i].isEnabled = kind != .off; left[i].isEnabled = kind == .text; right[i].isEnabled = kind == .text
    }
    @objc func changed(_ sender: NSPopUpButton) { updateFields(sender.tag); dirty = true; saved = false; updateFeedback() }
    func controlTextDidChange(_ obj: Notification) { dirty = true; saved = false; updateFeedback() }
    func updateFeedback() {
        saveButton.isEnabled = dirty
        feedback.stringValue = dirty ? "저장 전 변경 사항이 있습니다.\n메뉴를 이동해도 입력 내용은 유지됩니다." :
            (saved ? "저장했습니다. " : "") + (connected ? "디바이스의 ‘커스텀’ 탭에서 확인하세요." : "기기를 연결하면 저장한 구성이 전송됩니다.")
    }
    @objc func save() {
        view.window?.makeFirstResponder(nil)
        let slots = (0..<3).map { i in CustomSelection(source: CustomSource.allCases[selectors[i].indexOfSelectedItem], title: CustomData.field(titles[i].stringValue), left: CustomData.field(left[i].stringValue), right: CustomData.field(right[i].stringValue)) }
        do { try CustomData.save(slots); saved = true; loadSaved() }
        catch { let a = NSAlert(); a.messageText = "저장하지 못했습니다"; a.informativeText = error.localizedDescription; a.runModal() }
    }
}
