#include "NesMode.h"
#include "../../core/Hash.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../art/ArtRenderer.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../../core/Hash.h"

namespace {

// The 2C02's usable palette as RGB, the common de-facto conversion. Rows 0-3
// are the four brightness tiers; the blacks and mirrored greys are collapsed.
constexpr uint16_t NES_PAL_COUNT = 56;
constexpr uint32_t NES_PAL[NES_PAL_COUNT] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600,
    0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120,
    0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170,
    0xEA9E22, 0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5,
    0xF7D8A5, 0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3ECDC, 0x000000, 0x000000};

// HUD row geometry. The level (album art) sits centred beneath it.
constexpr int HUD_Y = 8;
constexpr int LEVEL_S = 128;
constexpr int LEVEL_X = (320 - LEVEL_S) / 2;
constexpr int LEVEL_Y = 42;
// Art cells: 64x64 samples drawn as 2px blocks, NES-chunky.
constexpr int ACELLS = 64, ACELL = 2;

uint16_t col(uint32_t rgb) {
  const float f = theme::dimFactor();
  return M5.Display.color565(static_cast<uint8_t>(((rgb >> 16) & 0xFF) * f),
                             static_cast<uint8_t>(((rgb >> 8) & 0xFF) * f),
                             static_cast<uint8_t>((rgb & 0xFF) * f));
}

// Nearest NES palette entry by squared RGB distance. 54 comparisons per
// sample, 4096 samples per cover: ~220k multiplies once per track, invisible
// against the JPEG decode that precedes it.
uint16_t nesQuantize(uint16_t c565) {
  const int r = ((c565 >> 11) & 0x1F) << 3;
  const int g = ((c565 >> 5) & 0x3F) << 2;
  const int b = (c565 & 0x1F) << 3;
  int best = 0;
  int32_t best_d = INT32_MAX;
  for (int i = 0; i < NES_PAL_COUNT; ++i) {
    const int pr = (NES_PAL[i] >> 16) & 0xFF;
    const int pg = (NES_PAL[i] >> 8) & 0xFF;
    const int pb = NES_PAL[i] & 0xFF;
    const int32_t d = (r - pr) * (r - pr) + (g - pg) * (g - pg) * 2 +
                      (b - pb) * (b - pb);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return col(NES_PAL[best]);
}

void hudLabel(int x, const char *tag) {
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  M5.Display.setCursor(x, HUD_Y);
  M5.Display.print(tag);
}

void hudValue(int x, const char *val) {
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  M5.Display.setCursor(x, HUD_Y + 14);
  M5.Display.print(val);
}

// Attract-mode Tetris geometry. Two lanes per side, clear of everything:
// HUD ends ~y36, the level spans x96..224, the title band starts y176.
constexpr int TET_TOP = 42;
constexpr int TET_BOTTOM = 170;
constexpr int TET_CELL = 7;
constexpr int TET_LANES[4] = {10, 52, 240, 282};  // lane x, two per side

// The seven tetrominoes as 4x2 cell masks (row-major, bit 3 = leftmost).
constexpr uint8_t TET_SHAPES[7][2] = {
    {0b1111, 0b0000},  // I
    {0b0110, 0b0110},  // O
    {0b1110, 0b0100},  // T
    {0b0110, 0b1100},  // S
    {0b1100, 0b0110},  // Z
    {0b1110, 0b1000},  // J
    {0b1110, 0b0010},  // L
};
// Tier-2 NES palette entries, one per shape, like the cartridge would.
constexpr uint32_t TET_COLORS[7] = {0x48CDDE, 0xEA9E22, 0xA01ACC, 0x0C9300,
                                    0xB53120, 0x155FD9, 0x994E00};

// The classic block: filled cell, dark outline, light glint pixel.
void drawTetCell(int x, int y, uint32_t rgb) {
  M5.Display.fillRect(x, y, TET_CELL, TET_CELL, col(rgb));
  M5.Display.drawRect(x, y, TET_CELL, TET_CELL, col(0x0F0F0F));
  M5.Display.fillRect(x + 1, y + 1, 2, 2, col(0xFFFEFF));
}

void drawTetPiece(int shape, int x, int y, uint32_t rgb) {
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (TET_SHAPES[shape][r] & (1 << (3 - c))) {
        drawTetCell(x + c * TET_CELL, y + r * TET_CELL, rgb);
      }
    }
  }
}

