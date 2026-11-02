#pragma once
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "DashboardThemes.h"
#include "LightbarModel.h"

namespace Dashboard {
enum class Page : uint8_t {
  Environment,
  Graph,
  Computer,
  Custom,
  Agents,
  Focus,
  Clock,
  Diagnostics,
  Settings,
  Light,
  Count,
  LedTest,
  LightSetup
};
enum class AgentStatus : uint8_t {
  Unknown,
  Idle,
  Busy,
  Waiting,
  Complete,
  Error,
  Interrupted,
  Offline
};
constexpr uint32_t PC_TTL = 6000, AGENT_TTL = 15000, CUSTOM_TTL = 10000;
inline bool fresh(bool valid, uint32_t at, uint32_t now, uint32_t ttl) {
  return valid && uint32_t(now - at) < ttl;
}
inline int wrap(int value, int count) { return (value % count + count) % count; }
struct Agent {
  AgentStatus status = AgentStatus::Unknown;
  bool received = false;
  uint32_t at = 0, elapsedSeconds = 0;
  char title[64] = {};
  AgentStatus visibleStatus(uint32_t now) const {
    return fresh(received, at, now, AGENT_TTL) ? status : AgentStatus::Unknown;
  }
  uint32_t elapsed(uint32_t now) const {
    return elapsedSeconds + (status == AgentStatus::Busy && fresh(received, at, now, AGENT_TTL)
                                 ? uint32_t(now - at) / 1000
                                 : 0);
  }
};
struct Computer {
  bool received = false;
  uint32_t at = 0;
  float cpu = 0, memory = 0, rx = 0, tx = 0;
};
enum class CustomKind : uint8_t { Empty, Gauge, Text };
struct CustomItem {
  CustomKind kind = CustomKind::Empty;
  uint32_t at = 0;
  float percent = 0;
  char title[25] = {}, left[25] = {}, right[25] = {};
  bool valid(uint32_t now) const { return kind != CustomKind::Empty && fresh(true, at, now, CUSTOM_TTL); }
};
struct LedTestState {
  uint8_t selected = 0, runningMode = 0;
  bool active = false;
  uint32_t startedAt = 0;
  void start(uint32_t now) { active = true; runningMode = selected; startedAt = now; }
  void stop() { active = false; }
  void tick(uint32_t now) { if (active && uint32_t(now - startedAt) >= 15000) stop(); }
};
struct Sensors {
  float value[4] = {};
  bool valid[4] = {};
  bool radarValid = false, present = false, moving = false, radio = false, scd = false,
       light = false, saturated = false;
};
struct FocusTimer {
  uint8_t minutes = 25;
  bool running = false, finished = false;
  uint32_t remainingMs = 25 * 60000UL, lastAt = 0;
  void tick(uint32_t now) {
    if (!running) return;
    const uint32_t dt = now - lastAt;
    lastAt = now;
    if (dt >= remainingMs) {
      remainingMs = 0;
      running = false;
      finished = true;
    } else
      remainingMs -= dt;
  }
  void toggle(uint32_t now) {
    tick(now);
    if (finished) reset();
    running = !running;
    lastAt = now;
  }
  void reset() {
    running = false;
    finished = false;
    remainingMs = uint32_t(minutes) * 60000UL;
  }
  void setMinutes(int value) {
    minutes = uint8_t(std::max(5, std::min(120, value)));
    reset();
  }
};
struct State {
  Page page = Page::Environment;
  uint8_t theme = 0, rotation = 3, brightness = 20;
  bool detail = false, editing = false, bleConnected = false, bleEnabled = true, prefsSaved = true;
  uint8_t metric = 2, span = 0, setting = 0;
  Sensors sensors;
  Computer computer;
  CustomItem custom[3];
  LedTestState ledTest;
  Lightbar::View light;
  uint8_t lightItem=0, lightSetupItem=0;
  int8_t lightRequest=-1, lightDelta=0;
  uint32_t lightPairConfirmAt=0;
  bool lightPairConfirm=false;
  Agent agents[2];
  FocusTimer focus;
  bool clockValid = false;
  uint32_t clockSeconds = 0, clockAt = 0;
  uint16_t co2Yellow = 1000, co2Red = 1500;
  uint8_t co2Level = 0;
  uint32_t now = 0, freeHeap = 0;
  bool displayReady = false;
  uint32_t localSeconds() const { return (clockSeconds + uint32_t(now - clockAt) / 1000) % 86400; }
};
struct Sample {
  uint32_t at = 0;
  float value[4] = {};
  uint8_t valid = 0;
};
template <size_t Capacity>
struct Ring {
  Sample data[Capacity] = {};
  size_t head = 0, count = 0;
  void push(const Sample& s) {
    data[head] = s;
    head = (head + 1) % Capacity;
    if (count < Capacity) ++count;
  }
  const Sample& at(size_t i) const { return data[(head + Capacity - count + i) % Capacity]; }
};
struct History {
  Ring<181> recent;
  Ring<1441> day;
  uint32_t lastSample = 0, lastMinute = 0;
  bool started = false;
  double totals[4] = {};
  uint16_t counts[4] = {};
  void tick(const Sensors& sensors, uint32_t now) {
    if (!started) {
      started = true;
      lastSample = now - 10000;
      lastMinute = now;
    }
    if (uint32_t(now - lastSample) < 10000) return;
    lastSample = now;
    Sample sample;
    sample.at = now;
    for (int m = 0; m < 4; ++m)
      if (sensors.valid[m] && isfinite(sensors.value[m])) {
        sample.value[m] = sensors.value[m];
        sample.valid |= 1u << m;
        totals[m] += sample.value[m];
        ++counts[m];
      }
    recent.push(sample);
    if (uint32_t(now - lastMinute) >= 60000) {
      Sample minute;
      minute.at = now;
      for (int m = 0; m < 4; ++m) {
        if (counts[m]) {
          minute.value[m] = float(totals[m] / counts[m]);
          minute.valid |= 1u << m;
        }
        totals[m] = 0;
        counts[m] = 0;
      }
      day.push(minute);
      lastMinute = now;
    }
  }
};
// Press is emitted on release. A long hold never produces a second short action.
class Button {
  bool raw = false, held = false, longSent = false, armed = true;
  uint32_t changed = 0, pressedAt = 0;

