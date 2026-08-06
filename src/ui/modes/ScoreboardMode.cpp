#include "ScoreboardMode.h"

#include <cstdio>

#include "../Crt.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {

struct Team {
  const char *name;
  uint32_t primary;
  uint32_t secondary;
};

// Colours only. Reproducing club marks would be both a licensing question and,
// at 320x240, visually worse than the palette alone.
const Team TEAMS[] = {
    {"GATORS", 0xFA4616, 0x0021A5},   // orange / blue
    {"JAGUARS", 0x006778, 0xD7A22A},  // teal / gold
};
constexpr int TEAM_COUNT = sizeof(TEAMS) / sizeof(TEAMS[0]);

constexpr int DIG_W = 26, DIG_H = 44, SEG = 5;
constexpr int CLOCK_X = 92, CLOCK_Y = 88;

uint16_t shade(uint32_t rgb, float f = 1.0f) {
  const float d = theme::dimFactor() * f;
  return M5.Display.color565(static_cast<uint8_t>(((rgb >> 16) & 0xFF) * d),
                             static_cast<uint8_t>(((rgb >> 8) & 0xFF) * d),
                             static_cast<uint8_t>((rgb & 0xFF) * d));
}

// Seven-segment digit. Drawn rather than typed: no font gives the stadium-board
// look, and the off-segments being faintly visible is most of the effect.
void digit(int x, int y, int v, uint16_t on, uint16_t off) {
  //        a
  //      f   b
  //        g
  //      e   c
  //        d
  static const uint8_t MAP[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D,
                                  0x7D, 0x07, 0x7F, 0x6F, 0x00};
  const uint8_t m = MAP[v < 0 || v > 10 ? 10 : v];
  const int w = DIG_W, h = DIG_H, s = SEG;
  const int vlen = (h - (3 * s)) / 2;  // vertical bars span half, minus bars
  auto seg = [&](int px, int py, int sw, int sh, bool lit) {
    M5.Display.fillRect(px, py, sw, sh, lit ? on : off);
  };
  seg(x + s, y, w - 2 * s, s, m & 0x01);                       // a  top
  seg(x + w - s, y + s, s, vlen, m & 0x02);                    // b  upper right
  seg(x + w - s, y + (h / 2) + (s / 2), s, vlen, m & 0x04);    // c  lower right
  seg(x + s, y + h - s, w - 2 * s, s, m & 0x08);               // d  bottom
  seg(x, y + (h / 2) + (s / 2), s, vlen, m & 0x10);            // e  lower left
  seg(x, y + s, s, vlen, m & 0x20);                            // f  upper left
  seg(x + s, y + (h - s) / 2, w - 2 * s, s, m & 0x40);         // g  middle
}

}  // namespace

void ScoreboardMode::enter(const AppState &st, const ViewCtx &) {
  const Team &t = TEAMS[team_ % TEAM_COUNT];
  M5.Display.fillScreen(TFT_BLACK);

  // Team bands top and bottom, the way a real board is trimmed.
  M5.Display.fillRect(0, 0, 320, 26, shade(t.primary));
  M5.Display.fillRect(0, 26, 320, 3, shade(t.secondary));
  M5.Display.fillRect(0, 214, 320, 26, shade(t.primary));
  M5.Display.fillRect(0, 211, 320, 3, shade(t.secondary));

  M5.Display.setFont(theme::fontTitle());
  M5.Display.setTextColor(TFT_BLACK, shade(t.primary));
  M5.Display.setCursor(10, 5);
  M5.Display.print(t.name);

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(TFT_BLACK, shade(t.primary));
  M5.Display.setCursor(240, 8);
  M5.Display.print("NOW PLAYING");

  // Track and artist as the two "team" rows.
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(shade(t.secondary, 1.0f), TFT_BLACK);
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 300, lines, 1);
  M5.Display.setCursor(10, 40);
  M5.Display.print(lines[0]);

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, TFT_BLACK);
  wrapText(st.pb.artist, 300, lines, 1);
  M5.Display.setCursor(10, 64);
  M5.Display.print(lines[0]);

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, TFT_BLACK);
  M5.Display.setCursor(10, 148);
  M5.Display.print("CLOCK");
  M5.Display.setCursor(10, 190);
  M5.Display.print("DRIVE");

  crt::apply(0, 29, 320, 182);
  last_sec_ = -1;
}

void ScoreboardMode::tick(const AppState &st, const ViewCtx &, uint32_t) {
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  if (sec == last_sec_) return;
  last_sec_ = sec;

  const Team &t = TEAMS[team_ % TEAM_COUNT];
  const uint16_t on = shade(t.secondary);
  const uint16_t off = shade(t.secondary, 0.12f);

  // Elapsed as a game clock: M:SS in seven segment.
  const uint32_t total = st.pb.progress_ms / 1000;
  const int mm = static_cast<int>(total / 60) % 10;
  const int ss = static_cast<int>(total % 60);

  digit(CLOCK_X, CLOCK_Y, mm, on, off);
  M5.Display.fillRect(CLOCK_X + DIG_W + 8, CLOCK_Y + 12, SEG, SEG, on);
  M5.Display.fillRect(CLOCK_X + DIG_W + 8, CLOCK_Y + 28, SEG, SEG, on);
  digit(CLOCK_X + DIG_W + 22, CLOCK_Y, ss / 10, on, off);
  digit(CLOCK_X + (DIG_W * 2) + 30, CLOCK_Y, ss % 10, on, off);

  // Progress as a yard line, marked every ten.
  constexpr int BX = 10, BW = 300, BY = 178;
  int filled = 0;
  if (st.pb.duration_ms > 0) {
    filled = static_cast<int>((static_cast<uint64_t>(BW) * st.pb.progress_ms) /
                              st.pb.duration_ms);
  }
  M5.Display.fillRect(BX, BY, filled, 8, shade(t.primary));
  M5.Display.fillRect(BX + filled, BY, BW - filled, 8, shade(t.secondary, 0.18f));
  for (int i = 1; i < 10; ++i) {
    M5.Display.drawFastVLine(BX + (BW * i) / 10, BY, 8, TFT_BLACK);
  }
}
