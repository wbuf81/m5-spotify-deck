#include "GameBoyMode.h"

#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {

// The handheld itself, drawn on the left, with the cover inside its LCD.
//
// The first version tinted the whole screen in four DMG greens. It was
// recognisable but flat — no object, no depth, and the album lost all its
// colour. Drawing the device gives it somewhere to live, and the art keeps the
// chunky low-resolution grid while staying in colour, which is closer to a Game
// Boy Color than a DMG.
constexpr uint32_t C_BODY = 0xC6C9B6;
constexpr uint32_t C_BODY_HI = 0xDFE2CE;
constexpr uint32_t C_BODY_LO = 0x9A9D8C;
constexpr uint32_t C_BEZEL = 0x4A4E45;
constexpr uint32_t C_BEZEL_HI = 0x6E7366;
constexpr uint32_t C_DPAD = 0x3A3A3C;
constexpr uint32_t C_BTN = 0x8E2A50;
constexpr uint32_t C_TEXT = 0x53566B;

constexpr int BODY_X = 8, BODY_Y = 4, BODY_W = 144, BODY_H = 232;
constexpr int BEZEL_X = 18, BEZEL_Y = 18, BEZEL_W = 124, BEZEL_H = 108;
constexpr int LCD_X = 28, LCD_Y = 28, LCD_W = 104, LCD_H = 88;

// Chunky on purpose: 26x22 cells is close to the original grid, which is the
// part of the Game Boy look worth keeping.
constexpr int CELL = 4;
constexpr int COLS = LCD_W / CELL;  // 26
constexpr int ROWS = LCD_H / CELL;  // 22

constexpr int TEXT_X = 162;

uint16_t col(uint32_t rgb) {
  const float f = theme::dimFactor();
  return M5.Display.color565(static_cast<uint8_t>(((rgb >> 16) & 0xFF) * f),
                             static_cast<uint8_t>(((rgb >> 8) & 0xFF) * f),
                             static_cast<uint8_t>((rgb & 0xFF) * f));
}

// Four bits per channel keeps a retro palette without throwing the album's
// colour away entirely.
uint16_t posterize(uint16_t c) {
  const int r = (((c >> 11) & 0x1F) << 3) & 0xF0;
  const int g = (((c >> 5) & 0x3F) << 2) & 0xF0;
  const int b = ((c & 0x1F) << 3) & 0xF0;
  return M5.Display.color565(r | (r >> 4), g | (g >> 4), b | (b >> 4));
}

