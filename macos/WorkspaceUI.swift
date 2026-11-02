import AppKit

// All panels share top-to-bottom coordinates and live inside the main window.
final class WorkspaceCanvas: NSView {
    override var isFlipped: Bool { true }
    init(height: CGFloat) { super.init(frame: NSRect(x: 0, y: 0, width: 800, height: height)) }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

enum WorkspaceUI {
    static let accent = NSColor(calibratedRed: 0.42, green: 0.75, blue: 0.94, alpha: 1)
    @discardableResult static func label(_ parent: NSView, _ text: String, _ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ height: CGFloat = 24, size: CGFloat = 13, bold: Bool = false, dim: Bool = false) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: size, weight: bold ? .semibold : .regular)
        label.textColor = dim ? .secondaryLabelColor : .labelColor
        place(parent, label, x, y, width, height)
        return label
    }
    static func place(_ parent: NSView, _ view: NSView, _ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ height: CGFloat) {
        view.frame = NSRect(x: x, y: y, width: width, height: height)
        parent.addSubview(view)
    }
    @discardableResult static func button(_ parent: NSView, _ title: String, _ x: CGFloat, _ y: CGFloat, _ width: CGFloat, target: AnyObject, action: Selector, tag: Int = 0) -> NSButton {
        let button = NSButton(title: title, target: target, action: action)
        button.bezelStyle = .rounded; button.tag = tag
        place(parent, button, x, y, width, 32)
        return button
    }
    static func card(_ parent: NSView, _ y: CGFloat, _ height: CGFloat) {
        let card = NSView(frame: NSRect(x: 24, y: y, width: 752, height: height))
        card.wantsLayer = true
        card.layer?.backgroundColor = NSColor(calibratedWhite: 0.105, alpha: 1).cgColor
        card.layer?.cornerRadius = 12
        parent.addSubview(card)
    }
    static func heading(_ parent: NSView, _ title: String, _ subtitle: String) {
        label(parent, title, 28, 26, 744, 38, size: 27, bold: true)
        label(parent, subtitle, 30, 72, 738, 40, size: 13, dim: true)
    }
    static func scroll(_ view: NSView, frame: NSRect) -> NSScrollView {
        let scroll = NSScrollView(frame: frame)
        scroll.hasVerticalScroller = true; scroll.autohidesScrollers = true
        scroll.drawsBackground = false; scroll.documentView = view
        scroll.autoresizingMask = [.width, .height]
        return scroll
    }
}
