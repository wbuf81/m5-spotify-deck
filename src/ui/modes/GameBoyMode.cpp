#include "GameBoyMode.h"

#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {
// The DMG panel's four shades, darkest first.
constexpr uint32_t DMG[4] = {0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F};

// Square grid: the cover is square, and sampling it into a wide grid stretched
// it noticeably.
constexpr int COLS = 27, ROWS = 27, CELL = 6;
constexpr int ART_X = (320 - (COLS * CELL)) / 2, ART_Y = 6;

// 4x4 Bayer. Ordered dithering, not error diffusion: it tiles predictably,
// which is what makes it look like a handheld rather than like noise.
constexpr int BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint16_t dmg(int i) {
  const float f = theme::dimFactor();
  const uint32_t c = DMG[i];
  return M5.Display.color565(static_cast<uint8_t>(((c >> 16) & 0xFF) * f),
                             static_cast<uint8_t>(((c >> 8) & 0xFF) * f),
                             static_cast<uint8_t>((c & 0xFF) * f));
}
}  // namespace

void GameBoyMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(dmg(3));

  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  if (src.createSprite(COLS, ROWS)) {
    src.fillSprite(0);
    drawArtInto(&src, ctx.art_path, 0, 0, COLS);
    for (int y = 0; y < ROWS; ++y) {
      for (int x = 0; x < COLS; ++x) {
        const uint16_t c = src.readPixel(x, y);
        const int lum = ((((c >> 11) & 0x1F) << 3) * 30 +
                         (((c >> 5) & 0x3F) << 2) * 59 +
                         ((c & 0x1F) << 3) * 11) / 100;
        // Nudge by the dither threshold before quantising, so flat regions
        // break into patterns instead of banding.
        const int biased = lum + (BAYER[y & 3][x & 3] - 8) * 6;
        int shade = biased * 4 / 256;
        if (shade < 0) shade = 0;
        if (shade > 3) shade = 3;
        M5.Display.fillRect(ART_X + x * CELL, ART_Y + y * CELL, CELL, CELL,
                            dmg(shade));
      }
    }
    src.deleteSprite();
  }

  M5.Display.fillRect(0, 172, 320, 68, dmg(3));
  M5.Display.setFont(theme::fontTitle());
  M5.Display.setTextColor(dmg(0), dmg(3));
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 300, lines, 1);
  M5.Display.setCursor(10, 178);
  M5.Display.print(lines[0]);

  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(dmg(1), dmg(3));
  wrapText(st.pb.artist, 300, lines, 1);
  M5.Display.setCursor(10, 200);
  M5.Display.print(lines[0]);

  last_sec_ = -1;
}

void GameBoyMode::tick(const AppState &st, const ViewCtx &, uint32_t) {
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  if (sec == last_sec_) return;
  last_sec_ = sec;

  constexpr int BX = 10, BW = 220, BY = 224;
  int filled = 0;
  if (st.pb.duration_ms > 0) {
    filled = static_cast<int>((static_cast<uint64_t>(BW - 4) * st.pb.progress_ms) /
                              st.pb.duration_ms);
  }
  M5.Display.drawRect(BX, BY, BW, 10, dmg(0));
  M5.Display.fillRect(BX + 2, BY + 2, filled, 6, dmg(1));
  M5.Display.fillRect(BX + 2 + filled, BY + 2, BW - 4 - filled, 6, dmg(3));

  char buf[16], pad[16];
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(dmg(0), dmg(3));
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(pad, sizeof(pad), "%7s", buf);
  M5.Display.setCursor(320 - 10 - (7 * 6), BY + 1);
  M5.Display.print(pad);
}
