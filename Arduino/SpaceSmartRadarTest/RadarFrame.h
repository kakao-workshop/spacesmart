#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Hi-Link LD2410C data frames only. No commands are sent to the module.
struct RadarSample {
  uint8_t type = 0, state = 0, movingEnergy = 0, stationaryEnergy = 0;
  uint16_t movingCm = 0, stationaryCm = 0, detectionCm = 0;
};

class RadarFrame {
 public:
  RadarSample sample;
  uint32_t frames = 0, invalid = 0, states[4] = {};
  uint8_t lastFrame[64] = {};
  size_t lastSize = 0;

  void reset() { *this = RadarFrame(); }
  bool push(uint8_t value) {
    if (used == sizeof(buffer)) drop();
    buffer[used++] = value;
    const uint8_t header[] = {0xF4, 0xF3, 0xF2, 0xF1};
    const uint8_t footer[] = {0xF8, 0xF7, 0xF6, 0xF5};
    while (used) {
      const size_t prefix = used < 4 ? used : 4;
      if (memcmp(buffer, header, prefix)) { drop(); continue; }
      if (used < 6) return false;
      const uint16_t body = little16(buffer + 4);
      if (body < 13 || body > sizeof(buffer) - 10) {
        ++invalid; drop(); continue;
      }
      const size_t total = body + 10;
      if (used < total) return false;
      const bool typeOk = (buffer[6] == 2 && body == 13) ||
                          (buffer[6] == 1 && body >= 33);
      if (!typeOk || buffer[7] != 0xAA || buffer[8] > 3 ||
          buffer[body + 4] != 0x55 || buffer[body + 5] != 0 ||
          memcmp(buffer + total - 4, footer, 4)) {
        ++invalid; drop(); continue;
      }
      sample.type = buffer[6]; sample.state = buffer[8];
      sample.movingCm = little16(buffer + 9); sample.movingEnergy = buffer[11];
      sample.stationaryCm = little16(buffer + 12); sample.stationaryEnergy = buffer[14];
      sample.detectionCm = little16(buffer + 15);
      ++frames; ++states[sample.state];
      memcpy(lastFrame, buffer, total); lastSize = total;
      used -= total;
      memmove(buffer, buffer + total, used);
      return true;
    }
    return false;
  }
 private:
  uint8_t buffer[64] = {};
  size_t used = 0;
  static uint16_t little16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
  void drop() { --used; memmove(buffer, buffer + 1, used); }
};
