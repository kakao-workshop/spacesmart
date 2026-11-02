#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../SpaceSmart/DashboardRenderer.h"
using namespace Dashboard;

void save(const std::filesystem::path& path, const Canvas& canvas) {
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << canvas.width << ' ' << canvas.height << "\n255\n";
  for (int i = 0; i < canvas.width * canvas.height; ++i) {
    const uint16_t p = canvas.pixels[i];
    const char rgb[] = {char(((p >> 11) & 31) * 255 / 31), char(((p >> 5) & 63) * 255 / 63),
                        char((p & 31) * 255 / 31)};
    out.write(rgb, 3);
  }
  assert(out.good());
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::filesystem::path dir(argv[1]);
  std::filesystem::create_directories(dir);
  const char* pages[] = {"environment", "graph", "computer", "custom", "agents",
                         "focus", "clock", "diagnostics", "settings", "light", "ledtest", "lightsetup"};
  State state;
  History history;
  state.now = 86400000;
  state.clockValid = true;
  state.clockSeconds = 14 * 3600 + 32 * 60;
  state.clockAt = state.now;
  state.sensors.value[0] = 24.6;
  state.sensors.value[1] = 48;
  state.sensors.value[2] = 1240;
  state.sensors.value[3] = 420;
  for (auto& valid : state.sensors.valid) valid = true;
  state.sensors.radarValid = state.sensors.present = state.sensors.scd = state.sensors.light =
      state.sensors.radio = true;
  state.co2Level = 1;
  state.bleConnected = true;
  state.freeHeap = 165 * 1024;
  state.computer = {true, state.now, 38, 62, 128.4, 24.6};
  telemetry("agent claude busy 154 |대시보드 레이아웃 정리", state, state.now);
  telemetry("agent codex waiting 42 |변경 사항 검토 요청", state, state.now);
  telemetry("custom 0 gauge 37.5 |CPU 사용률", state, state.now);
  telemetry("custom 1 text |네트워크|RX 128KiB|TX 24KiB", state, state.now);
  telemetry("custom 2 text |집중 메모|설계 작업|진행 중", state, state.now);
  state.light.enabled=state.light.radio=state.light.verified=true; state.light.id=0x123456; state.light.state={1,7,5};
  state.focus.running = true;
  state.focus.remainingMs = (18 * 60 + 42) * 1000;
  for (uint32_t at = 0; at <= state.now; at += 10000) {
    Sensors sensors = state.sensors;
    sensors.value[2] = 850 + 390 * double(at) / state.now + 85 * std::sin(at / 240000.0) +
                       35 * std::cos(at / 96000.0);
    sensors.value[0] += std::sin(at / 900000.0);
    sensors.value[1] += 5 * std::sin(at / 1200000.0);
    sensors.value[3] += 100 * std::sin(at / 600000.0);
    history.tick(sensors, at);
  }
  std::vector<uint16_t> pixels(240 * 320 + 2, 0xdead);
  for (int orientation = 0; orientation < 2; ++orientation) {
    const char* layout = orientation ? "portrait" : "landscape";
    Canvas c{pixels.data() + 1, orientation ? 240 : 320, orientation ? 320 : 240};
    state.rotation = orientation ? 0 : 3;
    for (int theme = 0; theme < 4; ++theme) {
      state.theme = theme;
      for (int page = 0; page < 12; ++page) {
        state.page = page == 10 ? Page::LedTest : page == 11 ? Page::LightSetup : Page(page);
        state.detail = true;
        state.editing = state.page == Page::Settings;
        render(c, state, history);
        assert(pixels.front() == 0xdead && pixels.back() == 0xdead);
        save(dir / (std::string(layout) + "-" + THEMES[theme].id + "-" + pages[page] + ".ppm"), c);
      }
    }
    State disconnected;
    disconnected.rotation = state.rotation;
    for (int page = 0; page < 12; ++page) {
      disconnected.page = page == 10 ? Page::LedTest : page == 11 ? Page::LightSetup : Page(page);
      render(c, disconnected, History{});
      save(dir / (std::string(layout) + "-missing-" + pages[page] + ".ppm"), c);
    }
  }
  puts("PASS: 96 theme/mode/orientation renders and 24 missing-data screens; fixture data only");
}
