import Foundation
@main struct LightTests {
    static func main() throws {
        let now=Date(), start=now.addingTimeInterval(-11000)
        var profile=LightProfile(device:"device-a",remoteID:"ABCDEF",name:"기본 위치",started:start.addingTimeInterval(-10))
        for group in 0..<3 {
            let target=[4,8,12][group], ambient=[100,200,400][group]
            for i in 0..<7 {
                let before=ambient+(target-1)*10,after=ambient+(target+1)*10
                let line="le 42 \(group*7+i+1) ABCDEF 0 1 0 \(before) \(after) 0.2 0.2 \(target-2) \(target) 0 0 1 1 12000"
                var e=LightObservation(line,device:"device-a",now:start.addingTimeInterval(Double(group*3600+i*60)))!
                if i==6 {e.source=2}
                profile.observations.append(e)
            }
        }
        let a=LightLearner.assess(profile,now:now)
        assert(a.ready && a.usable==21 && a.sessions==3 && a.levels==3 && a.gain==10)
        assert(LightLearner.predict(lux:130,brightness:2,temperature:0,gain:10,examples:a.examples)==4)
        assert(LightLearner.predict(lux:10000,brightness:2,temperature:0,gain:10,examples:a.examples)==nil)
        assert(LightLearner.predict(lux:130,brightness:2,temperature:15,gain:10,examples:a.examples)==nil)
        var bad=profile;bad.observations=Array(bad.observations.prefix(19));assert(!LightLearner.assess(bad).ready)
        bad=profile;for i in bad.observations.indices{bad.observations[i].source=3;bad.observations[i].flags=64};assert(LightLearner.assess(bad).usable==0)
        bad=profile;for i in bad.observations.indices{bad.observations[i].after=bad.observations[i].before-10};assert(!LightLearner.assess(bad).ready)
        bad=profile;for i in bad.observations.indices{bad.observations[i].captured=now.addingTimeInterval(Double(i-30))};assert(!LightLearner.assess(bad).ready)
        bad=profile;for i in bad.observations.indices{bad.observations[i].flags=128};assert(LightLearner.assess(bad).usable==0)
        assert(LampStatus("ls 1 1 ABCDEF 1 1 1 1 7 0 1 0 nan 1 0 0 0 0 0")==nil)
        assert(LightObservation("le 1 1 ABCDEF 0 1 0 nan 10 0 0 1 2 0 0 1 1 12000",device:"a")==nil)
        let dir=FileManager.default.temporaryDirectory.appendingPathComponent("spacesmart-light-tests-"+UUID().uuidString)
        try privateDirectory(dir);defer{try? FileManager.default.removeItem(at:dir)}
        let url=dir.appendingPathComponent("learning.json")
        try privateWrite(JSONEncoder().encode(LightArchive(profiles:[profile])),to:url)
        let store=LightStore(url:url);let c=LightController(store:store)
        var sent:[String]=[];c.transport={sent.append($0);return true};c.connected(device:"device-a")
        let line="ls 1 100000 ABCDEF 1 1 1 1 2 0 5 0 130 1 0 0 0 0 0\n"
        // Fragmentation, including across newline; five stable snapshots before action.
        for b in line.utf8 {c.receive(Data([b]),now:now)}
        assert(c.status?.brightness==2 && c.assessment.ready)
        c.setAutomatic(true);assert(c.automatic)
        for i in 1...5{c.receive(Data(line.utf8),now:now.addingTimeInterval(Double(i*4)))}
        assert(sent.count==1 && sent[0].contains(" auto 5 1\n"))
        for i in 6...10{c.receive(Data(line.utf8),now:now.addingTimeInterval(Double(i*4)))}
        assert(sent.count==1) // One outstanding action, never replayed as telemetry.
        c.tick(now:now.addingTimeInterval(50));assert(!c.automatic)
        c.disconnected();assert(!c.automatic&&c.status==nil)
        let e=profile.observations[0];try store.record(e,device:"device-a");assert(store.profile(device:"device-a",remote:"ABCDEF").observations.count==21)
        assert(store.profile(device:"device-b",remote:"ABCDEF").observations.isEmpty)
        try store.select(name:"창가",device:"device-a",remote:"ABCDEF");assert(store.profile(device:"device-a",remote:"ABCDEF").observations.isEmpty)
        assert(LightStore(url:url).profile(device:"device-a",remote:"ABCDEF").name=="창가")
        try Data("broken".utf8).write(to:url);let broken=LightStore(url:url);assert(broken.loadError != nil)
        do{try broken.record(e,device:"device-a");assertionFailure("must preserve corrupt file")}catch{}
        let preserved=try String(contentsOf:url,encoding:.utf8);assert(preserved=="broken")
        print("PASS light learning: sample/session/level gates, personal ambient prediction, unknown contexts, automatic-data exclusion, profile isolation/persistence, corrupt-file preservation, stream fragmentation, one-step command, timeout and disconnect stops")
    }
}
