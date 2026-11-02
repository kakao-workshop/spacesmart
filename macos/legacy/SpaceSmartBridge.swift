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
    let eventScript: String
    init(script: String) { eventScript = script }

    func lines() -> [String] {
        var output: [String] = []
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
            output.append(String(format: "pc %.1f %.1f %.1f %.1f\n", percent, memory, Double(rx)/seconds/1024, Double(tx)/seconds/1024))
            previousCPU = cpu
        }
        previousNetwork = network; previousTime = now
        let clock = Calendar.current.dateComponents([.hour, .minute, .second], from: Date())
        output.append("clock \(clock.hour ?? 0) \(clock.minute ?? 0) \(clock.second ?? 0)\n")
        let process = Process(), pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        process.arguments = [eventScript, "--snapshot"]
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            process.waitUntilExit()
            if process.terminationStatus == 0, let text = String(data: data, encoding: .utf8) {
                output += text.split(separator: "\n").map { String($0) + "\n" }
            }
        } catch { log("Agent adapter unavailable: \(error)") }
        return output
    }
}

final class Bridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var characteristic: CBCharacteristic?
    var queue: [Data] = []
    var writing = false
    var timer: Timer?
    let metrics: Metrics
    let requestedID: UUID?
    init(metrics: Metrics, requestedID: UUID?) {
        self.metrics = metrics; self.requestedID = requestedID
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { scan() }
        else { log("Bluetooth state: \(central.state.rawValue) (5=on, 3=authorization required)") }
    }
    func scan() {
        guard central.state == .poweredOn, peripheral == nil else { return }
        log("Scanning for SpaceSmart...")
        central.scanForPeripherals(withServices: [serviceID], options: nil)
    }
    func centralManager(_ central: CBCentralManager, didDiscover candidate: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard peripheral == nil, requestedID == nil || candidate.identifier == requestedID else { return }
        central.stopScan(); peripheral = candidate; candidate.delegate = self
        log("Connecting to \(candidate.name ?? "SpaceSmart") / \(candidate.identifier)")
        central.connect(candidate, options: nil)
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([serviceID])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) { reconnect(error) }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { reconnect(error) }
    func reconnect(_ error: Error?) {
        log("Disconnected: \(error?.localizedDescription ?? "connection ended")")
        timer?.invalidate(); timer = nil; peripheral = nil; characteristic = nil; queue.removeAll(); writing = false
        DispatchQueue.main.asyncAfter(deadline: .now()+3) { self.scan() }
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil else { central.cancelPeripheralConnection(peripheral); return }
        for service in peripheral.services ?? [] where service.uuid == serviceID {
            peripheral.discoverCharacteristics([receiveID], for: service)
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard error == nil, let rx = service.characteristics?.first(where: { $0.uuid == receiveID }) else {
            central.cancelPeripheralConnection(peripheral); return
        }
        characteristic = rx
        log("Connected. Sending CPU, memory, network, clock and observed agent events every 2 seconds.")
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in self.enqueueMetrics() }
        enqueueMetrics()
    }
    func enqueueMetrics() {
        guard queue.count < 30 else { return }
        for line in metrics.lines() {
            let data = Data(line.utf8)
            guard data.count <= 96 else { continue }
            var offset = 0
            while offset < data.count {
                queue.append(data.subdata(in: offset..<min(offset+20, data.count)))
                offset += 20
            }
        }
        sendNext()
    }
    func sendNext() {
        guard !writing, !queue.isEmpty, let peripheral, let characteristic else { return }
        writing = true
        peripheral.writeValue(queue.removeFirst(), for: characteristic, type: .withResponse)
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        writing = false
        if let error { log("BLE write failed: \(error)"); central.cancelPeripheralConnection(peripheral); return }
        sendNext()
    }
}

let arguments = CommandLine.arguments
guard arguments.count >= 2 else {
    print("Usage: SpaceSmartBridge /absolute/path/to/agent_events.py [--metrics | --device UUID]")
    exit(2)
}
let metrics = Metrics(script: arguments[1])
if arguments.contains("--metrics") {
    Thread.sleep(forTimeInterval: 0.5)
    for line in metrics.lines() { print(line, terminator: "") }
} else {
    let deviceIndex = arguments.firstIndex(of: "--device")
    let identifier = deviceIndex.flatMap { $0+1 < arguments.count ? UUID(uuidString: arguments[$0+1]) : nil }
    let bridge = Bridge(metrics: metrics, requestedID: identifier)
    withExtendedLifetime(bridge) { RunLoop.main.run() }
}
