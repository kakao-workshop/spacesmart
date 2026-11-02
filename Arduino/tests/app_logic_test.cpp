#include "../SpaceSmart/AppLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

int main() {
  using namespace Logic;
  auto classify = [](uint16_t ppm, bool valid, Traffic previous) {
    return co2Traffic(ppm, valid, previous, 1000, 1500, 50);
  };
  // Missing/stale/zero data cannot produce a reassuring green signal.
  assert(classify(600, false, Traffic::Green) == Traffic::Unknown);
  assert(classify(0, true, Traffic::Red) == Traffic::Unknown);
  assert(classify(999, true, Traffic::Unknown) == Traffic::Green);
  assert(classify(1000, true, Traffic::Green) == Traffic::Yellow);
  assert(classify(1500, true, Traffic::Green) == Traffic::Red);
  assert(classify(1450, true, Traffic::Red) == Traffic::Red);
  assert(classify(1449, true, Traffic::Red) == Traffic::Yellow);
  assert(classify(950, true, Traffic::Yellow) == Traffic::Yellow);
  assert(classify(949, true, Traffic::Yellow) == Traffic::Green);
  assert(classify(600, true, Traffic::Red) == Traffic::Green);
  assert(!fresh(false, 10, 20, 100));
  assert(fresh(true, 10, 109, 100));
  assert(!fresh(true, 10, 110, 100));
  assert(fresh(true, UINT32_MAX - 19, 10, 31));
  assert(!fresh(true, UINT32_MAX - 19, 10, 30));

  DebouncedButton button;
  button.begin(false, 0);
  assert(!button.pressed(true, 1));
  assert(!button.pressed(false, 5));
  assert(!button.pressed(true, 10));
  assert(!button.pressed(true, 39));
  assert(button.pressed(true, 40));
  assert(!button.pressed(true, 900)); // held buttons never repeat
  assert(!button.pressed(false, 901));
  assert(!button.pressed(false, 931));
  assert(!button.pressed(true, 940));
  assert(button.pressed(true, 970));
  button.begin(true, 1000); // startup with held button must not trigger
  assert(!button.pressed(true, 2000));
  button.begin(false, UINT32_MAX - 10);
  assert(!button.pressed(true, UINT32_MAX - 9));
  assert(button.pressed(true, 20));

  uint8_t brightness = 20;
  assert(parseBrightness("0", 64, brightness) && brightness == 0);
  assert(parseBrightness("64", 64, brightness) && brightness == 64);
  for (const char* invalid : {"", "65", "-1", "12x", "1.5", "99999999999999999999"}) {
    assert(!parseBrightness(invalid, 64, brightness));
    assert(brightness == 64);
  }

  LineBuffer lines;
  for (const char c : {'s','t','a','t','u','s'})
    assert(lines.push(c) == LineBuffer::Result::Pending);
  assert(lines.push('\r') == LineBuffer::Result::Ready);
  assert(!strcmp(lines.data(), "status"));
  assert(lines.push('\n') == LineBuffer::Result::Pending);
  for (int i = 0; i < 100; ++i) lines.push('x');
  assert(lines.push('\n') == LineBuffer::Result::Overflow);
  lines.push('o'); lines.push('k');
  assert(lines.push('\n') == LineBuffer::Result::Ready);
  assert(!strcmp(lines.data(), "ok"));
  puts("PASS: thresholds/hysteresis, stale data, millis rollover, button debounce, serial bounds/ranges");
}
