import Foundation
import CoreBluetooth
import Darwin

// Uses only macOS system frameworks. No Python packages or cloud services.
let serviceID = CBUUID(string: "b72c0001-6e35-4b4f-8ad8-08d90a5c9170")
let receiveID = CBUUID(string: "b72c0002-6e35-4b4f-8ad8-08d90a5c9170")

func log(_ message: String) { print(message); fflush(stdout) }

func cpuTicks() -> [UInt32]? {
    var info = host_cpu_load_info()
    var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info>.size / MemoryLayout<integer_t>.size)
    let result = withUnsafeMutablePointer(to: &info) { pointer in
        pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, $0, &count)
        }
    }
    guard result == KERN_SUCCESS else { return nil }
    return [info.cpu_ticks.0, info.cpu_ticks.1, info.cpu_ticks.2, info.cpu_ticks.3]
}

func memoryPercent() -> Double? {
    var info = vm_statistics64()
    var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64>.size / MemoryLayout<integer_t>.size)
    let result = withUnsafeMutablePointer(to: &info) { pointer in
        pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
        }
    }
    guard result == KERN_SUCCESS else { return nil }
    var page: vm_size_t = 0
    guard host_page_size(mach_host_self(), &page) == KERN_SUCCESS else { return nil }
    // Active + wired + compressed pages; excludes reclaimable inactive/file cache.
    let used = (Double(info.active_count) + Double(info.wire_count) + Double(info.compressor_page_count)) * Double(page)
    return min(100, max(0, used / Double(ProcessInfo.processInfo.physicalMemory) * 100))
}

func networkCounters() -> [String: (UInt32, UInt32)] {
    var interfaces: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&interfaces) == 0 else { return [:] }
    defer { freeifaddrs(interfaces) }
    var cursor = interfaces, result: [String: (UInt32, UInt32)] = [:]
    while let current = cursor {
        let item = current.pointee
        let name = String(cString: item.ifa_name)
        if name.hasPrefix("en"), item.ifa_addr?.pointee.sa_family == UInt8(AF_LINK), let data = item.ifa_data {
            let counters = data.assumingMemoryBound(to: if_data.self).pointee
            result[name] = (counters.ifi_ibytes, counters.ifi_obytes)
        }
        cursor = item.ifa_next
    }
    return result
}

final class Metrics {
    var previousCPU = cpuTicks()
    var previousNetwork = networkCounters()
    var previousTime = ProcessInfo.processInfo.systemUptime
    let store: AgentStore
    var demoStarted: TimeInterval?
    var lastPC = "측정 대기"
    init(store: AgentStore = AgentStore()) { self.store = store }

    func lines() -> [String] {
        var output: [String] = []
        var readings = CustomReadings()
        let now = ProcessInfo.processInfo.systemUptime
        let network = networkCounters()
        var rx: UInt64 = 0, tx: UInt64 = 0
        for (name, value) in network {
            if let old = previousNetwork[name] {
                rx += UInt64(value.0 &- old.0); tx += UInt64(value.1 &- old.1)
            }
        }
        let seconds = max(0.1, now - previousTime)
        if let cpu = cpuTicks(), let old = previousCPU, let memory = memoryPercent() {
            let differences = zip(cpu, old).map { UInt64($0.0 &- $0.1) }
            let total = differences.reduce(0, +)
            let percent = total > 0 ? 100 * (1 - Double(differences[2]) / Double(total)) : 0
            readings.cpu = percent; readings.memory = memory
            output.append(String(format: "pc %.1f %.1f %.1f %.1f\n", locale: Locale(identifier: "en_US_POSIX"), percent, memory, Double(rx)/seconds/1024, Double(tx)/seconds/1024))
            previousCPU = cpu
        }
        previousNetwork = network; previousTime = now
        let clock = Calendar.current.dateComponents([.hour, .minute, .second], from: Date())
        output.append("clock \(clock.hour ?? 0) \(clock.minute ?? 0) \(clock.second ?? 0)\n")
        lastPC = output.first(where: { $0.hasPrefix("pc ") })?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "측정 대기"
        var agents = store.lines()
        if let started = demoStarted {
            let elapsed = Date().timeIntervalSince1970 - started
            if elapsed >= 0 && elapsed < 36 {
                let state = ["busy", "waiting", "complete"][Int(elapsed / 12)]
                agents = agents.filter { !$0.hasPrefix("agent codex ") }
                agents.append("agent codex \(state) \(Int(elapsed)) |상태 전송 시험\n")
            } else { demoStarted = nil }
        }
        output += agents
        readings.rx = Double(rx)/seconds/1024; readings.tx = Double(tx)/seconds/1024
        let slots = CustomData.load()
        if slots.contains(where: { $0.source == .disk || $0.source == .diskText }),
           let info = try? FileManager.default.attributesOfFileSystem(forPath: FileManager.default.homeDirectoryForCurrentUser.path) {
            readings.diskTotal = (info[.systemSize] as? NSNumber)?.doubleValue
            readings.diskFree = (info[.systemFreeSize] as? NSNumber)?.doubleValue
        }
        output += CustomData.lines(slots, readings: readings)
        return output
    }
}
