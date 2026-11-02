import Foundation

enum CustomSource: String, Codable, CaseIterable {
    case off, cpu, memory, disk, network, diskText, text
    var label: String {
        switch self {
        case .off: return "사용 안 함"
        case .cpu: return "CPU 사용률 · 막대"
        case .memory: return "메모리 사용률 · 막대"
        case .disk: return "저장 공간 사용률 · 막대"
        case .network: return "네트워크 RX/TX · 텍스트"
        case .diskText: return "저장 공간 여유/전체 · 텍스트"
        case .text: return "사용자 텍스트"
        }
    }
    var defaultTitle: String {
        switch self {
        case .off: return ""
        case .cpu: return "CPU 사용률"
        case .memory: return "메모리 사용률"
        case .disk: return "저장 공간"
        case .network: return "네트워크"
        case .diskText: return "여유 / 전체"
        case .text: return "메모"
        }
    }
}
struct CustomSelection: Codable {
    var source: CustomSource = .off
    var title = "", left = "", right = ""
}
struct CustomReadings {
    var cpu: Double?, memory: Double?, rx: Double?, tx: Double?
    var diskFree: Double?, diskTotal: Double?
}
enum CustomData {
    static let key = "customSlotsV1"
    static func load(_ defaults: UserDefaults = .standard) -> [CustomSelection] {
        guard let data = defaults.data(forKey: key), let saved = try? JSONDecoder().decode([CustomSelection].self, from: data), saved.count == 3 else { return Array(repeating: CustomSelection(), count: 3) }
        return saved
    }
    static func save(_ selections: [CustomSelection], defaults: UserDefaults = .standard) throws {
        guard selections.count == 3 else { throw SetupError.message("항목은 3개여야 합니다.") }
        defaults.set(try JSONEncoder().encode(selections), forKey: key)
    }
    static func field(_ input: String) -> String {
        var result = input.unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) }.map(String.init).joined().replacingOccurrences(of: "|", with: " ")
        while result.utf8.count > 24 { result.removeLast() }
        return result
    }
    static func number(_ value: Double) -> String { String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), value) }
    static func rate(_ value: Double, prefix: String) -> String {
        return prefix + " " + number(value >= 1024 ? value/1024 : value) + (value >= 1024 ? "MiB" : "KiB")
    }
    static func lines(_ selections: [CustomSelection], readings: CustomReadings) -> [String] {
        (0..<3).map { index in
            let slot = index < selections.count ? selections[index] : CustomSelection()
            let title = field(slot.title.isEmpty ? slot.source.defaultTitle : slot.title)
            let clear = "custom \(index) clear\n"
            func gauge(_ value: Double?) -> String {
                guard let value, value.isFinite, value >= 0, value <= 100 else { return clear }
                return "custom \(index) gauge \(number(value)) |\(title)\n"
            }
            func text(_ left: String, _ right: String) -> String { "custom \(index) text |\(title)|\(field(left))|\(field(right))\n" }
            switch slot.source {
            case .off: return clear
            case .cpu: return gauge(readings.cpu)
            case .memory: return gauge(readings.memory)
            case .disk:
                guard let free = readings.diskFree, let total = readings.diskTotal, total > 0 else { return clear }
                return gauge(max(0, min(100, (total-free)/total*100)))
            case .network:
                guard let rx = readings.rx, let tx = readings.tx, rx.isFinite, tx.isFinite, rx >= 0, tx >= 0 else { return clear }
                return text(rate(rx, prefix: "RX"), rate(tx, prefix: "TX"))
            case .diskText:
                guard let free = readings.diskFree, let total = readings.diskTotal, free.isFinite, total.isFinite, total > 0 else { return clear }
                return text(number(free/1e9)+"GB", number(total/1e9)+"GB")
            case .text: return text(slot.left, slot.right)
            }
        }
    }
}
