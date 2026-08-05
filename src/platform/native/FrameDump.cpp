// Emulator-only: dump the panel framebuffer to a 24-bit BMP.
//
// Screen-capturing the SDL window means hunting for its position and fighting
// Retina scaling. Reading the framebuffer back gives an exact 320x240 image
// instead, which is also what makes visual regression checks possible later.

#if defined(EMULATOR)

#include "FrameDump.h"

#include <M5Unified.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

void put16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(x & 0xff);
  v.push_back((x >> 8) & 0xff);
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x & 0xff);
  v.push_back((x >> 8) & 0xff);
  v.push_back((x >> 16) & 0xff);
  v.push_back((x >> 24) & 0xff);
}

}  // namespace

bool dumpFrameBmp(const char *path) {
  const int w = M5.Display.width();
  const int h = M5.Display.height();

  std::vector<uint16_t> px(static_cast<size_t>(w) * h, 0);
  M5.Display.readRect(0, 0, w, h, px.data());

  // 320*3 = 960 bytes per row, already 4-byte aligned, so no row padding.
  const int row_bytes = w * 3;
  const int pad = (4 - (row_bytes % 4)) % 4;
  const uint32_t image_bytes = static_cast<uint32_t>((row_bytes + pad) * h);

  std::vector<uint8_t> out;
  out.reserve(54 + image_bytes);

  // BITMAPFILEHEADER
  out.push_back('B');
  out.push_back('M');
  put32(out, 54 + image_bytes);
  put32(out, 0);
  put32(out, 54);

  // BITMAPINFOHEADER
  put32(out, 40);
  put32(out, static_cast<uint32_t>(w));
  put32(out, static_cast<uint32_t>(h));
  put16(out, 1);
  put16(out, 24);
  put32(out, 0);
  put32(out, image_bytes);
  put32(out, 2835);
  put32(out, 2835);
  put32(out, 0);
  put32(out, 0);

  // BMP rows run bottom-up, pixels are BGR.
  //
  // readRect hands back RGB565 with the bytes swapped (verified: writing
  // TFT_RED 0xF800 reads back 0x00F8), because LovyanGFX stores 16-bit colour
  // in the byte order the panel bus wants. Swap before unpacking channels.
  for (int y = h - 1; y >= 0; --y) {
    for (int x = 0; x < w; ++x) {
      const uint16_t raw = px[static_cast<size_t>(y) * w + x];
      const uint16_t c = static_cast<uint16_t>((raw >> 8) | (raw << 8));
      const uint8_t r5 = (c >> 11) & 0x1f;
      const uint8_t g6 = (c >> 5) & 0x3f;
      const uint8_t b5 = c & 0x1f;
      out.push_back(static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
      out.push_back(static_cast<uint8_t>((g6 << 2) | (g6 >> 4)));
      out.push_back(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)));
    }
    for (int p = 0; p < pad; ++p) out.push_back(0);
  }

  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  const size_t n = std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  return n == out.size();
}

#endif  // EMULATOR
