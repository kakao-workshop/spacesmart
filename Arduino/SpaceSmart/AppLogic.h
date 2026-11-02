#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Logic {
enum class Traffic : uint8_t { Unknown, Green, Yellow, Red, Off };
enum class Ai : uint8_t { Idle, Thinking, Done, Error };

// Unsigned subtraction also works across the millis() rollover.
inline bool fresh(bool valid, uint32_t last, uint32_t now, uint32_t maxAge) {
  return valid && static_cast<uint32_t>(now - last) < maxAge;
}

inline Traffic co2Traffic(uint16_t ppm, bool valid, Traffic previous,
                          uint16_t yellow, uint16_t red, uint16_t hysteresis) {
  if (!valid || ppm == 0) return Traffic::Unknown;
  if (ppm >= red || (previous == Traffic::Red && ppm >= red - hysteresis))
    return Traffic::Red;
  if (ppm >= yellow || ((previous == Traffic::Yellow || previous == Traffic::Red)
                       && ppm >= yellow - hysteresis)) return Traffic::Yellow;
  return Traffic::Green;
}

class DebouncedButton {
 public:
  void begin(bool pressed, uint32_t now) {
    raw_ = stable_ = pressed;
    changed_ = now;
  }
  bool pressed(bool value, uint32_t now) {
    if (value != raw_) { raw_ = value; changed_ = now; }
    if (value != stable_ && static_cast<uint32_t>(now - changed_) >= 30) {
      stable_ = value;
      return stable_;
    }
    return false;
  }
 private:
  bool raw_ = false, stable_ = false;
  uint32_t changed_ = 0;
};

// Bounded serial lines; an overlong command is discarded through its newline.
class LineBuffer {
 public:
  enum class Result { Pending, Ready, Overflow };
  Result push(char c) {
    if (c == '\r') c = '\n';
    if (c == '\n') {
      if (overflow_) { reset(); return Result::Overflow; }
      if (!size_) return Result::Pending;
      line_[size_] = '\0';
      size_ = 0;
      return Result::Ready;
    }
    if (overflow_) return Result::Pending;
    if (size_ >= sizeof(line_) - 1) { overflow_ = true; return Result::Pending; }
    line_[size_++] = c;
    return Result::Pending;
  }
  char* data() { return line_; }
 private:
  void reset() { size_ = 0; overflow_ = false; }
  char line_[96] = {};
  size_t size_ = 0;
  bool overflow_ = false;
};

inline bool parseBrightness(const char* text, uint8_t maximum, uint8_t& output) {
  if (!text || !*text) return false;
  unsigned value = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + static_cast<unsigned>(*p - '0');
    if (value > maximum) return false;
  }
  output = static_cast<uint8_t>(value);
  return true;
}
} // namespace Logic
