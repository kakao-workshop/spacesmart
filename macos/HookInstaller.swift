import Foundation

enum SetupError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case let .message(text) = self { return text }; return nil }
}
func shellQuote(_ value: String) -> String { "'" + value.replacingOccurrences(of: "'", with: "'\"'\"'") + "'" }

final class HookInstaller {
    let paths: SpacePaths
    init(paths: SpacePaths = SpacePaths()) { self.paths = paths }
    func command(_ app: String) -> String { "exec " + shellQuote(paths.helper.path) + " hook " + app }
    func transformed(_ source: [String: Any], app: String, install: Bool) throws -> [String: Any] {
        var result = source
        guard source["hooks"] == nil || source["hooks"] is [String: Any] else { throw SetupError.message("기존 hooks 형식이 올바르지 않아 변경하지 않았습니다.") }
        var hooks = source["hooks"] as? [String: Any] ?? [:]
        for (event, value) in hooks {
            guard let groups = value as? [[String: Any]] else { throw SetupError.message("기존 \(event) 훅 형식을 확인하세요. 파일은 변경하지 않았습니다.") }
            var retained: [[String: Any]] = []
            for var group in groups {
                guard let handlers = group["hooks"] as? [[String: Any]] else { throw SetupError.message("기존 훅 목록을 읽을 수 없습니다.") }
                let remaining = handlers.filter { ($0["command"] as? String) != command(app) }
                if remaining.count == handlers.count { retained.append(group) }
                else if !remaining.isEmpty { group["hooks"] = remaining; retained.append(group) }
            }
            if retained.isEmpty { hooks.removeValue(forKey: event) } else { hooks[event] = retained }
        }
        if install {
            var events = ["SessionStart", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PermissionRequest", "PreCompact", "PostCompact", "Stop", "SessionEnd"]
            events += app == "codex" ? ["Interrupt"] : ["PostToolUseFailure", "StopFailure", "Notification"]
            for event in events {
                var groups = hooks[event] as? [[String: Any]] ?? []
                groups.append(["hooks": [["type":"command", "command":command(app), "timeout":3]]])
                hooks[event] = groups
            }
        }
        if hooks.isEmpty && source["hooks"] == nil { result.removeValue(forKey: "hooks") } else { result["hooks"] = hooks }
        return result
    }
    func apply(app: String, install: Bool) throws {
        let file = paths.config(app)
        let before = try FileManager.default.fileExists(atPath: file.path) ? Data(contentsOf: file) : nil
        if before == nil && !install { return }
        let source: [String: Any]
        if let before {
            guard let object = try JSONSerialization.jsonObject(with: before) as? [String: Any] else { throw SetupError.message("설정 파일의 JSON 객체가 올바르지 않습니다.") }
            source = object
        } else { source = [:] }
        let after = try transformed(source, app: app, install: install)
        let encoded = try JSONSerialization.data(withJSONObject: after, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
        if NSDictionary(dictionary: source).isEqual(to: after) { return }
        try privateDirectory(paths.support)
        if let before {
            let backups = paths.support.appendingPathComponent("backups")
            try privateDirectory(backups)
            try privateWrite(before, to: backups.appendingPathComponent("\(app)-\(Int(Date().timeIntervalSince1970))-\(UUID().uuidString).json"))
        }
        // Detect a concurrent editor rather than replacing changes made after our read.
        let current = try FileManager.default.fileExists(atPath: file.path) ? Data(contentsOf: file) : nil
        guard current == before else { throw SetupError.message("설정 파일이 다른 프로그램에서 변경됐습니다. 다시 시도하세요.") }
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try privateWrite(encoded, to: file)
    }
    func installHelper(from source: URL) throws {
        try privateDirectory(paths.support)
        let temp = paths.support.appendingPathComponent("SpaceSmartAgent-" + UUID().uuidString)
        try FileManager.default.copyItem(at: source, to: temp)
        try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: temp.path)
        if rename(temp.path, paths.helper.path) != 0 {
            try? FileManager.default.removeItem(at: temp)
            throw SetupError.message("상태 수집 프로그램을 설치할 수 없습니다.")
        }
    }
}
