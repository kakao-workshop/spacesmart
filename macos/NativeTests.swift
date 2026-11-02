import Foundation
import Darwin

func expect(_ condition: @autoclosure () -> Bool, _ name: String) {
    if !condition() { fputs("FAIL: \(name)\n", stderr); exit(1) }
}
@main struct NativeTests {
    static func main() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("SpaceSmart Test ' 한글-" + UUID().uuidString)
        try privateDirectory(root); defer { try? FileManager.default.removeItem(at: root) }
        let paths = SpacePaths(home: root), installer = HookInstaller(paths: paths), store = AgentStore(paths: paths)
        let custom: [String: Any] = ["env":["PRIVATE_KEY":"keep-me"], "permissions":["allow":["Read"]], "hooks":["Stop":[["matcher":"existing", "hooks":[["type":"command", "command":"echo existing", "timeout":120]]]]]]
        for app in ["codex", "claude"] {
            try FileManager.default.createDirectory(at: paths.config(app).deletingLastPathComponent(), withIntermediateDirectories: true)
            let original = try JSONSerialization.data(withJSONObject: custom)
            try original.write(to: paths.config(app))
            try installer.apply(app: app, install: true)
            let once = try Data(contentsOf: paths.config(app))
            try installer.apply(app: app, install: true)
            expect(once == (try? Data(contentsOf: paths.config(app))), "idempotent install \(app)")
            let installed = try JSONSerialization.jsonObject(with: once) as! [String: Any]
            expect((installed["env"] as? [String:String])?["PRIVATE_KEY"] == "keep-me", "preserve unrelated settings")
            let hooks = installed["hooks"] as! [String:[[String:Any]]]
            expect(hooks["Stop"]?.count == 2, "preserve existing hooks")
            try installer.apply(app: app, install: false)
            let removed = try JSONSerialization.jsonObject(with: Data(contentsOf: paths.config(app))) as! [String: Any]
            expect(NSDictionary(dictionary: custom).isEqual(to: removed), "remove only own handlers")
        }
        let corrupt = Data("{broken".utf8); try corrupt.write(to: paths.config("codex"))
        do { try installer.apply(app: "codex", install: true); expect(false, "reject malformed JSON") } catch {}
        expect((try? Data(contentsOf: paths.config("codex"))) == corrupt, "malformed file unchanged")
        let pid = getpid(); let owner = Owner(pid: pid, start: processIdentity(pid)!.start)
        var payload: [String: Any] = ["session_id":"one", "hook_event_name":"UserPromptSubmit", "cwd":"/projects/상태 시험", "prompt":"never store this prompt", "tool_input":["secret":"never store this secret"]]
        try store.record(app: "codex", payload: payload, now: 100, owner: owner)
        expect(store.lines(now: 104).joined().contains("codex busy 4 |상태 시험"), "working lifecycle")
        let stateFiles = try FileManager.default.contentsOfDirectory(at: paths.states, includingPropertiesForKeys: nil).filter { $0.pathExtension == "json" }
        let saved = try String(contentsOf: stateFiles[0], encoding: .utf8)
        expect(!saved.contains("secret") && !saved.contains("prompt") && !saved.contains("projects/"), "privacy allowlist")
        for (event, state) in [("PermissionRequest","waiting"), ("Stop","complete")] {
            payload["hook_event_name"] = event; try store.record(app: "codex", payload: payload, now: 110, owner: owner)
            expect(store.lines(now: 111).joined().contains("codex \(state)"), "event \(event)")
        }
        expect(store.lines(now: 171).joined().contains("codex idle"), "complete returns idle")
        payload["session_id"] = "child"; payload["agent_id"] = "subagent"
        try store.record(app: "codex", payload: payload, now: 120, owner: owner)
        let afterChild = try FileManager.default.contentsOfDirectory(at: paths.states, includingPropertiesForKeys: nil).filter { $0.pathExtension == "json" }
        expect(afterChild.count == 1, "subagent ignored")
        let stale = AgentRecord(app:"codex", state:"busy", event:"test", title:"", updated:100, started:100, elapsed:0, owner:owner)
        expect(store.observed(stale, now:401) == "unknown", "stale work expires")
        let noOwner = AgentRecord(app:"codex", state:"busy", event:"test", title:"", updated:100, started:100, elapsed:0, owner:nil)
        expect(store.observed(noOwner, now:116) == "unknown", "ownerless event expires")
        var long = payload; long.removeValue(forKey:"agent_id"); long["session_id"] = "long"; long["hook_event_name"] = "PermissionRequest"; long["cwd"] = "/" + String(repeating:"한글",count:100)
        try store.record(app:"codex",payload:long,now:130,owner:owner)
        expect(store.lines(now:131).allSatisfy { $0.utf8.count <= 96 }, "BLE packet length")
        let attrs = try FileManager.default.attributesOfItem(atPath: stateFiles[0].path)
        expect((attrs[.posixPermissions] as? NSNumber)?.intValue == 0o600, "private state permissions")
        let script = "printf '%s' " + shellQuote(paths.helper.path)
        let p=Process(),pipe=Pipe();p.executableURL=URL(fileURLWithPath:"/bin/zsh");p.arguments=["-c",script];p.standardOutput=pipe
        try p.run();let quoted=pipe.fileHandleForReading.readDataToEndOfFile();p.waitUntilExit()
        expect(String(data:quoted,encoding:.utf8)==paths.helper.path,"shell quoting spaces/apostrophe/Korean")
        let metrics = Metrics(store: store)
        for (age, expected) in [(1.0,"busy"),(13.0,"waiting"),(25.0,"complete"),(37.0,"unknown")] {
            metrics.demoStarted = Date().timeIntervalSince1970 - age
            let lines = metrics.lines()
            expect(lines.contains(where: { $0.hasPrefix("agent codex \(expected) ") }), "demo transition and restoration \(expected)")
            expect(lines.allSatisfy { $0.utf8.count <= 96 }, "all outgoing lines bounded")
        }
        expect(metrics.demoStarted == nil, "demo cleared after 36 seconds")
        let readings = CustomReadings(cpu:37.5,memory:65,rx:1024,tx:2,diskFree:25e9,diskTotal:100e9)
        let selections = [CustomSelection(source:.cpu),CustomSelection(source:.network),CustomSelection(source:.text,title:"긴 한글 제목입니다\n|",left:"왼쪽\n|값",right:"오른쪽")]
        let customLines = CustomData.lines(selections,readings:readings)
        expect(customLines[0] == "custom 0 gauge 37.5 |CPU 사용률\n", "custom gauge")
        expect(customLines[1].contains("|네트워크|RX 1.0MiB|TX 2.0KiB"), "custom text source")
        expect(customLines.allSatisfy { $0.utf8.count <= 96 && $0.filter({$0 == "\n"}).count == 1 }, "custom bounded sanitized records")
        expect(customLines[2].split(separator:"|",omittingEmptySubsequences:false).count == 4,"no delimiter injection")
        expect(CustomData.lines([CustomSelection(source:.disk)],readings:readings)[0].contains("75.0"),"disk percentage")
        expect(CustomData.lines([CustomSelection(source:.cpu)],readings:CustomReadings())[0] == "custom 0 clear\n","missing source clears rather than fabricates zero")
        let defaults = UserDefaults(suiteName:"SpaceSmartTest-"+UUID().uuidString)!
        try CustomData.save(selections, defaults:defaults)
        expect(CustomData.load(defaults)[2].source == .text,"custom configuration persistence")
        defaults.removeObject(forKey:CustomData.key)
        print("PASS: install/remove preservation, idempotence, malformed JSON, privacy, lifecycle, stale data, subagents, UTF-8 bounds, permissions, portable paths, demo transitions/restoration")
    }
}
