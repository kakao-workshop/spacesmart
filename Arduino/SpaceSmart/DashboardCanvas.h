#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>

namespace Dashboard {
struct Glyph {
  uint32_t code, offset;
  uint8_t width, height, advance;
  int8_t left, top;
};
struct Font {
  const Glyph* glyphs;
  const uint8_t* bits;
  size_t count;
  uint8_t height;
};
uint32_t decode(const char*& s);
const Font& fontFor(int size);
struct Canvas {
  uint16_t* pixels;
  int width, height;
  void pixel(int x, int y, uint16_t c) {
    if (x >= 0 && x < width && y >= 0 && y < height) pixels[y * width + x] = c;
  }
  void rect(int x, int y, int w, int h, uint16_t c) {
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(width, x + w),
        y1 = std::min(height, y + h);
    for (int yy = y0; yy < y1; ++yy)
      for (int xx = x0; xx < x1; ++xx) pixels[yy * width + xx] = c;
  }
  void clear(uint16_t c) { rect(0, 0, width, height, c); }
  void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
        err = dx + dy;
    for (;;) {
      pixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e = 2 * err;
      if (e >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
  void roundRect(int x, int y, int w, int h, int r, uint16_t c) {
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    for (int yy = 0; yy < r; ++yy)
      for (int xx = 0; xx < r; ++xx)
        if ((r - 1 - xx) * (r - 1 - xx) + (r - 1 - yy) * (r - 1 - yy) <= r * r) {
          pixel(x + xx, y + yy, c);
          pixel(x + w - 1 - xx, y + yy, c);
          pixel(x + xx, y + h - 1 - yy, c);
          pixel(x + w - 1 - xx, y + h - 1 - yy, c);
        }
  }
  const Glyph* glyph(uint32_t code, const Font& f) const {
    size_t lo = 0, hi = f.count;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (f.glyphs[mid].code < code)
        lo = mid + 1;
      else
        hi = mid;
    }
    if (lo < f.count && f.glyphs[lo].code == code) return f.glyphs + lo;
    if (code != '?') return glyph('?', f);
    return nullptr;
  }
  int measure(const char* text, int size) const {
    const Font& f = fontFor(size);
    int length = 0;
    while (*text) {
      const Glyph* g = glyph(decode(text), f);
      if (g) length += g->advance;
    }
    return length;
  }
  void text(int x, int y, const char* text, int size, uint16_t color, int limit = -1) {
    const Font& f = fontFor(size);
    int right = limit < 0 ? width : std::min(width, x + limit);
    const bool shortened = limit >= 0 && measure(text, size) > right - x;
    if (shortened) right -= measure("...", size);
    while (*text) {
      const Glyph* g = glyph(decode(text), f);
      if (!g) continue;
      if (x + g->advance > right) break;
      for (int yy = 0; yy < g->height; ++yy)
        for (int xx = 0; xx < g->width; ++xx) {
          size_t bit = yy * g->width + xx;
          if (f.bits[g->offset + bit / 8] & (0x80 >> (bit % 8)))
            pixel(x + g->left + xx, y + g->top + yy, color);
        }
      x += g->advance;
    }
    if (shortened) this->text(x, y, "...", size, color);
  }
};
}  // namespace Dashboard
