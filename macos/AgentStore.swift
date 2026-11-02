import Foundation
import CryptoKit
import Darwin

struct SpacePaths {
    let home: URL
    init(home: URL = FileManager.default.homeDirectoryForCurrentUser) { self.home = home }
    var support: URL { home.appendingPathComponent("Library/Application Support/SpaceSmart") }
    var states: URL { support.appendingPathComponent("state") }
    var helper: URL { support.appendingPathComponent("SpaceSmartAgent") }
    func config(_ app: String) -> URL { home.appendingPathComponent(app == "codex" ? ".codex/hooks.json" : ".claude/settings.json") }
}

struct Owner: Codable { let pid: Int32; let start: String }
struct AgentRecord: Codable {
    var app: String, state: String, event: String, title: String
    var updated: TimeInterval, started: TimeInterval
    var elapsed: Int
    var owner: Owner?
}

func processIdentity(_ pid: Int32) -> (parent: Int32, start: String, command: String)? {
    guard pid > 1 else { return nil }
    let process = Process(), pipe = Pipe()
    process.executableURL = URL(fileURLWithPath: "/bin/ps")
    process.arguments = ["-p", String(pid), "-o", "ppid=,lstart=,comm="]
    process.standardOutput = pipe; process.standardError = FileHandle.nullDevice
    do {
        try process.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile(); process.waitUntilExit()
        let fields = (String(data: data, encoding: .utf8) ?? "").split(maxSplits: 6, whereSeparator: { $0.isWhitespace })
        guard process.terminationStatus == 0, fields.count == 7, let parent = Int32(fields[0]) else { return nil }
        return (parent, fields[1...5].joined(separator: " "), String(fields[6]))
    } catch { return nil }
}

func findOwner(_ app: String) -> Owner? {
    var pid = getppid()
    for _ in 0..<8 {
        guard let info = processIdentity(pid) else { break }
        if URL(fileURLWithPath: info.command).lastPathComponent.lowercased().contains(app) {
            return Owner(pid: pid, start: info.start)
        }
        pid = info.parent
    }
    return nil
}

func privateDirectory(_ url: URL) throws {
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
    try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: url.path)
}
func privateWrite(_ data: Data, to url: URL) throws {
    let temp = url.deletingLastPathComponent().appendingPathComponent(".spacesmart-" + UUID().uuidString)
    let fd = open(temp.path, O_CREAT | O_EXCL | O_WRONLY, 0o600)
    guard fd >= 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
    defer { close(fd); unlink(temp.path) }
    try data.withUnsafeBytes { bytes in
        guard let base = bytes.baseAddress else { return }
        var offset = 0
        while offset < bytes.count {
            let count = write(fd, base.advanced(by: offset), bytes.count - offset)
            if count < 0 && errno == EINTR { continue }
            guard count > 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
            offset += count
        }
    }
    guard fsync(fd) == 0, rename(temp.path, url.path) == 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
}

final class AgentStore {
    let paths: SpacePaths
    init(paths: SpacePaths = SpacePaths()) { self.paths = paths }
    static let states = ["SessionStart":"idle", "UserPromptSubmit":"busy", "PreToolUse":"busy", "PostToolUse":"busy", "PostToolUseFailure":"busy", "PreCompact":"busy", "PostCompact":"busy", "PermissionRequest":"waiting", "Stop":"complete", "StopFailure":"error", "Interrupt":"interrupted", "SessionEnd":"offline"]

