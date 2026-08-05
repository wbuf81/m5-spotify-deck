#include "Theme.h"

namespace theme {

Palette pal{};

namespace {

// Base palette at full strength, as 24-bit values.
constexpr uint32_t BASE_BG = 0x101012;
constexpr uint32_t BASE_STRIP = 0x1C1C20;
constexpr uint32_t BASE_TEXT = 0xF2F2F4;
constexpr uint32_t BASE_DIM = 0x8C8C96;
constexpr uint32_t BASE_ACCENT = 0x1DB954;  // Spotify green
constexpr uint32_t BASE_BAR_BG = 0x3A3A42;
constexpr uint32_t BASE_WARN = 0xE0A030;

uint16_t scaled(uint32_t rgb888, float f) {
  const uint8_t r = static_cast<uint8_t>(((rgb888 >> 16) & 0xFF) * f);
  const uint8_t g = static_cast<uint8_t>(((rgb888 >> 8) & 0xFF) * f);
  const uint8_t b = static_cast<uint8_t>((rgb888 & 0xFF) * f);
  return M5.Display.color565(r, g, b);
}

}  // namespace

const lgfx::IFont *fontTitle() { return &fonts::lgfxJapanGothicP_20; }
const lgfx::IFont *fontArtist() { return &fonts::lgfxJapanGothicP_16; }
const lgfx::IFont *fontSmall() { return &fonts::Font0; }

void applyBrightness(uint8_t brightness) {
  // On hardware the backlight does the work, which also saves power. In the
  // emulator there is no backlight, so scale the palette instead to make the
  // behaviour observable.
  M5.Display.setBrightness(brightness);

#if defined(EMULATOR)
  const float f = static_cast<float>(brightness) / BRIGHT_ACTIVE;
#else
  const float f = 1.0f;
#endif

  pal.bg = scaled(BASE_BG, f);
  pal.strip = scaled(BASE_STRIP, f);
  pal.text = scaled(BASE_TEXT, f);
  pal.dim = scaled(BASE_DIM, f);
  pal.accent = scaled(BASE_ACCENT, f);
  pal.bar_bg = scaled(BASE_BAR_BG, f);
  pal.warn = scaled(BASE_WARN, f);
}

}  // namespace theme
