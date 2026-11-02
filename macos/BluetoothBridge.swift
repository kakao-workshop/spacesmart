import Foundation
import CoreBluetooth

final class BluetoothBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var devices: [UUID: CBPeripheral] = [:]
    var peripheral: CBPeripheral?
    var characteristic: CBCharacteristic?
    var queue: [Data] = []
    var writing = false
    var lastWrite = Date.distantPast
    var timer: Timer?
    var connectionTimeout: Timer?
    var enabled = true
    var status = "Bluetooth 준비 중"
    var connected: Bool { characteristic != nil && peripheral?.state == .connected }
    var onChange: (() -> Void)?
    let metrics = Metrics()
    let light = LightController()
    let lightTX = CBUUID(string:"b72c0003-6e35-4b4f-8ad8-08d90a5c9170")
    var lightNotify:CBCharacteristic?
    var requestedID: UUID? = UserDefaults.standard.string(forKey: "deviceID").flatMap(UUID.init(uuidString:))
    override init() {
        super.init()
        light.transport = { [weak self] line in self?.enqueueCommand(line) ?? false }
        central = CBCentralManager(delegate: self, queue: .main)
    }
    func update(_ text: String) { status = text; log(text); onChange?() }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: scan()
        case .unauthorized: update("Bluetooth 권한이 필요합니다. 시스템 설정에서 SpaceSmart를 허용하세요.")
        case .poweredOff: update("Mac의 Bluetooth를 켜세요.")
        case .unsupported: update("이 Mac에서는 Bluetooth LE를 사용할 수 없습니다.")
        default: update("Bluetooth 준비 중")
        }
    }
    func scan() {
        guard enabled, central.state == .poweredOn, peripheral == nil else { return }
        central.scanForPeripherals(withServices: [serviceID], options: nil)
        update(requestedID == nil ? "주변 기기를 찾고 있습니다. 목록에서 내 기기를 선택하세요." : "저장한 기기를 찾고 있습니다.")
    }
    func centralManager(_ central: CBCentralManager, didDiscover candidate: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        devices[candidate.identifier] = candidate
        onChange?()
        if candidate.identifier == requestedID { connect(candidate.identifier) }
    }
    func connect(_ id: UUID) {
        guard peripheral == nil, let candidate = devices[id] else { return }
        enabled = true; requestedID = id
        UserDefaults.standard.set(id.uuidString, forKey: "deviceID")
        central.stopScan(); peripheral = candidate; candidate.delegate = self
        update("SpaceSmart 연결 중…")
        central.connect(candidate, options: nil)
        connectionTimeout?.invalidate()
        connectionTimeout = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self, weak candidate] _ in
            guard let self, let candidate, self.peripheral === candidate, !self.connected else { return }
            self.central.cancelPeripheralConnection(candidate)
        }
    }
    func disconnect(forget: Bool = false) {
        enabled = false; central.stopScan(); timer?.invalidate(); timer = nil; connectionTimeout?.invalidate()
        if forget { requestedID = nil; UserDefaults.standard.removeObject(forKey: "deviceID"); devices.removeAll() }
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        clearConnection(); update("연결 중지")
    }
    func clearConnection() { lightNotify=nil; light.disconnected(); timer?.invalidate(); timer = nil; connectionTimeout?.invalidate(); peripheral = nil; characteristic = nil; queue.removeAll(); writing = false }
    func reconnect() {
        clearConnection()
        guard enabled else { return }
        update("연결이 끊겼습니다. 자동으로 다시 찾습니다.")
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in self?.scan() }
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { peripheral.discoverServices([serviceID]) }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) { if self.peripheral === peripheral { reconnect() } }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { if self.peripheral === peripheral { reconnect() } }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == serviceID }) else { central.cancelPeripheralConnection(peripheral); return }
        peripheral.discoverCharacteristics([receiveID,lightTX], for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard self.peripheral === peripheral else { return }
        guard error == nil, let rx = service.characteristics?.first(where: { $0.uuid == receiveID }) else { central.cancelPeripheralConnection(peripheral); return }
        characteristic = rx; connectionTimeout?.invalidate()
        light.connected(device:peripheral.identifier.uuidString)
        lightNotify=service.characteristics?.first(where:{$0.uuid==lightTX})
        if let lightNotify {peripheral.setNotifyValue(true,for:lightNotify)}
        update("연결됨 · 2초마다 PC 상태와 시간을 전송합니다.")
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in self?.enqueue() }
        enqueue()
    }
    func enqueueCommand(_ line:String)->Bool {
        guard connected,lightNotify?.isNotifying==true,queue.count<35,line.utf8.count<=96,line.hasSuffix("\n"),!line.dropLast().contains("\n") else{return false}
        let data=Data(line.utf8)
        for offset in stride(from:0,to:data.count,by:20){queue.append(data.subdata(in:offset..<min(offset+20,data.count)))}
        sendNext();return true
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateValueFor characteristic:CBCharacteristic,error:Error?) {
        guard self.peripheral===peripheral,characteristic.uuid==lightTX,error==nil,let data=characteristic.value else{return}
        light.receive(data)
    }
    func peripheral(_ peripheral:CBPeripheral,didUpdateNotificationStateFor characteristic:CBCharacteristic,error:Error?) {
        if characteristic.uuid==lightTX,error != nil {light.message="조명 데이터 알림을 켜지 못했습니다. 다시 연결하세요.";light.onChange?()}
    }
    func enqueue() {
        light.tick()
        if writing && Date().timeIntervalSince(lastWrite) > 8, let peripheral { central.cancelPeripheralConnection(peripheral); return }
        guard queue.count < 30 else { return }
        for line in metrics.lines() {
            let data = Data(line.utf8)
            guard data.count <= 96 else { continue }
            for offset in stride(from: 0, to: data.count, by: 20) { queue.append(data.subdata(in: offset..<min(offset + 20, data.count))) }
        }
        onChange?(); sendNext()
    }
    func sendNext() {
        guard !writing, !queue.isEmpty, let peripheral, let characteristic else { return }
        writing = true; lastWrite = Date()
        peripheral.writeValue(queue.removeFirst(), for: characteristic, type: .withResponse)
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard self.peripheral === peripheral else { return }
        writing = false
        if error != nil { central.cancelPeripheralConnection(peripheral); return }
        sendNext()
    }
}
