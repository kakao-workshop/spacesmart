import Foundation
final class LightController {
    let store:LightStore
    var status:LampStatus?
    var device=""
    var transport:((String)->Bool)?
    var onChange:(()->Void)?
    private(set) var automatic=false
    var message="조명을 설치한 뒤 리모컨을 등록하세요."
    var buffer=Data()
    var pending:[UInt32:(Date,String)]=[:]
    var lastAuto=Date.distantPast
    var autoFailures=0
    var stable:[Double]=[]
    init(store:LightStore=LightStore(url:HookInstaller().paths.support.appendingPathComponent("light-learning.json"))) {self.store=store}
    func connected(device:String) {self.device=device;status=nil;automatic=false;buffer.removeAll();pending.removeAll();stable.removeAll()}
    func disconnected() {status=nil;automatic=false;buffer.removeAll();pending.removeAll();stable.removeAll();message="연결 전 · 자동 조절 중지";onChange?()}
    func send(_ op:String)->Bool {
        if ["enable ","id ","verify ","learn","pair","unknown"].contains(where:{op.hasPrefix($0)}) {automatic=false;stable.removeAll()}
        guard transport != nil,!device.isEmpty,pending.count<4 else {message="기기 연결을 확인하세요.";onChange?();return false}
        let token=UInt32.random(in:1...UInt32.max)
        guard transport?("lamp \(token) \(op)\n")==true else{message="전송 대기열이 찼습니다. 다시 시도하세요.";onChange?();return false}
        pending[token]=(Date(),op);message="명령 전송 중 · 조명 상태는 추정입니다.";onChange?();return true
    }
    var profile:LightProfile? {guard let s=status else{return nil};return store.profile(device:device,remote:s.remoteID)}
    var assessment:LightAssessment {profile.map{LightLearner.assess($0)} ?? LightAssessment()}
    func selectProfile(_ name:String) throws {guard let s=status else{return};automatic=false;stable.removeAll();try store.select(name:name,device:device,remote:s.remoteID);message="위치 변경 · 자동 조절은 꺼졌습니다.";onChange?()}
    func setAutomatic(_ value:Bool) {
        if value {
            guard let s=status,s.verified,s.enabled,s.radio,s.power==1,s.brightness>=0,s.temperature>=0,Date().timeIntervalSince(s.received)<8,assessment.ready else {message="실물 동작 확인·켜짐 상태·충분한 학습이 필요합니다.";onChange?();return}
        }
        automatic=value;autoFailures=0;stable.removeAll();message=value ? "자동 조절 켜짐 · 조건이 맞으면 한 단계씩 변경합니다." : "자동 조절 꺼짐";onChange?()
    }
    func receive(_ bytes:Data,now:Date=Date()) {
        buffer.append(bytes)
        if buffer.count>1024 {buffer.removeAll();return}
        while let end=buffer.firstIndex(of:10) {
            let data=buffer.prefix(upTo:end);buffer.removeSubrange(...end)
            guard let line=String(data:data,encoding:.utf8) else{continue}
            if let s=LampStatus(line,now:now) {
                if let old=status,old.boot != s.boot || old.remoteID != s.remoteID {automatic=false;stable.removeAll()}
                status=s;_ = store.index(device:device,remote:s.remoteID)
                if s.lux>=0 {stable.append(s.lux);if stable.count>5{stable.removeFirst()}} else {stable.removeAll()}
                evaluate(now:now)
            } else if let e=LightObservation(line,device:device,now:now) {
                do {
                    try store.record(e,device:device)
                    let f=line.split(separator:" ")
                    _ = transport?("lamp 0 ack \(f[1]) \(f[2])\n")
                    if e.source==3,automatic {
                        let gain=assessment.gain
                        let observed=e.b1==e.b0 ? 0 : (e.after-e.before)/Double(e.b1-e.b0)
                        if e.flags != 64 || gain<=0 || observed<=0 || abs(observed-gain)>gain*0.6 {autoFailures+=1} else{autoFailures=0}
                        if autoFailures>=2 {automatic=false;message="예상과 다른 조도 변화가 반복돼 자동 조절을 멈췄습니다. 설치 위치를 확인하세요."}
                    }
                } catch {automatic=false;message="학습 기록 저장 실패 · 자동 조절 중지. 데이터 폴더를 확인하세요."}
            } else {
                let f=line.split(separator:" ")
                if f.count==5,f[0]=="la",let token=UInt32(f[1]),let request=pending.removeValue(forKey:token) {
                    if f[2]=="1" {message=request.1.hasPrefix("auto ") ? "자동 밝기 한 단계 전송 · 조도 변화 확인 중" : "기기가 명령을 접수했습니다. 조명 동작을 확인하세요."}
                    else {message="명령을 적용하지 못했습니다: \(f[3])";if request.1.hasPrefix("auto "){automatic=false}}
                }
            }
            onChange?()
        }
    }
    func tick(now:Date=Date()) {
        let expired=pending.filter{now.timeIntervalSince($0.value.0)>10}
        if !expired.isEmpty {for (token,_) in expired{pending.removeValue(forKey:token)};automatic=false;message="기기 응답 시간 초과 · 자동 재전송하지 않습니다.";onChange?()}
        if let s=status,now.timeIntervalSince(s.received)>10,automatic {automatic=false;message="센서 데이터가 끊겨 자동 조절을 멈췄습니다.";onChange?()}
    }
    func evaluate(now:Date) {
        guard automatic,let s=status else{return}
        let r=assessment
        guard r.ready,s.enabled,s.radio,s.verified,s.power==1,s.lux>=0,s.present,!s.busy,s.holdSeconds==0,pending.isEmpty,now.timeIntervalSince(lastAuto)>=90,stable.count==5 else{return}
        let spread=stable.max()!-stable.min()!
        guard spread<=max(3,median(stable)*0.1) else{return}
        guard let target=LightLearner.predict(lux:s.lux,brightness:s.brightness,temperature:s.temperature,gain:r.gain,examples:r.examples) else {message="아직 배우지 못한 주변 밝기입니다. 수동 조절 기록을 더 모읍니다.";return}
        guard abs(target-Double(s.brightness))>=1.5 else{return}
        let delta=target>Double(s.brightness) ? 1 : -1
        if send("auto \(s.revision) \(delta)") {lastAuto=now;stable.removeAll()}
    }
}
