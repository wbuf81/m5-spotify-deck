#pragma once

// Layout geometry and palette, in one place.
//
// ART_SIZE drives every other coordinate, so the spec's 160/150 artwork
// fallbacks are a one-constant change rather than a redesign.

#include <M5Unified.h>

#include <cstdint>

namespace theme {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int MARGIN = 8;

constexpr int ART_SIZE = 176;
constexpr int ART_X = MARGIN;
constexpr int ART_Y = MARGIN;

constexpr int COL_X = ART_X + ART_SIZE + MARGIN;   // 192
constexpr int COL_Y = MARGIN;
constexpr int COL_W = SCREEN_W - COL_X - MARGIN;   // 120
constexpr int COL_H = ART_SIZE;

constexpr int STRIP_Y = ART_Y + ART_SIZE + MARGIN;  // 192
constexpr int STRIP_H = SCREEN_H - STRIP_Y;         // 48

constexpr int BAR_Y = 200;
constexpr int BAR_H = 3;
constexpr int BAR_X = MARGIN;
constexpr int BAR_W = SCREEN_W - (MARGIN * 2);
constexpr int TIME_Y = 212;

// The whole info row beneath the progress bar: timecodes, play glyph, toast.
// Clearing it must cover full glyph extents, not an assumed line height — a
// hardcoded 10px clear left the descenders of the taller JetBrains Mono face
// behind, which showed up as toast text ghosting under the timecodes.
constexpr int ROW_Y = BAR_Y + BAR_H + 3;
constexpr int ROW_H = SCREEN_H - ROW_Y;

// Brightness levels from the spec.
constexpr uint8_t BRIGHT_ACTIVE = 180;
constexpr uint8_t BRIGHT_IDLE = 60;
constexpr uint8_t BRIGHT_OFF = 0;

// Live palette. Recomputed by applyBrightness() so the emulator can show the
// dim/sleep behaviour, which a backlight call alone cannot demonstrate on a
// desktop window.
struct Palette {
  uint16_t bg;
  uint16_t strip;
  uint16_t text;
  uint16_t dim;
  uint16_t accent;
  uint16_t bar_bg;
  uint16_t warn;
};

extern Palette pal;

// Fonts must cover Latin-1 at minimum. The Adafruit GFX FreeSans faces are
// ASCII-only and render "Björk" as "Bj?rk", which is unacceptable for a device
// pointed at a real music library. The lgfxJapanGothic faces are misleadingly
// named — they carry the full Latin range plus CJK — and are proportional.
//
// Size, not weight, carries the hierarchy: these faces have no bold variant.
const lgfx::IFont *fontTitle();
const lgfx::IFont *fontArtist();
const lgfx::IFont *fontSmall();

// Sets the backlight on hardware and, in the emulator, scales the palette so
// dimming is actually visible on screen.
void applyBrightness(uint8_t brightness);

}  // namespace theme
