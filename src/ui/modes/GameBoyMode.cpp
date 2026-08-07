#include "GameBoyMode.h"

#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../TextWrap.h"
#include "../Theme.h"

namespace {

// The four DMG greens, darkest to lightest. Nothing else appears on this
// view above the strip — that IS the filter. Named DMG0..3 rather than DMG0..3
// because the ESP32 Arduino core claims DMG0..G39 as GPIO aliases.
constexpr uint32_t DMG0 = 0x0F380F;
constexpr uint32_t DMG1 = 0x306230;
constexpr uint32_t DMG2 = 0x8BAC0F;
constexpr uint32_t DMG3 = 0x9BBC0F;

// Cover: 88x88 dither cells drawn as 2px blocks = 176px, matching the
// classic layout's art size. 2px cells put the whole view at the Game Boy's
// horizontal density (160 cells across 320).
constexpr int ART_CELLS = 88, CELL = 2;
constexpr int ART_PX = 8, ART_PY = 8;

// Dialog box, Pokemon-style, right of the art.
constexpr int BOX_X = 196, BOX_Y = 8, BOX_W = 116, BOX_H = 110;

constexpr uint32_t BOOT_MS = 700;

uint16_t col(uint32_t rgb) {
  const float f = theme::dimFactor();
  return M5.Display.color565(static_cast<uint8_t>(((rgb >> 16) & 0xFF) * f),
                             static_cast<uint8_t>(((rgb >> 8) & 0xFF) * f),
                             static_cast<uint8_t>((rgb & 0xFF) * f));
}

// 4x4 Bayer matrix, scaled to slice luminance into the four greens. This is
// the exact trick GB software used: the LCD had four shades, and anything
// smoother was an ordered-dither illusion.
constexpr int BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint16_t ditherGreen(uint16_t c565, int x, int y) {
  const int r = ((c565 >> 11) & 0x1F) << 3;
  const int g = ((c565 >> 5) & 0x3F) << 2;
  const int b = (c565 & 0x1F) << 3;
  // Luminance 0..255, nudged by the Bayer cell, sliced into 4 bands.
  int lum = (r * 30 + g * 59 + b * 11) / 100;
  lum += (BAYER[y & 3][x & 3] - 8) * 4;
  if (lum < 64) return col(DMG0);
  if (lum < 128) return col(DMG1);
  if (lum < 192) return col(DMG2);
  return col(DMG3);
}

// Double-line border, the RPG dialog look: dark outer line, light gap, dark
// inner line, rounded by skipping the corner pixels.
void dialogBox(int x, int y, int w, int h) {
  M5.Display.fillRect(x, y, w, h, col(DMG3));
  M5.Display.drawRoundRect(x, y, w, h, 3, col(DMG0));
  M5.Display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 3, col(DMG0));
  M5.Display.drawRoundRect(x + 4, y + 4, w - 8, h - 8, 2, col(DMG1));
}

}  // namespace

void GameBoyMode::drawMain(const AppState &st, const ViewCtx &ctx) {
  using namespace theme;
  M5.Display.fillRect(0, 0, SCREEN_W, STRIP_Y, col(DMG3));

  // The cover, dithered into the four greens. Decoded small, drawn as 2px
  // cells row by row — one blit per row, not one per cell.
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  bool art_ok = false;
  if (src.createSprite(ART_CELLS, ART_CELLS)) {
    src.fillSprite(0);
    art_ok = drawArtInto(&src, ctx.art_path, 0, 0, ART_CELLS);
    if (art_ok) {
      M5Canvas strip(&M5.Display);
      strip.setColorDepth(16);
      const bool strips = strip.createSprite(ART_CELLS * CELL, CELL);
      M5.Display.startWrite();
      for (int y = 0; y < ART_CELLS; ++y) {
        for (int x = 0; x < ART_CELLS; ++x) {
          const uint16_t c = ditherGreen(src.readPixel(x, y), x, y);
          if (strips) {
            strip.fillRect(x * CELL, 0, CELL, CELL, c);
          } else {
            M5.Display.fillRect(ART_PX + x * CELL, ART_PY + y * CELL, CELL,
                                CELL, c);
          }
        }
        if (strips) strip.pushSprite(ART_PX, ART_PY + y * CELL);
      }
      M5.Display.endWrite();
      if (strips) strip.deleteSprite();
    }
    src.deleteSprite();
  }
  if (!art_ok) {
    // No cover: a dithered gradient placeholder keeps the screen honest.
    for (int y = 0; y < ART_CELLS; ++y) {
      for (int x = 0; x < ART_CELLS; ++x) {
        const int lum = 40 + (y * 180) / ART_CELLS;
        M5.Display.fillRect(ART_PX + x * CELL, ART_PY + y * CELL, CELL, CELL,
                            ditherGreen(M5.Display.color565(lum, lum, lum), x, y));
      }
    }
  }
  // Art frame: single dark line, like a window tile border.
  M5.Display.drawRect(ART_PX - 2, ART_PY - 2, ART_CELLS * CELL + 4,
                      ART_CELLS * CELL + 4, col(DMG0));

  // Track info in the dialog box.
  dialogBox(BOX_X, BOX_Y, BOX_W, BOX_H);
  M5.Display.setFont(fontArtist());
  M5.Display.setTextColor(col(DMG0), col(DMG3));
  char lines[3][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, BOX_W - 20, lines, 3);
  int y = BOX_Y + 10;
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(BOX_X + 10, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }
  y += 4;
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(col(DMG1), col(DMG3));
  const int an = wrapText(st.pb.artist, BOX_W - 20, lines, 2);
  for (int i = 0; i < an; ++i) {
    M5.Display.setCursor(BOX_X + 10, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }

  // Below the box: the RPG continue-marker, and a cartridge code because
  // every GB screen had one somewhere.
  M5.Display.fillTriangle(BOX_X + BOX_W - 16, BOX_Y + BOX_H - 10,
                          BOX_X + BOX_W - 8, BOX_Y + BOX_H - 10,
                          BOX_X + BOX_W - 12, BOX_Y + BOX_H - 5, col(DMG0));
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(col(DMG1), col(DMG3));
  M5.Display.setCursor(BOX_X + 2, STRIP_Y - 16);
  M5.Display.print("DMG-05-SPT");
}

void GameBoyMode::enter(const AppState &, const ViewCtx &) {
  // Power-on: pale screen, then the boot bar drops in tick(). The main screen
  // is drawn when the ritual finishes.
  M5.Display.fillRect(0, 0, theme::SCREEN_W, theme::STRIP_Y, col(DMG3));
  start_ms_ = 0;
  phase_ = 0;
  last_bar_y_ = -1;
}

void GameBoyMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  if (phase_ >= 1) return;
  if (start_ms_ == 0) start_ms_ = now_ms;
  const uint32_t t = now_ms - start_ms_;

  if (t < BOOT_MS) {
    // The logo-drop: a dark bar with a notch sliding from the top to centre.
    const float f = static_cast<float>(t) / BOOT_MS;
    const int target = theme::STRIP_Y / 2 - 8;
    const int by = static_cast<int>(target * f);
    if (by != last_bar_y_) {
      if (last_bar_y_ >= 0) {
        M5.Display.fillRect(60, last_bar_y_, 200, 16, col(DMG3));
      }
      M5.Display.fillRect(60, by, 200, 16, col(DMG0));
      M5.Display.fillRect(156, by + 5, 8, 6, col(DMG3));
      last_bar_y_ = by;
    }
    return;
  }

  phase_ = 1;
  drawMain(st, ctx);
}
