#include "PixelAlbumMode.h"

#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../Crt.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {
// 40x30 cells of 8px fills 320x240 exactly.
constexpr int COLS = 40, ROWS = 30, CELL = 8;
constexpr int PANEL_Y = 176;

// 4x4 Bayer, so quantisation breaks into a pattern instead of banding.
constexpr int BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// RGB332: 3 bits red, 3 green, 2 blue — the authentic 8-bit palette. Straight
// quantisation of a gradient sky produced hard horizontal stripes that read as
// a glitch rather than as pixel art, so each cell is nudged by its dither
// threshold first. Blue gets the largest nudge because it has the fewest bits.
uint16_t posterize(uint16_t c, int cx, int cy) {
  const int t = BAYER[cy & 3][cx & 3] - 8;
  const int r = clamp8((((c >> 11) & 0x1F) << 3) + t * 3) & 0xE0;
  const int g = clamp8((((c >> 5) & 0x3F) << 2) + t * 3) & 0xE0;
  const int b = clamp8(((c & 0x1F) << 3) + t * 7) & 0xC0;
  return M5.Display.color565(r | (r >> 3), g | (g >> 3), b | (b >> 2));
}
}  // namespace

void PixelAlbumMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);

  // Decode once at cell resolution; the panel gets blocks, not a scaled image.
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  const bool ok = src.createSprite(COLS, COLS);
  if (ok) {
    src.fillSprite(theme::pal.bg);
    drawArtInto(&src, ctx.art_path, 0, 0, COLS);
    for (int y = 0; y < ROWS; ++y) {
      // Centre-crop the square cover into a 4:3 screen rather than stretching.
      const int sy = y + ((COLS - ROWS) / 2);
      for (int x = 0; x < COLS; ++x) {
        M5.Display.fillRect(x * CELL, y * CELL, CELL, CELL,
                            posterize(src.readPixel(x, sy), x, y));
      }
    }
    src.deleteSprite();
  }

  // Text sits on a hard-edged band; drop shadows and gradients would fight the
  // 8-bit look.
  M5.Display.fillRect(0, PANEL_Y, 320, 240 - PANEL_Y, theme::pal.bg);
  M5.Display.drawFastHLine(0, PANEL_Y, 320, ctx.tint);

  M5.Display.setFont(theme::fontTitle());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[1][WRAP_MAX_LINE];
  // Narrower than the screen: the timecodes live at x=220.
  wrapText(st.pb.title, 200, lines, 1);
  M5.Display.setCursor(10, PANEL_Y + 8);
  M5.Display.print(lines[0]);

  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 200, lines, 1);
  M5.Display.setCursor(10, PANEL_Y + 30);
  M5.Display.print(lines[0]);

  // No scanlines over the artwork: the 8px cells are already the texture, and
  // the two together read as interference.
  last_sec_ = -1;
}

void PixelAlbumMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t) {
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  if (sec == last_sec_) return;
  last_sec_ = sec;

  // Blocky progress, in the same cell grid as the artwork.
  constexpr int CELLS = 40, CW = 8, BY = 230;
  int filled = 0;
  if (st.pb.duration_ms > 0) {
    filled = static_cast<int>((static_cast<uint64_t>(CELLS) * st.pb.progress_ms) /
                              st.pb.duration_ms);
  }
  for (int i = 0; i < CELLS; ++i) {
    M5.Display.fillRect(i * CW, BY, CW - 1, 6,
                        i < filled ? ctx.tint : theme::pal.bar_bg);
  }

  char buf[16];
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  char pad[16];
  std::snprintf(pad, sizeof(pad), "%-7s", buf);
  M5.Display.setCursor(220, PANEL_Y + 8);
  M5.Display.print(pad);
  formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
  std::snprintf(pad, sizeof(pad), "%7s", buf);
  M5.Display.setCursor(220, PANEL_Y + 30);
  M5.Display.print(pad);
}
