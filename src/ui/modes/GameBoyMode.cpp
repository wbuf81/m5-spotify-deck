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
constexpr uint32_t C_BODY_HI = 0xE9ECDA;
constexpr uint32_t C_BODY_LO = 0x9A9D8C;
constexpr uint32_t C_BODY_SHADOW = 0x767A6B;
constexpr uint32_t C_BEZEL = 0x4A4E45;
constexpr uint32_t C_BEZEL_HI = 0x6E7366;
constexpr uint32_t C_DPAD = 0x3A3A3C;
constexpr uint32_t C_DPAD_HI = 0x5A5A5E;
constexpr uint32_t C_BTN = 0x8E2A50;
constexpr uint32_t C_BTN_HI = 0xB44E74;
constexpr uint32_t C_BTN_LO = 0x5E1A36;
constexpr uint32_t C_TEXT = 0x53566B;

// The handheld ends above the shared StatusStrip (y192), so the whole shell
// shrank and its controls moved up compared to the full-height first version.
constexpr int BODY_X = 8, BODY_Y = 4, BODY_W = 144, BODY_H = 184;
constexpr int BEZEL_X = 18, BEZEL_Y = 18, BEZEL_W = 124, BEZEL_H = 108;
constexpr int LCD_X = 28, LCD_Y = 28, LCD_W = 104, LCD_H = 88;

// Chunky on purpose: 26x22 cells is close to the original grid, which is the
// part of the Game Boy look worth keeping.
constexpr int CELL = 4;
constexpr int COLS = LCD_W / CELL;  // 26
constexpr int ROWS = LCD_H / CELL;  // 22

constexpr int TEXT_X = 166;

// The cartridge. Sized so its label recess holds a 72px cover.
constexpr int CART_X = 180, CART_Y = 20, CART_W = 108, CART_H = 104;
constexpr int LABEL_X = CART_X + 14, LABEL_Y = CART_Y + 22, LABEL_S = 76;
constexpr uint32_t C_CART = 0x83868F;
constexpr uint32_t C_CART_HI = 0xA6A9B2;
constexpr uint32_t C_CART_LO = 0x5E616A;

// DMG screen shades for the boot ritual.
constexpr uint32_t C_LCD_PALE = 0x9BBC0F;
constexpr uint32_t C_LCD_DARK = 0x0F380F;

// Timeline: cart drops, then the boot bar slides, then the cover appears.
constexpr uint32_t T_CART_IN = 450;
constexpr uint32_t T_BOOT_END = 950;


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

// A convex button: shadow ring, face, and an off-centre highlight arc. Three
// circles are what separate "button" from "coloured dot".
void convexButton(int cx, int cy, int r) {
  M5.Display.fillCircle(cx + 1, cy + 1, r, col(C_BTN_LO));
  M5.Display.fillCircle(cx, cy, r - 1, col(C_BTN));
  M5.Display.fillCircle(cx - r / 3, cy - r / 3, r / 3, col(C_BTN_HI));
}

// Cart shell only — the label is pasted separately, once, because it costs a
// JPEG decode and the shell is redrawn every frame of the drop.
void drawCartShell(int y) {
  M5.Display.fillRoundRect(CART_X, y, CART_W, CART_H, 6, col(C_CART));
  M5.Display.drawRoundRect(CART_X, y, CART_W, CART_H, 6, col(C_CART_LO));
  M5.Display.drawFastHLine(CART_X + 3, y + 1, CART_W - 6, col(C_CART_HI));
  // Grip grooves along the top, like the real shell.
  for (int i = 0; i < 3; ++i) {
    M5.Display.drawFastHLine(CART_X + 14, y + 6 + i * 4, CART_W - 28,
                             col(C_CART_LO));
  }
  // Label recess.
  M5.Display.fillRect(LABEL_X - 2, y + 20, LABEL_S + 4, LABEL_S + 4,
                      col(C_CART_LO));
}