// A chunky 8px coin: gold circle, darker rim, slot.
void drawCoin(int x, int y) {
  M5.Display.fillCircle(x + 4, y + 4, 4, col(0xEA9E22));
  M5.Display.drawCircle(x + 4, y + 4, 4, col(0x994E00));
  M5.Display.drawFastVLine(x + 4, y + 2, 5, col(0x994E00));
}

}  // namespace

void NesMode::drawLevel(const ViewCtx &ctx) {
  M5Canvas src(&M5.Display);
  src.setColorDepth(16);
  if (!src.createSprite(ACELLS, ACELLS)) return;
  src.fillSprite(0);
  const bool ok = drawArtInto(&src, ctx.art_path, 0, 0, ACELLS);

  M5Canvas strip(&M5.Display);
  strip.setColorDepth(16);
  const bool strips = strip.createSprite(ACELLS * ACELL, ACELL);
  M5.Display.startWrite();
  for (int y = 0; y < ACELLS; ++y) {
    for (int x = 0; x < ACELLS; ++x) {
      const uint16_t c =
          ok ? nesQuantize(src.readPixel(x, y)) : theme::pal.bar_bg;
      if (strips) {
        strip.fillRect(x * ACELL, 0, ACELL, ACELL, c);
      } else {
        M5.Display.fillRect(LEVEL_X + x * ACELL, LEVEL_Y + y * ACELL, ACELL,
                            ACELL, c);
      }
    }
    if (strips) strip.pushSprite(LEVEL_X, LEVEL_Y + y * ACELL);
  }
  M5.Display.endWrite();
  if (strips) strip.deleteSprite();
  src.deleteSprite();

  // Level frame: the flat black border every NES playfield had.
  M5.Display.drawRect(LEVEL_X - 2, LEVEL_Y - 2, LEVEL_S + 4, LEVEL_S + 4,
                      col(0x4F4F4F));
}

void NesMode::enter(const AppState &st, const ViewCtx &ctx) {
  using namespace theme;
  M5.Display.fillRect(0, 0, SCREEN_W, STRIP_Y, pal.bg);

  // The HUD row. Labels above, values below, like the original. TIME sits at
  // x=224 because the battery badge owns the top-right corner and ate the
  // first layout's label.
  hudLabel(16, "TRACK");
  hudLabel(150, "WORLD");
  hudLabel(224, "TIME");

  const uint32_t h = fnv1a(st.pb.track_id);
  char buf[24];
  std::snprintf(buf, sizeof(buf), " %u-%u", (h % 8) + 1, ((h >> 8) % 4) + 1);
  hudValue(16, buf);

  // WORLD = the artist, upper-cased and clipped like a cartridge label.
  char world[10];
  size_t i = 0;
  for (; i < sizeof(world) - 1 && st.pb.artist[i]; ++i) {
    world[i] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(st.pb.artist[i])));
  }
  world[i] = '\0';
  hudValue(150, world);

  drawLevel(ctx);

  // Title under the level, centred, caps — the "world name" card.
  char lines[1][WRAP_MAX_LINE];
  M5.Display.setFont(fontSmall());
  wrapText(st.pb.title, 300, lines, 1);
  for (char *c = lines[0]; *c; ++c) {
    *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
  }
  M5.Display.setTextColor(pal.text, pal.bg);
  const int w = M5.Display.textWidth(lines[0]);
  M5.Display.setCursor((SCREEN_W - w) / 2, LEVEL_Y + LEVEL_S + 8);
  M5.Display.print(lines[0]);

  last_time_ = -1;
  last_play_ = -1;
  last_liked_ = -2;
  last_coins_ = -1;

  seed_ = fnv1a(st.pb.track_id);
  last_ms_ = 0;
  for (int i = 0; i < PIECES; ++i) piece_last_y_[i] = -10000;
}

void NesMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  using namespace theme;

  // Tetris rain. The clock only advances while playing, so pause freezes the
  // pieces mid-fall along with everything else on the device.
  const float dt =
      last_ms_ == 0 ? 0.016f : (now_ms - last_ms_ < 100 ? (now_ms - last_ms_) / 1000.0f : 0.1f);
  last_ms_ = now_ms;
  if (st.pb.is_playing) clock_ += dt;
  {
    constexpr int RANGE = TET_BOTTOM - TET_TOP;
    constexpr int PIECE_H = TET_CELL * 2;
    for (int i = 0; i < PIECES; ++i) {
      const uint32_t h = seed_ ^ (0x9e3779b9u * (i + 1));
      const int lane = TET_LANES[(h >> 4) % 4];
      const int shape = (h >> 8) % 7;
      const float speed = 14.0f + ((h >> 12) % 20);          // px/s
      const float phase = static_cast<float>((h >> 16) % RANGE);
      const int span = RANGE + PIECE_H;
      int y = TET_TOP - PIECE_H +
              static_cast<int>(std::fmod(clock_ * speed + phase,
                                         static_cast<float>(span)));
      if (y == piece_last_y_[i]) continue;
      // Erase the previous position; lanes never overlap anything else, so a
      // plain background fill is safe.
      if (piece_last_y_[i] > -10000) {
        M5.Display.fillRect(lane, piece_last_y_[i], TET_CELL * 4, PIECE_H,
                            pal.bg);
      }
      // Clip to the band by drawing only rows fully inside it.
      if (y >= TET_TOP - PIECE_H && y <= TET_BOTTOM) {
        M5.Display.setClipRect(lane, TET_TOP, TET_CELL * 4, RANGE);
        drawTetPiece(shape, lane, y, TET_COLORS[shape]);
        M5.Display.clearClipRect();
      }
      piece_last_y_[i] = y;
    }
  }

  // TIME counts down in whole seconds, like a level timer.
  const int remain =
      st.pb.duration_ms > st.pb.progress_ms
          ? static_cast<int>((st.pb.duration_ms - st.pb.progress_ms) / 1000)
          : 0;
  if (remain != last_time_) {
    last_time_ = remain;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%4d", remain);
    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim, pal.bg);
    M5.Display.setCursor(224, HUD_Y + 14);
    M5.Display.print(buf);
  }

  // 1UP heart when liked, in the HUD's spare corner.
  const int liked_now = st.pb.liked_known ? (st.pb.liked ? 1 : 0) : -1;
  if (liked_now != last_liked_) {
    last_liked_ = liked_now;
    M5.Display.fillRect(60, HUD_Y + 14, 18, 12, pal.bg);
    if (liked_now == 1) {
      static const uint8_t H[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
      for (int ry = 0; ry < 6; ++ry) {
        for (int rx = 0; rx < 7; ++rx) {
          if (H[ry] & (1 << (6 - rx))) {
            M5.Display.fillRect(60 + rx * 2, HUD_Y + 14 + ry * 2, 2, 2,
                                col(0xB71E7B));
          }
        }
      }
    }
  }

  // Coins = volume in tens. A little absurd, which is the point.
  const int coins = st.pb.volume_pct < 0 ? 0 : st.pb.volume_pct / 10;
  if (coins != last_coins_) {
    last_coins_ = coins;
    M5.Display.fillRect(92, HUD_Y + 12, 52, 14, pal.bg);
    drawCoin(92, HUD_Y + 14);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "x%02d", coins);
    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim, pal.bg);
    M5.Display.setCursor(106, HUD_Y + 14);
    M5.Display.print(buf);
  }

  // The classic pause card, dropped over the middle of the level.
  const int play_now = st.pb.is_playing ? 1 : 0;
  if (play_now != last_play_) {
    const bool was_paused = last_play_ == 0;
    last_play_ = play_now;
    if (!st.pb.is_playing) {
      const char *msg = "- PAUSED -";
      M5.Display.setFont(fontSmall());
      const int w = M5.Display.textWidth(msg);
      const int px = (SCREEN_W - w) / 2;
      const int py = LEVEL_Y + LEVEL_S / 2 - 8;
      M5.Display.fillRect(px - 8, py - 5, w + 16, 22, TFT_BLACK);
      M5.Display.setTextColor(0xFFFF, TFT_BLACK);
      M5.Display.setCursor(px, py);
      M5.Display.print(msg);
    } else if (was_paused) {
      // Un-pausing has to repaint the level under the card. One decode.
      drawLevel(ctx);
    }
  }
}
