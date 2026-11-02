#pragma once
#include <stdint.h>

namespace Dashboard {
constexpr uint8_t THEME_COUNT = 4;
constexpr uint16_t rgb565(uint32_t c) {
  return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
}
struct Theme {
  const char *id, *name;
  uint32_t background, surface, raised, text, muted, line, accent;
  uint32_t good, warning, error, running;
};
// Semantic colors stay consistent across themes. Accent is for selection and graphs.
constexpr Theme THEMES[] = {
    {"mint", "소프트 민트", 0x0B0D10, 0x171B21, 0x232A32, 0xF3F4F6, 0xA6AEB8, 0x35414D, 0x6ED6B5,
     0x6ED6B5, 0xF1BD68, 0xF18484, 0x82B7FF},
    {"blue", "차콜 블루", 0x0A0E14, 0x151D28, 0x233246, 0xEEF3FA, 0xA7B5C8, 0x35465D, 0x82B7FF,
     0x6ED6B5, 0xF1BD68, 0xF18484, 0x82B7FF},
    {"amber", "웜 앰버", 0x100E0B, 0x201B14, 0x30291F, 0xF5F0E8, 0xB9AE9D, 0x4B4031, 0xE7BF86,
     0x6ED6B5, 0xF1BD68, 0xF18484, 0x82B7FF},
    {"violet", "미드나이트 바이올렛", 0x100D16, 0x1D1727, 0x30253F, 0xF2EEF8, 0xB5A8C6, 0x463751,
     0xBAA1EA, 0x6ED6B5, 0xF1BD68, 0xF18484, 0x82B7FF}};
}  // namespace Dashboard