void drawDevice() {
  // Body. Two highlight edges top-left, two shadow edges bottom-right: the
  // double lines are what make the shell look moulded rather than printed.
  M5.Display.fillRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, 10, col(C_BODY));
  M5.Display.drawRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, 10, col(C_BODY_SHADOW));
  M5.Display.drawFastHLine(BODY_X + 3, BODY_Y + 1, BODY_W - 6, col(C_BODY_HI));
  M5.Display.drawFastHLine(BODY_X + 4, BODY_Y + 2, BODY_W - 8, col(C_BODY_HI));
  M5.Display.drawFastVLine(BODY_X + 1, BODY_Y + 3, BODY_H - 6, col(C_BODY_HI));
  M5.Display.drawFastVLine(BODY_X + 2, BODY_Y + 4, BODY_H - 8, col(C_BODY_HI));
  M5.Display.drawFastHLine(BODY_X + 4, BODY_Y + BODY_H - 2, BODY_W - 8,
                           col(C_BODY_LO));
  M5.Display.drawFastVLine(BODY_X + BODY_W - 2, BODY_Y + 4, BODY_H - 8,
                           col(C_BODY_LO));

  // The DMG's lower-right corner is famously rounded much harder than the rest.
  constexpr int CR = 18;  // scaled down with the shell
  M5.Display.fillRect(BODY_X + BODY_W - CR, BODY_Y + BODY_H - CR, CR, CR,
                      theme::pal.bg);
  M5.Display.fillCircle(BODY_X + BODY_W - CR, BODY_Y + BODY_H - CR, CR - 1,
                        col(C_BODY));
  // Carry the shadow edge around that corner or the outline just stops dead.
  M5.Display.drawCircle(BODY_X + BODY_W - CR, BODY_Y + BODY_H - CR, CR - 1,
                        col(C_BODY_LO));
  M5.Display.fillRect(BODY_X + BODY_W - CR, BODY_Y + BODY_H - CR * 2, CR + 1,
                      CR, col(C_BODY));
  M5.Display.fillRect(BODY_X + BODY_W - CR * 2, BODY_Y + BODY_H - CR, CR,
                      CR + 1, col(C_BODY));
  M5.Display.drawFastVLine(BODY_X + BODY_W - 2, BODY_Y + 4, BODY_H - CR - 4,
                           col(C_BODY_LO));
  M5.Display.drawFastHLine(BODY_X + 4, BODY_Y + BODY_H - 2, BODY_W - CR - 4,
                           col(C_BODY_LO));

  // Screen bezel, with a drop shadow under its lower edge so it sits INTO the
  // shell instead of on top of it.
  M5.Display.fillRoundRect(BEZEL_X, BEZEL_Y, BEZEL_W, BEZEL_H, 5, col(C_BEZEL));
  M5.Display.drawFastHLine(BEZEL_X + 2, BEZEL_Y + 1, BEZEL_W - 4, col(C_BEZEL_HI));
  M5.Display.drawFastHLine(BEZEL_X + 2, BEZEL_Y + BEZEL_H, BEZEL_W - 4,
                           col(C_BODY_SHADOW));
  M5.Display.drawFastHLine(BEZEL_X + 4, BEZEL_Y + BEZEL_H + 1, BEZEL_W - 8,
                           col(C_BODY_LO));

  // The two indicator stripes above the LCD.
  M5.Display.drawFastHLine(BEZEL_X + 8, BEZEL_Y + 6, BEZEL_W - 16, col(0x8E2A50));
  M5.Display.drawFastHLine(BEZEL_X + 8, BEZEL_Y + 9, BEZEL_W - 16, col(0x2A3A8E));

  // Power lamp, with a glow ring: a lit LED bleeds into the plastic around it.
  M5.Display.fillCircle(BEZEL_X + 6, BEZEL_Y + BEZEL_H / 2, 4, col(0x5A1A14));
  M5.Display.fillCircle(BEZEL_X + 6, BEZEL_Y + BEZEL_H / 2, 2, col(0xE84438));

  // Brand text under the bezel.
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(col(C_TEXT), col(C_BODY));
  M5.Display.setCursor(BEZEL_X + 26, BEZEL_Y + BEZEL_H + 6);
  M5.Display.print("GAME BOY");

  // D-pad: recessed well, cross, highlight on the top arm, centre dimple.
  // Compact, so the well clears the brand text above it in the squat shell.
  const int dx = BODY_X + 32, dy = BODY_Y + 156;
  M5.Display.fillCircle(dx, dy, 17, col(C_BODY_LO));
  M5.Display.fillRect(dx - 13, dy - 4, 26, 9, col(C_DPAD));
  M5.Display.fillRect(dx - 4, dy - 13, 9, 26, col(C_DPAD));
  M5.Display.drawFastHLine(dx - 3, dy - 13, 7, col(C_DPAD_HI));
  M5.Display.drawFastVLine(dx - 13, dy - 3, 7, col(C_DPAD_HI));
  M5.Display.fillCircle(dx, dy, 2, col(0x2E2E30));

  // A and B, sitting on the diagonal the real thing uses.
  // No A/B letters at this scale: two magenta buttons on the diagonal read as
  // A/B by themselves, and the labels collided with the speaker grille.
  convexButton(BODY_X + 118, BODY_Y + 146, 8);
  convexButton(BODY_X + 96, BODY_Y + 158, 8);

  // Start / Select: recessed slots with the pill inside, like the real ones.
  for (int i = 0; i < 2; ++i) {
    const int sx = BODY_X + 42 + i * 26, sy = BODY_Y + 174;
    M5.Display.fillRoundRect(sx - 1, sy - 1, 22, 8, 4, col(C_BODY_LO));
    M5.Display.fillRoundRect(sx, sy, 20, 6, 3, col(C_DPAD));
    M5.Display.drawFastHLine(sx + 3, sy + 1, 14, col(C_DPAD_HI));
  }

  // Speaker grille, angled like the original.
  for (int i = 0; i < 6; ++i) {
    M5.Display.drawLine(BODY_X + 84 + i * 6, BODY_Y + 178,
                        BODY_X + 94 + i * 6, BODY_Y + 164, col(C_BODY_LO));
    M5.Display.drawLine(BODY_X + 85 + i * 6, BODY_Y + 178,
                        BODY_X + 95 + i * 6, BODY_Y + 164, col(C_BODY_SHADOW));
  }
}


}  // namespace

