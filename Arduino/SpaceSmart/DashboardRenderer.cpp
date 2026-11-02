#include "DashboardRenderer.h"

#include <float.h>

namespace Dashboard {
namespace {
const char* PAGE_NAMES[] = {"실내 환경",   "환경 그래프", "컴퓨터", "커스텀", "AI 작업",
                            "집중 타이머", "시계",        "기기 진단", "설정", "조명"};
const char* METRICS[] = {"온도", "습도", "CO2", "조도"};
const char* UNITS[] = {"°C", "%", "ppm", "lx"};
const char* SPANS[] = {"30분", "6시간", "24시간"};
uint32_t agentColor(const Theme& t, AgentStatus status) {
  switch (status) {
    case AgentStatus::Busy:
      return t.running;
    case AgentStatus::Waiting:
      return t.warning;
    case AgentStatus::Complete:
      return t.good;
    case AgentStatus::Error:
      return t.error;
    default:
      return t.muted;
  }
}
void right(Canvas& c, int x, int y, const char* txt, int size, uint16_t color) {
  c.text(x - c.measure(txt, size), y, txt, size, color);
}
void center(Canvas& c, int y, const char* txt, int size, uint16_t color) {
  c.text((c.width - c.measure(txt, size)) / 2, y, txt, size, color);
}
void formatValue(char* dst, size_t cap, int metric, float value, bool valid) {
  if (!valid)
    snprintf(dst, cap, "--");
  else if (metric == 0)
    snprintf(dst, cap, "%.1f", value);
  else if (metric == 3 && value >= 10000)
    snprintf(dst, cap, "%.1fk", value / 1000);
  else
    snprintf(dst, cap, "%.0f", value);
}
const char* metricStatus(const State& s, int m, uint32_t& color, const Theme& t) {
  if (!s.sensors.valid[m]) {
    color = t.muted;
    return m == 3 && s.sensors.saturated ? "포화" : "수신 대기";
  }
  const float v = s.sensors.value[m];
  color = t.good;
  if (m == 0) {
    if (v < 20) {
      color = t.running;
      return "서늘";
    }
    if (v > 26) {
      color = t.warning;
      return "더움";
    }
    return "쾌적";
  }
  if (m == 1) {
    if (v < 40) {
      color = t.warning;
      return "건조";
    }
    if (v > 60) {
      color = t.warning;
      return "다습";
    }
    return "적정";
  }
  if (m == 2) {
    if (s.co2Level == 2) {
      color = t.error;
      return "환기 필요";
    }
    if (s.co2Level == 1) {
      color = t.warning;
      return "환기 권장";
    }
    return "목표 안";
  }
  if (v < 300) {
    color = t.running;
    return "어두움";
  }
  if (v > 500) {
    color = t.warning;
    return "밝음";
  }
  return "적정";
}
void footer(Canvas& c, const State& s, const Theme& t, const char* hint = nullptr) {
  c.line(12, c.height - 27, c.width - 13, c.height - 27, rgb565(t.line));
  const char* message = hint        ? hint
                        : !s.detail ? "회전: 화면  ·  누름: 선택"
                                    : "K0: 뒤로  ·  길게: 홈";
  c.text(12, c.height - 21, message, 12, rgb565(t.muted), c.width - 24);
}
void header(Canvas& c, const State& s, const Theme& t) {
  c.text(12, 10, s.page == Page::LedTest ? "LED 시험" : s.page == Page::LightSetup ? "조명 설정" : PAGE_NAMES[uint8_t(s.page)], 14, rgb565(t.text));
  char buf[24];
  if (s.clockValid) {
    const uint32_t seconds = s.localSeconds();
    snprintf(buf, sizeof(buf), "%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds / 60) % 60));
  } else
    snprintf(buf, sizeof(buf), "%s", s.bleConnected ? "BLE 연결" : "PC 대기");
  if (s.page != Page::Clock) right(c, c.width - 12, 11, buf, 12, rgb565(t.muted));
  c.rect(12, 33, 20, 2, rgb565(t.accent));
  c.line(36, 34, c.width - 13, 34, rgb565(t.line));
}
void environment(Canvas& c, const State& s, const Theme& t) {
  const bool portrait = c.height > c.width;
  const int w = (c.width - 32) / 2, h = (c.height - 84) / 2;
  for (int m = 0; m < 4; ++m) {
    const int x = 12 + (m % 2) * (w + 8), y = 44 + (m / 2) * (h + 8);
    uint32_t statusColor;
    const char* status = metricStatus(s, m, statusColor, t);
    c.roundRect(x, y, w, h, 7, rgb565(t.surface));
    c.text(x + 10, y + 8, METRICS[m], 14, rgb565(t.muted));
    if (!portrait) right(c, x + w - 10, y + 9, status, 12, rgb565(statusColor));
    char value[24];
    formatValue(value, sizeof(value), m, s.sensors.value[m], s.sensors.valid[m]);
    int size = 28;
    if (c.measure(value, size) > w - 20) size = 20;
    const int valueY = y + (portrait ? 36 : 28);
    c.text(x + 10, valueY, value, size, rgb565(t.text), w - 20);
    if (portrait) {
      c.text(x + 10, y + h - 49, status, 12, rgb565(statusColor), w - 20);
      right(c, x + w - 10, y + h - 32, UNITS[m], 12, rgb565(t.muted));
    } else {
      int unitX = x + 12 + c.measure(value, size);
      if (unitX + c.measure(UNITS[m], 12) < x + w - 8)
        c.text(unitX, valueY + 12, UNITS[m], 12, rgb565(t.muted));
    }
    const int trackX = x + 10, trackW = w - 20, trackY = y + h - 12;
    c.roundRect(trackX, trackY, trackW, 4, 2, rgb565(t.line));
    if (s.sensors.valid[m]) {
      const float mins[] = {16, 20, 400, 0}, maxs[] = {32, 80, 2000, 1000},
                  goodMin[] = {20, 40, 400, 300}, goodMax[] = {26, 60, float(s.co2Yellow), 500};
      auto at = [&](float v) {
        return int(std::max(0.f, std::min(1.f, (v - mins[m]) / (maxs[m] - mins[m]))) *
                   (trackW - 1));
      };
      c.rect(trackX + at(goodMin[m]), trackY, std::max(1, at(goodMax[m]) - at(goodMin[m])), 4,
             rgb565(t.good));
      c.rect(trackX + at(s.sensors.value[m]) - 1, trackY - 3, 3, 10, rgb565(t.text));
    }
  }
  const char* presence = !s.sensors.radarValid ? "재실 미확인"
                         : s.sensors.present   ? "재실 중"
                                               : "자리 비움";
  char hint[96];
  snprintf(hint, sizeof(hint), "%s  ·  %s", presence, s.detail ? "K0: 뒤로" : "회전: 화면");
  footer(c, s, t, hint);
}
void graph(Canvas& c, const State& s, const Theme& t, const History& history) {
  const int m = s.metric;
  char value[24];
  formatValue(value, sizeof(value), m, s.sensors.value[m], s.sensors.valid[m]);
  c.text(12, 44, METRICS[m], 14, rgb565(t.muted));
  right(c, c.width - 12, 45, SPANS[s.span], 12, rgb565(t.accent));
  c.text(12, 65, value, 40, rgb565(t.text));
  int ux = 16 + c.measure(value, 40);
  if (ux + c.measure(UNITS[m], 12) < c.width - 12) c.text(ux, 88, UNITS[m], 12, rgb565(t.muted));
  const uint32_t window = s.span == 0 ? 1800000UL : s.span == 1 ? 21600000UL : 86400000UL;
  const uint32_t gap = s.span == 0 ? 16000 : 91000;
  const size_t count = s.span == 0 ? history.recent.count : history.day.count;
  auto sample = [&](size_t i) -> const Sample& {
    return s.span == 0 ? history.recent.at(i) : history.day.at(i);
  };
  float low = FLT_MAX, high = -FLT_MAX;
  size_t valid = 0;
  for (size_t i = 0; i < count; ++i) {
    const Sample& p = sample(i);
    if (uint32_t(s.now - p.at) <= window && (p.valid & (1u << m))) {
      low = std::min(low, p.value[m]);
      high = std::max(high, p.value[m]);
      ++valid;
    }
  }
  const int left = 42, rightX = c.width - 14, top = 121, bottom = c.height - 53;
  if (!valid) {
    center(c, (top + bottom) / 2 - 7, "측정 기록을 모으고 있어요", 14, rgb565(t.muted));
    footer(c, s, t, s.detail ? "회전: 센서  ·  누름: 기간" : "누름: 그래프 탐색");
    return;
  }
  if (m == 2) {
    low = std::min(low, float(s.co2Yellow));
    high = std::max(high, float(s.co2Yellow));
  }
  const float padding = std::max((high - low) * .15f, m == 0 ? 1.f : m == 1 ? 5.f : 50.f);
  low -= padding;
  high += padding;
  if (m != 0) low = std::max(0.f, low);
  auto yy = [&](float v) { return bottom - int((v - low) / (high - low) * (bottom - top)); };
  c.line(left, top, left, bottom, rgb565(t.line));
  c.line(left, bottom, rightX, bottom, rgb565(t.line));
  char tick[24];
  snprintf(tick, sizeof(tick), m == 0 ? "%.1f" : "%.0f", high);
  right(c, left - 5, top - 6, tick, 12, rgb565(t.muted));
  snprintf(tick, sizeof(tick), m == 0 ? "%.1f" : "%.0f", low);
  right(c, left - 5, bottom - 8, tick, 12, rgb565(t.muted));
  if (m == 2) {
    int y = yy(float(s.co2Yellow));
    for (int x = left; x < rightX; x += 7) c.line(x, y, std::min(x + 3, rightX), y, rgb565(t.line));
  }
  bool previous = false;
  int px = 0, py = 0;
  uint32_t previousAt = 0;
  for (size_t i = 0; i < count; ++i) {
    const Sample& p = sample(i);
    const uint32_t age = s.now - p.at;
    if (age > window) continue;
    if (!(p.valid & (1u << m))) {
      previous = false;
      continue;
    }
    const int x = rightX - int(double(age) / window * (rightX - left)), y = yy(p.value[m]);
    if (previous && uint32_t(p.at - previousAt) <= gap) {
      c.line(px, py, x, y, rgb565(t.accent));
      c.line(px, py + 1, x, y + 1, rgb565(t.accent));
    } else {
      c.rect(x - 1, y - 1, 3, 3, rgb565(t.accent));
    }
    previous = true;
    px = x;
    py = y;
    previousAt = p.at;
  }
  char start[32];
  snprintf(start, sizeof(start), "%s 전", SPANS[s.span]);
  c.text(left, bottom + 7, start, 12, rgb565(t.muted));
  right(c, rightX, bottom + 7, "지금", 12, rgb565(t.muted));
  footer(c, s, t, s.detail ? "회전: 센서  ·  누름: 기간" : "누름: 그래프 탐색");
}
void computer(Canvas& c, const State& s, const Theme& t) {
  if (!fresh(s.computer.received, s.computer.at, s.now, PC_TTL)) {
    center(c, c.height / 2 - 28, "PC 연결 대기", 20, rgb565(t.text));
    center(c, c.height / 2 + 6, "연결 프로그램을 실행해 주세요", 14, rgb565(t.muted));
    center(c, c.height / 2 + 30,
           s.bleConnected ? "BLE 연결됨 · 데이터 대기" : "BLE 또는 USB로 연결", 12,
           rgb565(t.accent));
    footer(c, s, t);
    return;
  }
  const int rowH = c.height > c.width ? 70 : 49;
  float measures[] = {s.computer.cpu, s.computer.memory};
  const char* labels[] = {"CPU 사용률", "메모리 사용률"};
  for (int i = 0; i < 2; ++i) {
    int y = 44 + i * (rowH + 7);
    c.roundRect(12, y, c.width - 24, rowH, 7, rgb565(t.surface));
    c.text(23, y + 8, labels[i], 14, rgb565(t.muted));
    char number[20];
    snprintf(number, sizeof(number), "%.0f%%", measures[i]);
    right(c, c.width - 23, y + 4, number, 28, rgb565(t.text));
    int trackY = y + rowH - 14;
    c.roundRect(23, trackY, c.width - 46, 5, 2, rgb565(t.line));
    c.rect(23, trackY, int((c.width - 46) * measures[i] / 100), 5,
           rgb565(measures[i] >= 90 ? t.warning : t.accent));
  }
  const int y = c.height - 84;
  c.roundRect(12, y, c.width - 24, 48, 7, rgb565(t.surface));
  c.text(22, y + 6, "네트워크 · 초당 전송", 12, rgb565(t.muted));
  char rx[40], tx[40];
  auto rate = [](char* out, size_t capacity, const char* label, float kib) {
    const char* unit = "KiB";
    if (kib >= 1048576) {
      kib /= 1048576;
      unit = "GiB";
    } else if (kib >= 1024) {
      kib /= 1024;
      unit = "MiB";
    }
    snprintf(out, capacity, "%s %.*f%s", label, kib < 100 ? 1 : 0, kib, unit);
  };
  rate(rx, sizeof(rx), "수신", s.computer.rx);
  rate(tx, sizeof(tx), "송신", s.computer.tx);
  c.text(22, y + 24, rx, 14, rgb565(t.text), (c.width - 44) / 2);
  right(c, c.width - 22, y + 24, tx, 14, rgb565(t.text));
  footer(c, s, t);
}
void agents(Canvas& c, const State& s, const Theme& t) {
  const int h = (c.height - 84) / 2;
  for (int i = 0; i < 2; ++i) {
    const Agent& a = s.agents[i];
    const AgentStatus state = a.visibleStatus(s.now);
    int y = 44 + i * (h + 8);
    c.roundRect(12, y, c.width - 24, h, 7, rgb565(t.surface));
    c.text(24, y + 9, i ? "Codex" : "Claude", 14, rgb565(t.text));
    right(c, c.width - 24, y + 10, statusName(state), 12, rgb565(agentColor(t, state)));
    const bool known = state != AgentStatus::Unknown && state != AgentStatus::Offline;
    c.text(24, y + 34,
           known && a.title[0] ? a.title
           : known             ? "작업 이름 없음"
                               : "상태 이벤트 수신 대기",
           14, rgb565(t.muted), c.width - 48);
    char elapsed[48];
    const uint32_t seconds = a.elapsed(s.now);
    if (known)
      snprintf(elapsed, sizeof(elapsed), "경과 %02lu:%02lu",
               static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
    else
      snprintf(elapsed, sizeof(elapsed), "%s",
               s.bleConnected ? "PC 연결됨" : "PC 연결을 확인해 주세요");
    c.text(24, y + h - 22, elapsed, 12, rgb565(t.muted), c.width - 48);
  }
  footer(c, s, t);
}
void focus(Canvas& c, const State& s, const Theme& t) {
  const auto& f = s.focus;
  char label[40];
  snprintf(label, sizeof(label), "집중 %u분", f.minutes);
  center(c, 49, label, 14, rgb565(t.muted));
  const uint32_t seconds = (f.remainingMs + 999) / 1000;
  char timer[24];
  snprintf(timer, sizeof(timer), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60),
           static_cast<unsigned long>(seconds % 60));
  center(c, c.height / 2 - 35, timer, 48, rgb565(t.text));
  center(c, c.height / 2 + 24,
         f.finished                                     ? "집중 완료 · 잠시 쉬세요"
         : f.running                                    ? "집중 중"
         : f.remainingMs == uint32_t(f.minutes) * 60000 ? "시작할 준비가 됐어요"
                                                        : "일시 정지",
         14, rgb565(f.finished ? t.good : t.accent));
  const int y = c.height - 65, w = c.width - 40;
  c.roundRect(20, y, w, 5, 2, rgb565(t.line));
  const float fraction = 1 - float(f.remainingMs) / (uint32_t(f.minutes) * 60000);
  c.rect(20, y, int(w * fraction), 5, rgb565(t.accent));
  footer(c, s, t, s.detail ? "누름: 시작/정지  ·  K0: 뒤로" : "누름: 타이머 선택");
}
void clockPage(Canvas& c, const State& s, const Theme& t) {
  const bool portrait = c.height > c.width;
  char time[16] = "--:--";
  if (s.clockValid) {
    const uint32_t seconds = s.localSeconds();
    snprintf(time, sizeof(time), "%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>((seconds / 60) % 60));
  }
  center(c, portrait ? 52 : 35, time, portrait ? 72 : 96, rgb565(t.text));
  const char* labels[] = {"온도 °C", "습도 %", "CO2 ppm"};
  for (int i = 0; i < 3; ++i) {
    const int w = portrait ? c.width - 24 : (c.width - 40) / 3;
    const int h = portrait ? 39 : 66;
    const int x = portrait ? 12 : 12 + i * (w + 8);
    const int y = portrait ? c.height - 161 + i * 43 : c.height - 99;
    c.roundRect(x, y, w, h, 7, rgb565(t.surface));
    c.text(x + 9, y + (portrait ? 12 : 6), labels[i], 12, rgb565(t.muted));
    char value[24];
    formatValue(value, sizeof(value), i, s.sensors.value[i], s.sensors.valid[i]);
    uint32_t color = t.text;
    if (i == 2 && s.sensors.valid[i]) color = s.co2Level == 2 ? t.error : s.co2Level == 1 ? t.warning : t.good;
    const int size = c.measure(value, 28) > (portrait ? w - 90 : w - 12) ? 20 : 28;
    if (portrait) right(c, x + w - 9, y + 3, value, size, rgb565(color));
    else c.text(x + (w - c.measure(value, size)) / 2, y + 27, value, size, rgb565(color));
  }
  footer(c, s, t);
}
void customPage(Canvas& c, const State& s, const Theme& t) {
  const int rowH = (c.height - 92) / 3;
  for (int i = 0; i < 3; ++i) {
    const auto& item = s.custom[i];
    const int y = 44 + i * (rowH + 7);
    c.roundRect(12, y, c.width - 24, rowH, 7, rgb565(t.surface));
    if (!item.valid(s.now)) continue;
    c.text(23, y + 7, item.title, 14, rgb565(t.muted), c.width - 126);
    if (item.kind == CustomKind::Gauge) {
      char value[20]; snprintf(value, sizeof(value), "%.0f%%", item.percent);
      right(c, c.width - 23, y + 3, value, 28, rgb565(t.text));
      const int trackY = y + rowH - 13;
      c.roundRect(23, trackY, c.width - 46, 5, 2, rgb565(t.line));
      c.rect(23, trackY, int((c.width - 46) * item.percent / 100), 5, rgb565(item.percent >= 90 ? t.warning : t.accent));
    } else {
      const int half = (c.width - 52) / 2;
      c.text(23, y + rowH - 22, item.left, 14, rgb565(t.text), half);
      const int start = std::max(c.width - 23 - c.measure(item.right, 14), c.width - 23 - half);
      c.text(start, y + rowH - 22, item.right, 14, rgb565(t.text), half);
    }
  }
  footer(c, s, t);
}
void ledTestPage(Canvas& c, const State& s, const Theme& t) {
  const char* names[] = {"자동 순환", "빨강", "초록", "파랑", "흰색", "한 개씩"};
  center(c, 44, "본체 8개 · 신호등 1개", 14, rgb565(t.muted));
  const int columns = c.width > c.height ? 3 : 2;
  const int w = (c.width - 24 - (columns - 1) * 6) / columns;
  for (int i = 0; i < 6; ++i) {
    int x = 12 + (i % columns) * (w + 6), y = 75 + (i / columns) * 43;
    c.roundRect(x, y, w, 36, 6, rgb565(s.ledTest.selected == i ? t.raised : t.surface));
    c.text(x + (w - c.measure(names[i], 14)) / 2, y + 10, names[i], 14, rgb565(s.ledTest.selected == i ? t.accent : t.text));
  }
  char status[80];
  if (s.ledTest.active) {
    const unsigned remaining = (15000 - std::min(uint32_t(15000), uint32_t(s.now - s.ledTest.startedAt)) + 999) / 1000;
    snprintf(status, sizeof(status), "%s · %u초 후 복원", names[s.ledTest.runningMode], remaining);
  } else snprintf(status, sizeof(status), "누름: 시작 · 육안으로 점등 확인");
  center(c, c.height - 72, status, 12, rgb565(s.ledTest.active ? t.accent : t.text));
  center(c, c.height - 51, "시험 밝기 20 · K0 즉시 종료", 12, rgb565(t.muted));
  footer(c, s, t, s.ledTest.active ? "누름: 중지  ·  K0: 뒤로" : "회전: 선택  ·  누름: 시험");
}
void diagnostics(Canvas& c, const State& s, const Theme& t) {
  const char* labels[] = {"SCD40",     "VEML7700",    "재실 레이더", "BLE",
                          "PC 데이터", "여유 메모리", "가동 시간"};
  char values[7][40];
  snprintf(values[0], 40, "%s",
           s.sensors.valid[2] ? "정상"
           : s.sensors.scd    ? "수신 대기"
                              : "미연결");
  snprintf(values[1], 40, "%s",
           s.sensors.valid[3]    ? "정상"
           : s.sensors.saturated ? "포화"
           : s.sensors.light     ? "수신 대기"
                                 : "미연결");
  snprintf(values[2], 40, "%s", s.sensors.radarValid ? "정상" : "수신 대기");
  snprintf(values[3], 40, "%s", !s.bleEnabled ? "꺼짐" : s.bleConnected ? "연결됨" : "연결 대기");
  snprintf(values[4], 40, "%s",
           fresh(s.computer.received, s.computer.at, s.now, PC_TTL) ? "정상" : "수신 대기");
  snprintf(values[5], 40, "%lu KiB", static_cast<unsigned long>(s.freeHeap / 1024));
  snprintf(values[6], 40, "%lu분", static_cast<unsigned long>(s.now / 60000));
  int rowH = (c.height - 78) / 7;
  for (int i = 0; i < 7; ++i) {
    int y = 45 + i * rowH;
    c.text(15, y, labels[i], 14, rgb565(t.muted));
    right(c, c.width - 15, y, values[i], 14, rgb565(t.text));
    if (i < 6) c.line(15, y + rowH - 4, c.width - 15, y + rowH - 4, rgb565(t.surface));
  }
  footer(c, s, t);
}
void lightPage(Canvas& c,const State& s,const Theme& t) {
  const auto& lamp=s.light; char buf[80];
  if(s.page==Page::LightSetup) {
    snprintf(buf,sizeof(buf),"ID %06lX · %s",(unsigned long)lamp.id,lamp.radio?"무선 준비":"nRF24 미연결");
    c.text(14,43,buf,12,rgb565(t.muted),c.width-28);
    const char* names[]={lamp.enabled?"조명 제어 끄기":"조명 제어 켜기","리모컨 ID 등록","조명과 새로 연결","현재 켜짐으로 맞춤","현재 꺼짐으로 맞춤",lamp.verified?"실물 확인 취소":"실물 동작 확인 완료"};
    const int first=s.lightSetupItem<4?0:2;
    const int h=(c.height-113)/4;
    for(int j=0;j<4;j++) { int i=first+j,y=64+j*(h+2); bool active=s.lightSetupItem==i;
      c.roundRect(12,y,c.width-24,h,5,rgb565(active?t.raised:t.surface));
      c.text(22,y+5,names[i],14,rgb565(active?t.accent:t.text),c.width-44);
    }
    footer(c,s,t,s.lightPairConfirm?"리모컨 연결 변경 · 다시 누르면 실행":lamp.learning?"60초 내 리모컨을 두 번 조작":"회전: 항목 · 누름: 실행 · K0: 뒤로");
    return;
  }
  snprintf(buf,sizeof(buf),"%s · %s",lamp.enabled?"제어 켜짐":"제어 꺼짐",lamp.verified?"상태는 추정":"실물 확인 전");
  c.text(14,43,buf,12,rgb565(t.muted),c.width-28);
  const char* names[]={"전원","밝기","색온도","기본 조명","조명 설정"};
  char vals[5][48];
  snprintf(vals[0],48,"%s",lamp.state.power<0?"미확인 · 전환":lamp.state.power?"켜짐 · 전환":"꺼짐 · 전환");
  if(lamp.state.brightness<0)snprintf(vals[1],48,"-- / 15"); else snprintf(vals[1],48,"%d / 15",lamp.state.brightness);
  if(lamp.state.temperature<0)snprintf(vals[2],48,"-- K"); else snprintf(vals[2],48,"~%d K",2700+lamp.state.temperature*3800/15);
  snprintf(vals[3],48,"50%% · 2700 K"); snprintf(vals[4],48,"등록 / 상태 맞춤");
  int h=(c.height-110)/5;
  for(int i=0;i<5;i++){int y=62+i*(h+2);bool sel=s.detail&&s.lightItem==i;
    c.roundRect(12,y,c.width-24,h,5,rgb565(sel?t.raised:t.surface));
    c.text(22,y+4,names[i],12,rgb565(sel?t.accent:t.muted));
    right(c,c.width-22,y+3,vals[i],14,rgb565(sel&&s.editing?t.accent:t.text));
  }
  footer(c,s,t,!s.detail?"누름: 조명 선택":s.editing?"회전: 조절 · 누름: 완료":lamp.busy?"무선 전송 중":s.light.message);
}
void settings(Canvas& c, const State& s, const Theme& t) {
  const char* names[] = {"테마", "화면 방향", "LED 밝기", "집중 시간", "LED 시험"};
  char values[5][80];
  snprintf(values[0], 80, "%s", t.name);
  snprintf(values[1], 80, "%s", s.rotation % 2 ? "가로 320×240" : "세로 240×320");
  snprintf(values[2], 80, "%u / 64", s.brightness);
  snprintf(values[3], 80, "%u분", s.focus.minutes);
  snprintf(values[4], 80, "누르면 열기");
  const bool portrait = c.height > c.width;
  const int h = (c.height - 82) / 5;
  for (int i = 0; i < 5; ++i) {
    const int y = 44 + i * (h + 2);
    bool selected = s.detail && s.setting == i;
    c.roundRect(12, y, c.width - 24, h, 5, rgb565(selected ? t.raised : t.surface));
    c.text(22, y + (portrait ? 5 : 8), names[i], 12, rgb565(selected ? t.accent : t.muted));
    const int limit = portrait ? c.width - 44 : c.width - 120;
    const int start = std::max(c.width - 22 - c.measure(values[i], 14), c.width - 22 - limit);
    c.text(start, y + (portrait ? h - 21 : 7), values[i], 14, rgb565(t.text), limit);
    if (selected && s.editing) c.rect(12, y + 5, 3, h - 10, rgb565(t.accent));
  }
  footer(c, s, t,
         !s.detail   ? "누름: 설정 선택"
         : s.editing ? "회전: 값 변경  ·  누름: 확정"
                     : "회전: 항목  ·  누름: 편집");
}
}  // namespace
void render(Canvas& c, const State& s, const History& history) {
  const Theme& t = THEMES[s.theme % THEME_COUNT];
  c.clear(rgb565(t.background));
  header(c, s, t);
  switch (s.page) {
    case Page::Environment:
      environment(c, s, t);
      break;
    case Page::Graph:
      graph(c, s, t, history);
      break;
    case Page::Computer:
      computer(c, s, t);
      break;
    case Page::Custom:
      customPage(c, s, t);
      break;
    case Page::Light:
    case Page::LightSetup:
      lightPage(c,s,t);
      break;
    case Page::LedTest:
      ledTestPage(c, s, t);
      break;
    case Page::Agents:
      agents(c, s, t);
      break;
    case Page::Focus:
      focus(c, s, t);
      break;
    case Page::Clock:
      clockPage(c, s, t);
      break;
    case Page::Diagnostics:
      diagnostics(c, s, t);
      break;
    case Page::Settings:
      settings(c, s, t);
      break;
    default:
      break;
  }
}
}  // namespace Dashboard