void drawDevice() {
  // Body, with a lit top-left edge and a shadowed bottom-right so it reads as
  // an object rather than a rectangle.
  M5.Display.fillRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, 10, col(C_BODY));
  M5.Display.drawRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, 10, col(C_BODY_LO));
  M5.Display.drawFastHLine(BODY_X + 3, BODY_Y + 1, BODY_W - 6, col(C_BODY_HI));
  M5.Display.drawFastVLine(BODY_X + 1, BODY_Y + 3, BODY_H - 6, col(C_BODY_HI));
  // The DMG's lower-right corner is famously rounded much harder than the rest.
  M5.Display.fillRect(BODY_X + BODY_W - 26, BODY_Y + BODY_H - 26, 26, 26,
                      theme::pal.bg);
  M5.Display.fillCircle(BODY_X + BODY_W - 26, BODY_Y + BODY_H - 26, 25,
                        col(C_BODY));

  // Screen bezel.
  M5.Display.fillRoundRect(BEZEL_X, BEZEL_Y, BEZEL_W, BEZEL_H, 5, col(C_BEZEL));
  M5.Display.drawFastHLine(BEZEL_X + 2, BEZEL_Y + 1, BEZEL_W - 4, col(C_BEZEL_HI));

  // The two indicator stripes above the LCD.
  M5.Display.drawFastHLine(BEZEL_X + 8, BEZEL_Y + 6, BEZEL_W - 16, col(0x8E2A50));
  M5.Display.drawFastHLine(BEZEL_X + 8, BEZEL_Y + 9, BEZEL_W - 16, col(0x2A3A8E));

  // Power lamp.
  M5.Display.fillCircle(BEZEL_X + 6, BEZEL_Y + BEZEL_H / 2, 3, col(0xC03028));

  // Brand text under the bezel.
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(col(C_TEXT), col(C_BODY));
  M5.Display.setCursor(BEZEL_X + 26, BEZEL_Y + BEZEL_H + 6);
  M5.Display.print("GAME BOY");

  // D-pad.
  const int dx = BODY_X + 34, dy = BODY_Y + 168;
  M5.Display.fillRect(dx - 16, dy - 5, 32, 11, col(C_DPAD));
  M5.Display.fillRect(dx - 5, dy - 16, 11, 32, col(C_DPAD));

  // A and B, sitting on the diagonal the real thing uses.
  M5.Display.fillCircle(BODY_X + 118, BODY_Y + 160, 9, col(C_BTN));
  M5.Display.fillCircle(BODY_X + 96, BODY_Y + 172, 9, col(C_BTN));
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(col(C_TEXT), col(C_BODY));
  M5.Display.setCursor(BODY_X + 114, BODY_Y + 172);
  M5.Display.print("A");
  M5.Display.setCursor(BODY_X + 92, BODY_Y + 184);
  M5.Display.print("B");

  // Start / Select.
  for (int i = 0; i < 2; ++i) {
    M5.Display.fillRoundRect(BODY_X + 46 + i * 26, BODY_Y + 200, 20, 6, 3,
                             col(C_DPAD));
  }

  // Speaker grille, angled like the original.
  for (int i = 0; i < 6; ++i) {
    M5.Display.drawLine(BODY_X + 96 + i * 7, BODY_Y + 224,
                        BODY_X + 108 + i * 7, BODY_Y + 208, col(C_BODY_LO));
  }
}

}  // namespace

void GameBoyMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);
  drawDevice();

  // Cover inside the LCD, chunky but in colour.
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  if (src.createSprite(COLS, COLS)) {
    src.fillSprite(0);
    if (drawArtInto(&src, ctx.art_path, 0, 0, COLS)) {
      for (int y = 0; y < ROWS; ++y) {
        const int sy = y + ((COLS - ROWS) / 2);  // centre-crop the square cover
        for (int x = 0; x < COLS; ++x) {
          M5.Display.fillRect(LCD_X + x * CELL, LCD_Y + y * CELL, CELL, CELL,
                              posterize(src.readPixel(x, sy)));
        }
      }
    } else {
      M5.Display.fillRect(LCD_X, LCD_Y, LCD_W, LCD_H, col(0x9BBC0F));
    }
    src.deleteSprite();
  }

  // Track details beside the device.
  M5.Display.setFont(theme::fontTitle());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[3][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 150, lines, 3);
  int y = 30;
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(TEXT_X, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }

  y += 6;
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  const int an = wrapText(st.pb.artist, 150, lines, 2);
  for (int i = 0; i < an; ++i) {
    M5.Display.setCursor(TEXT_X, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }

  last_sec_ = -1;
}

void GameBoyMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t) {
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  if (sec == last_sec_) return;
  last_sec_ = sec;

  constexpr int BX = TEXT_X, BW = 146, BY = 196;
  int filled = 0;
  if (st.pb.duration_ms > 0) {
    filled = static_cast<int>((static_cast<uint64_t>(BW - 4) * st.pb.progress_ms) /
                              st.pb.duration_ms);
  }
  M5.Display.drawRect(BX, BY, BW, 10, theme::pal.bar_bg);
  M5.Display.fillRect(BX + 2, BY + 2, filled, 6, ctx.tint);
  M5.Display.fillRect(BX + 2 + filled, BY + 2, BW - 4 - filled, 6,
                      theme::pal.bg);

  char buf[16], pad[16];
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(pad, sizeof(pad), "%-7s", buf);
  M5.Display.setCursor(TEXT_X, BY + 16);
  M5.Display.print(pad);
  formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
  std::snprintf(pad, sizeof(pad), "%7s", buf);
  M5.Display.setCursor(320 - 8 - (7 * 6), BY + 16);
  M5.Display.print(pad);
}
