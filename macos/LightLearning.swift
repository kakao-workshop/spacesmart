import Foundation

struct LampStatus {
    var boot: UInt32, uptime: UInt32, remoteID: String, enabled: Bool, radio: Bool, verified: Bool
    var power: Int, brightness: Int, temperature: Int, revision: UInt32, busy: Bool
    var lux: Double, present: Bool, holdSeconds: Int, tx: UInt32, rx: UInt32, dropped: UInt32, learning: Bool
    var received: Date
    init?(_ line: String, now: Date = Date()) {
        let f=line.split(separator:" ").map(String.init)
        guard f.count==19,f[0]=="ls",let boot=UInt32(f[1]),let up=UInt32(f[2]),f[3].count==6,UInt32(f[3],radix:16) != nil,
              let power=Int(f[7]),(-1...1).contains(power),let b=Int(f[8]),(-1...15).contains(b),let t=Int(f[9]),(-1...15).contains(t),let rev=UInt32(f[10]),
              let lux=Double(f[12]),lux.isFinite,lux >= -1,let hold=Int(f[14]),(0...600).contains(hold),let tx=UInt32(f[15]),let rx=UInt32(f[16]),let drop=UInt32(f[17]),
              [4,5,6,11,13,18].allSatisfy({f[$0]=="0"||f[$0]=="1"}) else { return nil }
        self.boot=boot;uptime=up;remoteID=f[3];enabled=f[4]=="1";radio=f[5]=="1";verified=f[6]=="1";self.power=power;brightness=b;temperature=t;revision=rev;busy=f[11]=="1"
        self.lux=lux;present=f[13]=="1";holdSeconds=hold;self.tx=tx;self.rx=rx;dropped=drop;learning=f[18]=="1";received=now
    }
}
struct LightObservation: Codable {
    var key: String, remoteID: String, captured: Date, source: Int, flags: Int
    var before: Double, after: Double, spreadBefore: Double, spreadAfter: Double
    var b0: Int, b1: Int, t0: Int, t1: Int, p0: Int, p1: Int, duration: Double
    var slope: Double? {
        guard flags==0,(0...2).contains(source),p0==1,p1==1,(0...15).contains(b0),(0...15).contains(b1),b0 != b1,t0==t1,(0...15).contains(t0),before>=0,after>=0,
              abs(after-before)>max(1,spreadBefore+spreadAfter) else { return nil }
        let value=(after-before)/Double(b1-b0)
        return value.isFinite && value>0 ? value : nil
    }
    init?(_ line: String, device: String, now: Date = Date()) {
        let f=line.split(separator:" ").map(String.init)
        guard f.count==18,f[0]=="le",let boot=UInt32(f[1]),let seq=UInt32(f[2]),f[3].count==6,UInt32(f[3],radix:16) != nil,let age=UInt32(f[4]),let source=Int(f[5]),(0...3).contains(source),let flags=Int(f[6]),(0...1023).contains(flags),
              let before=Double(f[7]),let after=Double(f[8]),let s0=Double(f[9]),let s1=Double(f[10]),[before,after,s0,s1].allSatisfy({$0.isFinite}),before >= -1,after >= -1,s0>=0,s1>=0,
              let b0=Int(f[11]),let b1=Int(f[12]),let t0=Int(f[13]),let t1=Int(f[14]),[b0,b1,t0,t1].allSatisfy({(-1...15).contains($0)}),
              let p0=Int(f[15]),let p1=Int(f[16]),[p0,p1].allSatisfy({(-1...1).contains($0)}),let duration=UInt32(f[17]) else{return nil}
        key="\(device):\(boot):\(seq)";remoteID=f[3];captured=now.addingTimeInterval(-Double(age)/1000);self.source=source;self.flags=flags
        self.before=before;self.after=after;spreadBefore=s0;spreadAfter=s1;self.b0=b0;self.b1=b1;self.t0=t0;self.t1=t1;self.p0=p0;self.p1=p1;self.duration=Double(duration)/1000
    }
}
struct LightProfile: Codable {
    var device: String, remoteID: String, name: String
    var started: Date = Date()
    var observations: [LightObservation] = []
}
struct LightArchive: Codable {
    var version=1
    var profiles: [LightProfile] = []
    var selected: [String:String] = [:]
}
struct LightAssessment {
    var usable=0, sessions=0, levels=0
    var ready=false, gain: Double=0
    var reason="수동 조절 기록을 모으고 있습니다."
    var examples: [LightObservation] = []
}
func median(_ values:[Double])->Double {
    let a=values.sorted(); guard !a.isEmpty else{return 0}; let m=a.count/2
    return a.count%2==0 ? (a[m-1]+a[m])/2 : a[m]
}
enum LightLearner {
    static func assess(_ profile: LightProfile, now: Date = Date())->LightAssessment {
        var r=LightAssessment()
        // Old evidence expires; profiles never share evidence across positions or devices.
        r.examples=Array(profile.observations.filter{$0.slope != nil && now.timeIntervalSince($0.captured)>=0 && now.timeIntervalSince($0.captured)<90*86400}.sorted{$0.captured<$1.captured}.suffix(200))
        r.usable=r.examples.count;r.levels=Set(r.examples.map{$0.b1}).count
        var previous:Date?
        for e in r.examples { if previous==nil || e.captured.timeIntervalSince(previous!)>=1200 {r.sessions+=1}; previous=e.captured }
        guard r.usable>=20,r.sessions>=3,r.levels>=3 else {r.reason="유효 기록 \(r.usable)/20 · 서로 다른 사용 시간 \(r.sessions)/3 · 밝기 단계 \(r.levels)/3";return r}
        let slopes=r.examples.compactMap{$0.slope};r.gain=median(slopes)
        guard r.gain>0,median(slopes.map{abs($0-r.gain)})/r.gain<=0.35 else {r.reason="조도 반응이 일정하지 않습니다. 센서 위치를 확인하거나 새 위치로 학습하세요.";return r}
        // The final user-selected level must be predictable from ambient context.
        let errors=r.examples.enumerated().compactMap { index,e -> Double? in
            let others=r.examples.enumerated().filter{$0.offset != index}.map{$0.element}
            guard let p=predict(lux:e.before,brightness:e.b0,temperature:e.t0,gain:r.gain,examples:others) else{return nil}
            return abs(p-Double(e.b1))
        }
        guard errors.count>=r.examples.count*3/4,median(errors)<=2 else {r.reason="사용자 선호를 아직 안정적으로 예측하기 어렵습니다. 수동 조절 기록을 더 모읍니다.";return r}
        r.ready=true;r.reason="자동 조절 준비 완료 · 설치 위치별 선호 학습";return r
    }
    static func predict(lux:Double,brightness:Int,temperature:Int,gain:Double,examples:[LightObservation])->Double? {
        guard lux.isFinite,lux>=0,gain>0,(0...15).contains(brightness),(0...15).contains(temperature) else{return nil}
        let ambient=max(0,lux-gain*Double(brightness+1))
        let x=log1p(ambient)
        let neighbors=examples.filter{abs($0.t1-temperature)<=1 && $0.slope != nil}.map { e -> (Double,Int) in
            let a=max(0,e.before-gain*Double(e.b0+1))
            return (abs(log1p(a)-x),e.b1)
        }.filter{$0.0<=0.45}.sorted{$0.0<$1.0}
        guard neighbors.count>=5 else{return nil} // Never extrapolate into unseen ambient conditions.
        let chosen=Array(neighbors.prefix(7)), targets=chosen.map{Double($0.1)}
        guard (targets.max()!-targets.min()!)<=4 else{return nil}
        return median(targets)
    }
}
// File writes and ACKs are ordered: an event is acknowledged only after durable storage.
final class LightStore {
    let url:URL
    private(set) var archive=LightArchive()
    private(set) var loadError:String?
    init(url:URL) {
        self.url=url
        if FileManager.default.fileExists(atPath:url.path) {
            do {archive=try JSONDecoder().decode(LightArchive.self,from:Data(contentsOf:url));guard archive.version==1 else {throw CocoaError(.fileReadCorruptFile)}}
            catch {loadError="학습 파일을 읽지 못했습니다. 원본을 보존합니다.";archive=LightArchive()}
        }
    }
    func index(device:String,remote:String)->Int {
        let key=device+":"+remote, name=archive.selected[key] ?? "기본 위치"
        if let i=archive.profiles.firstIndex(where:{$0.device==device&&$0.remoteID==remote&&$0.name==name}){return i}
        archive.profiles.append(LightProfile(device:device,remoteID:remote,name:name));return archive.profiles.count-1
    }
    func profile(device:String,remote:String)->LightProfile {archive.profiles[index(device:device,remote:remote)]}
    func save() throws {
        if loadError != nil {throw CocoaError(.fileReadCorruptFile)}
        try privateDirectory(url.deletingLastPathComponent());try privateWrite(JSONEncoder().encode(archive),to:url)
    }
    func record(_ event:LightObservation,device:String) throws {
        if archive.profiles.contains(where:{$0.observations.contains(where:{$0.key==event.key})}) {try save();return}
        let i=index(device:device,remote:event.remoteID)
        var e=event
        if e.captured<archive.profiles[i].started {e.flags |= 1024}
        archive.profiles[i].observations.append(e)
        if archive.profiles[i].observations.count>2000 {archive.profiles[i].observations.removeFirst(archive.profiles[i].observations.count-2000)}
        try save()
    }
    func select(name:String,device:String,remote:String) throws {
        let n=String(name.trimmingCharacters(in:.whitespacesAndNewlines).prefix(40));guard !n.isEmpty else{return}
        archive.selected[device+":"+remote]=n;_ = index(device:device,remote:remote);try save()
    }
    func csv(device:String,remote:String)->String {
        var rows=["timestamp,source,flags,lux_before,lux_after,spread_before,spread_after,brightness_before,brightness_after,temperature_before,temperature_after,power_before,power_after,duration_seconds"]
        for e in profile(device:device,remote:remote).observations {
            rows.append([ISO8601DateFormatter().string(from:e.captured),String(e.source),String(e.flags),String(e.before),String(e.after),String(e.spreadBefore),String(e.spreadAfter),String(e.b0),String(e.b1),String(e.t0),String(e.t1),String(e.p0),String(e.p1),String(e.duration)].joined(separator:","))
        }
        return rows.joined(separator:"\n")+"\n"
    }
}