 public:
  enum Event { None, Short, Long };
  void begin(bool down, uint32_t now) {
    raw = held = down;
    longSent = false;
    armed = !down;
    changed = pressedAt = now;
  }
  Event update(bool down, uint32_t now) {
    if (down != raw) {
      raw = down;
      changed = now;
    }
    if (raw != held && uint32_t(now - changed) >= 30) {
      held = raw;
      if (held) {
        pressedAt = now;
        longSent = false;
      } else {
        const bool click = armed && !longSent;
        armed = true;
        return click ? Short : None;
      }
    }
    if (held && armed && !longSent && uint32_t(now - pressedAt) >= 800) {
      longSent = true;
      return Long;
    }
    return None;
  }
};
inline const char* statusName(AgentStatus state) {
  switch (state) {
    case AgentStatus::Idle:
      return "대기";
    case AgentStatus::Busy:
      return "작업 중";
    case AgentStatus::Waiting:
      return "확인 필요";
    case AgentStatus::Complete:
      return "응답 완료";
    case AgentStatus::Error:
      return "오류";
    case AgentStatus::Interrupted:
      return "중단";
    case AgentStatus::Offline:
      return "연결 끊김";
    default:
      return "상태 미확인";
  }
}
inline bool parseAgentStatus(const char* text, AgentStatus& out) {
  const char* names[] = {"unknown",  "idle",  "busy",        "waiting",
                         "complete", "error", "interrupted", "offline"};
  for (uint8_t i = 0; i < 8; ++i)
    if (!strcmp(text, names[i])) {
      out = AgentStatus(i);
      return true;
    }
  return false;
}
inline bool readUnsigned(const char*& p, uint32_t limit, uint32_t& result) {
  while (isspace(static_cast<unsigned char>(*p))) ++p;
  if (*p < '0' || *p > '9') return false;
  uint32_t value = 0;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = uint32_t(*p - '0');
    if (value > limit / 10 || (value == limit / 10 && digit > limit % 10)) return false;
    value = value * 10 + digit;
    ++p;
  }
  if (*p && !isspace(static_cast<unsigned char>(*p)) && *p != '|') return false;
  result = value;
  return true;
}
// A bounded, line-based protocol works over both fragmented BLE writes and USB.
inline bool telemetry(const char* line, State& state, uint32_t now) {
  int n = 0;
  if (!strncmp(line, "custom ", 7)) {
    const char* p = line + 7;
    uint32_t slot;
    if (!readUnsigned(p, 2, slot)) return false;
    while (isspace(static_cast<unsigned char>(*p))) ++p;
    CustomItem item;
    item.at = now;
    auto field = [](char* out, const char* begin, size_t length) {
      if (length > 24) return false;
      for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(begin[i]) < 32 || begin[i] == '|' || begin[i] == 127) return false;
      memcpy(out, begin, length); out[length] = 0;
      return true;
    };
    if (!strcmp(p, "clear")) {
      state.custom[slot] = item;
      return true;
    }
    if (!strncmp(p, "gauge ", 6)) {
      float value;
      if (sscanf(p + 6, "%f %n", &value, &n) != 1 || !isfinite(value) || value < 0 || value > 100) return false;
      p += 6 + n;
      if (*p++ != '|' || !*p || !field(item.title, p, strlen(p))) return false;
      item.kind = CustomKind::Gauge; item.percent = value;
    } else if (!strncmp(p, "text |", 6)) {
      p += 6;
      const char* middle = strchr(p, '|');
      if (!middle || middle == p || !field(item.title, p, size_t(middle - p))) return false;
      const char* end = strchr(++middle, '|');
      if (!end || !field(item.left, middle, size_t(end - middle)) || !field(item.right, end + 1, strlen(end + 1))) return false;
      item.kind = CustomKind::Text;
    } else return false;
    state.custom[slot] = item;
    return true;
  }
  if (!strncmp(line, "pc ", 3)) {
    float cpu, mem, rx, tx;
    if (sscanf(line + 3, "%f %f %f %f %n", &cpu, &mem, &rx, &tx, &n) != 4 || line[3 + n])
      return false;
    if (!isfinite(cpu) || !isfinite(mem) || !isfinite(rx) || !isfinite(tx) || cpu < 0 ||
        cpu > 100 || mem < 0 || mem > 100 || rx < 0 || tx < 0 || rx > 1e9 || tx > 1e9)
      return false;
    state.computer = {true, now, cpu, mem, rx, tx};
    return true;
  }
  if (!strncmp(line, "clock ", 6)) {
    uint32_t h, m, s;
    const char* p = line + 6;
    if (!readUnsigned(p, 23, h) || !readUnsigned(p, 59, m) || !readUnsigned(p, 59, s)) return false;
    while (isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p) return false;
    state.clockValid = true;
    state.clockSeconds = h * 3600 + m * 60 + s;
    state.clockAt = now;
    return true;
  }
  if (!strncmp(line, "agent ", 6)) {
    char app[12] = {}, status[16] = {};
    uint32_t elapsed;
    if (sscanf(line + 6, "%11s %15s %n", app, status, &n) != 2) return false;
    const char* title = line + 6 + n;
    if (!readUnsigned(title, 604800, elapsed)) return false;
    while (isspace(static_cast<unsigned char>(*title))) ++title;
    const int index = !strcmp(app, "claude") ? 0 : !strcmp(app, "codex") ? 1 : -1;
    AgentStatus parsed;
    if (index < 0 || !parseAgentStatus(status, parsed)) return false;
    if (*title && *title != '|') return false;
    if (*title == '|') ++title;
    Agent a;
    a.status = parsed;
    a.received = true;
    a.at = now;
    a.elapsedSeconds = uint32_t(elapsed);
    snprintf(a.title, sizeof(a.title), "%s", title);
    state.agents[index] = a;
    return true;
  }
  return false;
}
}  // namespace Dashboard