void GameBoyMode::enter(const AppState &st, const ViewCtx &ctx) {
  (void)ctx;
  M5.Display.fillScreen(theme::pal.bg);
  drawDevice();

  // The LCD starts dark: nothing is inserted yet. The cover arrives at the
  // end of the boot ritual, from tick().
  M5.Display.fillRect(LCD_X, LCD_Y, LCD_W, LCD_H, col(C_LCD_DARK));

  // Track details under the cart's resting place.
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[2][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 148, lines, 2);
  int y = CART_Y + CART_H + 10;
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(TEXT_X, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 148, lines, 1);
  M5.Display.setCursor(TEXT_X, y + 2);
  M5.Display.print(lines[0]);

  start_ms_ = 0;
  phase_ = 0;
}

void GameBoyMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  (void)st;
  if (phase_ >= 2) return;
  if (start_ms_ == 0) {
    start_ms_ = now_ms;
    last_cart_y_ = -CART_H;
    last_bar_y_ = -1;
  }
  const uint32_t t = now_ms - start_ms_;

  if (phase_ == 0) {
    // Cart drop: eased so it lands rather than stops.
    float f = t >= T_CART_IN ? 1.0f : static_cast<float>(t) / T_CART_IN;
    f = 1.0f - (1.0f - f) * (1.0f - f);
    const int y = -CART_H + static_cast<int>((CART_Y + CART_H) * f);
    if (y != last_cart_y_) {
      // Erase only the sliver above the shell; the shell repaints the rest.
      if (y > last_cart_y_) {
        M5.Display.fillRect(CART_X, last_cart_y_, CART_W, y - last_cart_y_,
                            theme::pal.bg);
      }
      drawCartShell(y);
      last_cart_y_ = y;
    }
    if (t >= T_CART_IN) {
      phase_ = 1;
      drawCartShell(CART_Y);
      // Paste the label: the cover, once, plus a thin sticker border.
      if (!drawArt(ctx.art_path, LABEL_X, LABEL_Y, LABEL_S)) {
        M5.Display.fillRect(LABEL_X, LABEL_Y, LABEL_S, LABEL_S,
                            theme::pal.bar_bg);
      }
      M5.Display.drawRect(LABEL_X - 1, LABEL_Y - 1, LABEL_S + 2, LABEL_S + 2,
                          col(C_CART_HI));
      // Power on: the LCD goes pale, ready for the logo drop.
      M5.Display.fillRect(LCD_X, LCD_Y, LCD_W, LCD_H, col(C_LCD_PALE));
    }
    return;
  }

  // Phase 1: the boot bar slides down to centre, then the cover fades in.
  if (t < T_BOOT_END) {
    const float f = static_cast<float>(t - T_CART_IN) / (T_BOOT_END - T_CART_IN);
    const int target = LCD_Y + LCD_H / 2 - 5;
    const int by = LCD_Y + static_cast<int>((target - LCD_Y) * f);
    if (by != last_bar_y_) {
      if (last_bar_y_ >= 0) {
        M5.Display.fillRect(LCD_X + 10, last_bar_y_, LCD_W - 20, 10,
                            col(C_LCD_PALE));
      }
      // The "logo": a dark bar with a notch, abstract enough to be ours.
      M5.Display.fillRect(LCD_X + 10, by, LCD_W - 20, 10, col(C_LCD_DARK));
      M5.Display.fillRect(LCD_X + LCD_W / 2 - 3, by + 3, 6, 4, col(C_LCD_PALE));
      last_bar_y_ = by;
    }
    return;
  }

  // Boot done: the cover appears on the little screen, chunky and in colour.
  phase_ = 2;
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  if (src.createSprite(COLS, COLS)) {
    src.fillSprite(0);
    if (drawArtInto(&src, ctx.art_path, 0, 0, COLS)) {
      M5Canvas strip(&M5.Display);
      strip.setColorDepth(16);
      const bool strips = strip.createSprite(COLS * CELL, CELL);
      M5.Display.startWrite();
      for (int y = 0; y < ROWS; ++y) {
        const int sy = y + ((COLS - ROWS) / 2);  // centre-crop the square cover
        for (int x = 0; x < COLS; ++x) {
          const uint16_t c = posterize(src.readPixel(x, sy));
          if (strips) {
            strip.fillRect(x * CELL, 0, CELL, CELL, c);
          } else {
            M5.Display.fillRect(LCD_X + x * CELL, LCD_Y + y * CELL, CELL, CELL,
                                c);
          }
        }
        if (strips) strip.pushSprite(LCD_X, LCD_Y + y * CELL);
      }
      M5.Display.endWrite();
      if (strips) strip.deleteSprite();
    } else {
      M5.Display.fillRect(LCD_X, LCD_Y, LCD_W, LCD_H, col(C_LCD_PALE));
    }
    src.deleteSprite();
  }
}
