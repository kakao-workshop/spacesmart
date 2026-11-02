#include <cassert>
#include <cstdio>

#include "../SpaceSmart/DashboardController.h"
using namespace Dashboard;
int main() {
  State s;
  assert(telemetry("pc 38.5 62 120 5.4", s, 100));
  assert(s.computer.cpu == 38.5f && fresh(true, 100, 6099, PC_TTL));
  assert(!fresh(true, 100, 6100, PC_TTL));
  for (const char* bad :
       {"pc nan 2 3 4", "pc 0 inf 0 0", "pc 101 2 3 4", "pc -1 2 3 4", "pc 1 2 3 4 junk",
        "pc 1 2 3", "clock 24 0 0", "clock -1 0 0", "clock 23 59 60", "agent chat busy 0 |test",
        "agent codex running 0", "agent codex busy -1", "agent codex busy 99999999999999999999999",
        "agent codex busy 2 unexpected"})
    assert(!telemetry(bad, s, 200));
  assert(s.computer.at == 100);
  assert(telemetry("custom 0 gauge 37.5 |CPU", s, 120));
  assert(s.custom[0].kind == CustomKind::Gauge && s.custom[0].percent == 37.5f);
  assert(s.custom[0].valid(10119) && !s.custom[0].valid(10120));
  assert(telemetry("custom 1 text |네트워크|RX 12KiB|TX 4KiB", s, 121));
  assert(!strcmp(s.custom[1].left, "RX 12KiB") && !strcmp(s.custom[1].right, "TX 4KiB"));
  for (const char* bad : {"custom 3 gauge 50 |bad", "custom -1 clear", "custom 0 gauge nan |bad",
       "custom 0 gauge 101 |bad", "custom 0 gauge -1 |bad", "custom 0 gauge 1 junk |bad",
       "custom 0 gauge 1 |", "custom 1 text |title|left", "custom 1 text ||left|right",
       "custom 1 text |title|left|right|extra", "custom 1 text |1234567890123456789012345|left|right",
       "custom 1 text |title|left\t|right", "custom 0 clear extra"}) assert(!telemetry(bad, s, 130));
  assert(s.custom[0].at == 120 && s.custom[1].at == 121);
  assert(telemetry("custom 1 text |title||right", s, 140));
  assert(telemetry("custom 1 clear", s, 150) && s.custom[1].kind == CustomKind::Empty);
  assert(telemetry("agent codex busy 42 |한글 Task", s, 1000));
  assert(!strcmp(s.agents[1].title, "한글 Task"));
  assert(s.agents[1].visibleStatus(15999) == AgentStatus::Busy);
  assert(s.agents[1].visibleStatus(16000) == AgentStatus::Unknown);
  assert(s.agents[1].elapsed(2000) == 43);
  assert(telemetry("agent claude waiting 80 |", s, 1000));
  assert(s.agents[0].visibleStatus(1000) == AgentStatus::Waiting);
  assert(telemetry("clock 23 59 59", s, UINT32_MAX - 499));
  s.now = 500;
  assert(s.localSeconds() == 0);
  FocusTimer f;
  f.toggle(UINT32_MAX - 999);
  f.tick(1000);
  assert(f.remainingMs == 25 * 60000 - 2000);
  f.toggle(1000);
  f.tick(5000);
  assert(f.remainingMs == 25 * 60000 - 2000);
  f.toggle(5000);
  f.tick(25 * 60000 + 5000);
  assert(f.finished && !f.running && f.remainingMs == 0);
  f.toggle(25 * 60000 + 6000);
  assert(f.running && !f.finished && f.remainingMs == 25 * 60000);
  f.setMinutes(200);
  assert(f.minutes == 120 && !f.running);
  f.setMinutes(-1);
  assert(f.minutes == 5);
  Button b;
  b.begin(false, 0);
  assert(b.update(true, 10) == Button::None);
  assert(b.update(true, 40) == Button::None);
  assert(b.update(true, 840) == Button::Long);
  assert(b.update(false, 850) == Button::None);
  assert(b.update(false, 880) == Button::None);
  b.begin(true, 0);
  assert(b.update(true, 1000) == Button::None);
  b.update(false, 1001);
  assert(b.update(false, 1031) == Button::None);
  b.update(true, 1100);
  b.update(true, 1130);
  b.update(false, 1200);
  assert(b.update(false, 1230) == Button::Short);
  s = State{};
  rotate(s, -1);
  assert(s.page == Page::Light);
  rotate(s, -1);
  assert(s.page == Page::Settings);
  push(s, 0);
  assert(s.detail && !s.editing);
  push(s, 0);
  assert(s.editing);
  assert(rotate(s, -1) && s.theme == 3);
  back(s);
  assert(!s.editing && s.detail);
  rotate(s, 1);
  push(s, 0);
  rotate(s, 1);
  assert(s.rotation == 0);
  back(s);
  rotate(s, 1);
  push(s, 0);
  rotate(s, -32);
  assert(s.brightness == 0);
  rotate(s, 32);
  assert(s.brightness == 64);
  back(s, true);
  assert(s.page == Page::Environment && !s.detail);
  s.page = Page::Graph;
  push(s, 0);
  rotate(s, 1);
  assert(s.metric == 3);
  push(s, 0);
  assert(s.span == 1);
  back(s);
  assert(!s.detail);
  openSettings(s); s.setting = 4; push(s, 200);
  assert(s.page == Page::LedTest && !s.editing);
  assert(!rotate(s, -1) && s.ledTest.selected == 5);
  push(s, UINT32_MAX - 100);
  assert(s.ledTest.active && s.ledTest.runningMode == 5);
  rotate(s, 1); assert(s.ledTest.runningMode == 5 && s.ledTest.selected == 0);
  s.ledTest.tick(14898); assert(s.ledTest.active);
  s.ledTest.tick(14899); assert(!s.ledTest.active);
  push(s, 500); back(s); assert(!s.ledTest.active && s.page == Page::Settings && s.setting == 4);
  push(s, 600); push(s, 601); assert(s.ledTest.active);
  openSettings(s); assert(!s.ledTest.active && s.page == Page::Settings);
  History h;
  Sensors sensors;
  sensors.valid[2] = true;
  sensors.value[2] = 800;
  for (uint32_t t = 0; t <= 60000; t += 10000) h.tick(sensors, t);
  assert(h.recent.count == 7 && h.day.count == 1 && h.day.at(0).value[2] == 800);
  sensors.valid[2] = false;
  for (uint32_t t = 70000; t <= 120000; t += 10000) h.tick(sensors, t);
  assert(h.day.count == 2 && h.day.at(1).valid == 0 && h.recent.at(7).valid == 0);
  Ring<3> r;
  for (uint32_t t = 0; t < 8; ++t) {
    Sample sample;
    sample.at = t;
    r.push(sample);
  }
  assert(r.count == 3 && r.at(0).at == 5 && r.at(2).at == 7);
  assert(fresh(true, UINT32_MAX - 9, 10, 21));
  assert(!fresh(true, UINT32_MAX - 9, 10, 20));
  puts(
      "PASS: telemetry validation, freshness, timer rollover, navigation, long press, history gaps "
      "and ring buffers");
}
