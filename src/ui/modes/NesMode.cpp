#include "NesMode.h"
#include "../../core/Hash.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "../../art/ArtRenderer.h"
#include "../TextWrap.h"
#include "../Theme.h"

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
}

void NesMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t) {
  using namespace theme;

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
