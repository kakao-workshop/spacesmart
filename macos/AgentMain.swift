import Foundation

@main struct AgentMain {
    static func main() {
        let args = CommandLine.arguments
        if args.count == 3, args[1] == "hook", ["claude", "codex"].contains(args[2]) {
            // Observation only: no decisions, no prompt persistence, bounded stdin.
            var data = Data()
            while data.count <= 1_048_576 {
                guard let chunk = try? FileHandle.standardInput.read(upToCount: 65_536), !chunk.isEmpty else { break }
                data.append(chunk)
            }
            if data.count <= 1_048_576, let payload = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                try? AgentStore().record(app: args[2], payload: payload, owner: findOwner(args[2]))
            }
            print("{}")
        } else if args.count == 2, args[1] == "snapshot" {
            print(AgentStore().lines().joined(), terminator: "")
        } else {
            fputs("Usage: SpaceSmartAgent hook claude|codex OR snapshot\n", stderr)
            exit(2)
        }
    }
}