    func record(app: String, payload: [String: Any], now: TimeInterval = Date().timeIntervalSince1970, owner: Owner? = nil) throws {
        guard ["claude", "codex"].contains(app), let session = payload["session_id"] as? String,
              let event = payload["hook_event_name"] as? String,
              payload["agent_id"] == nil || payload["agent_id"] is NSNull,
              payload["subagent_id"] == nil || payload["subagent_id"] is NSNull else { return }
        var state = Self.states[event]
        if event == "SessionStart", payload["source"] as? String == "compact" { state = "busy" }
        if event == "Notification" {
            let kind = payload["notification_type"] as? String ?? ""
            state = ["permission_prompt", "elicitation_dialog", "elicitation_url_dialog"].contains(kind) ? "waiting" : nil
        }
        guard let state else { return }
        try privateDirectory(paths.states)
        let lock = open(paths.states.appendingPathComponent(app + ".lock").path, O_CREAT | O_RDWR, 0o600)
        guard lock >= 0 else { return }
        defer { flock(lock, LOCK_UN); close(lock) }
        var acquired = false
        for _ in 0..<10 { if flock(lock, LOCK_EX | LOCK_NB) == 0 { acquired = true; break }; usleep(20_000) }
        guard acquired else { return }
        let key = SHA256.hash(data: Data(session.utf8)).map { String(format: "%02x", $0) }.joined().prefix(24)
        let file = paths.states.appendingPathComponent(app + "-" + key + ".json")
        let previous = (try? Data(contentsOf: file)).flatMap { try? JSONDecoder().decode(AgentRecord.self, from: $0) }
        let start = event == "UserPromptSubmit" ? now : (previous?.started ?? now)
        let title = URL(fileURLWithPath: payload["cwd"] as? String ?? "").lastPathComponent
        let record = AgentRecord(app: app, state: state, event: event, title: title.isEmpty ? app : title,
                                 updated: now, started: start, elapsed: min(604800, max(0, Int(now - start))), owner: owner ?? previous?.owner)
        try privateWrite(JSONEncoder().encode(record), to: file)
    }

    func observed(_ record: AgentRecord, now: TimeInterval, checkOwner: Bool = true) -> String {
        let age = now - record.updated
        if age < -3 || age > 86400 { return "unknown" }
        if checkOwner, let owner = record.owner {
            guard processIdentity(owner.pid)?.start == owner.start else { return "offline" }
        }
        if record.owner == nil && age > 15 { return "unknown" }
        if ["busy", "waiting"].contains(record.state) && age > 300 { return "unknown" }
        if ["complete", "error", "interrupted"].contains(record.state) && age > 60 { return "idle" }
        return record.state
    }

    func lines(now: TimeInterval = Date().timeIntervalSince1970, checkOwner: Bool = true) -> [String] {
        let files = (try? FileManager.default.contentsOfDirectory(at: paths.states, includingPropertiesForKeys: [.contentModificationDateKey, .fileSizeKey])) ?? []
        let priority = ["waiting":7, "error":6, "busy":5, "complete":4, "interrupted":3, "idle":2, "offline":1, "unknown":0]
        return ["claude", "codex"].map { app in
            let recent = files.filter { $0.lastPathComponent.hasPrefix(app + "-") && $0.pathExtension == "json" }.sorted {
                ((try? $0.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast) >
                ((try? $1.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast)
            }.prefix(32)
            let candidates: [(AgentRecord, String)] = recent.compactMap { file in
                guard let size = try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize, size <= 8192,
                      let data = try? Data(contentsOf: file), let record = try? JSONDecoder().decode(AgentRecord.self, from: data) else { return nil }
                let state = observed(record, now: now, checkOwner: checkOwner)
                guard priority[state] != nil else { return nil }; return (record, state)
            }
            guard let selected = candidates.max(by: {
                let a = priority[$0.1]!, b = priority[$1.1]!
                return a == b ? $0.0.updated < $1.0.updated : a < b
            }) else { return "agent \(app) unknown 0 |\n" }
            let record = selected.0, state = selected.1
            let clean = record.title.unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) }.map(String.init).joined().replacingOccurrences(of: "|", with: " ")
            var title = clean
            let elapsed = min(604800, max(0, record.elapsed + (state == "busy" ? Int(max(0, now - record.updated)) : 0)))
            let prefix = "agent \(app) \(state) \(elapsed) |"
            while title.utf8.count > min(63, 95 - prefix.utf8.count) { title.removeLast() }
            return prefix + title + "\n"
        }
    }
}
