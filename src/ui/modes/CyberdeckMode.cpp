#include "CyberdeckMode.h"

#include <M5Unified.h>

#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../Crt.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {

// Phosphor green, independent of the album tint: a terminal that changed colour
// with the record would stop reading as a terminal.
constexpr uint32_t P_BRIGHT = 0x4AF626;
constexpr uint32_t P_MID = 0x2E9418;
constexpr uint32_t P_DIM = 0x1A5610;

constexpr int LEFT = 12;
constexpr int BAR_Y = 168;
constexpr int TIME_Y = 186;
constexpr int CURSOR_Y = 210;

uint16_t phos(uint32_t rgb) {
  const float f = theme::dimFactor();
  return M5.Display.color565(static_cast<uint8_t>(((rgb >> 16) & 0xFF) * f),
                             static_cast<uint8_t>(((rgb >> 8) & 0xFF) * f),
                             static_cast<uint8_t>((rgb & 0xFF) * f));
}

void label(int y, const char *tag, const char *value, uint16_t tag_c,
           uint16_t val_c) {
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(tag_c);
  M5.Display.setCursor(LEFT, y);
  M5.Display.print(tag);
  M5.Display.setTextColor(val_c);
  M5.Display.setCursor(LEFT + 40, y);
  M5.Display.print(value);
}

}  // namespace

void CyberdeckMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(TFT_BLACK);

  const uint16_t bright = phos(P_BRIGHT);
  const uint16_t mid = phos(P_MID);
  const uint16_t dim = phos(P_DIM);

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(mid);
  M5.Display.setCursor(LEFT, 12);
  M5.Display.print("> M5-SPOTIFY  ::  NOW_PLAYING");
  M5.Display.drawFastHLine(LEFT, 28, 320 - (LEFT * 2), dim);

  // Album art as a coarse luminance ramp in phosphor, the way a terminal that
  // could show a picture would show one.
  constexpr int TH = 16, CELL = 4, IMG_X = 232, IMG_Y = 40;
  M5Canvas thumb(&M5.Display);
  thumb.setColorDepth(16);
  if (thumb.createSprite(TH, TH)) {
    thumb.fillSprite(0);
    if (drawArtInto(&thumb, ctx.art_path, 0, 0, TH)) {
      for (int y = 0; y < TH; ++y) {
        for (int x = 0; x < TH; ++x) {
          const uint16_t c = thumb.readPixel(x, y);
          const int lum = ((((c >> 11) & 0x1F) << 3) * 30 +
                           (((c >> 5) & 0x3F) << 2) * 59 +
                           ((c & 0x1F) << 3) * 11) / 100;
          const uint32_t base = lum > 170 ? P_BRIGHT : (lum > 85 ? P_MID : P_DIM);
          if (lum > 28) {
            M5.Display.fillRect(IMG_X + x * CELL, IMG_Y + y * CELL, CELL - 1,
                                CELL - 1, phos(base));
          }
        }
      }
    }
    thumb.deleteSprite();
  }

  // Wrapped so a long title does not run under the thumbnail.
  M5.Display.setFont(theme::fontSmall());
  char lines[2][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 200, lines, 2);
  label(44, "TRK", n > 0 ? lines[0] : "", dim, bright);
  if (n > 1) label(58, "", lines[1], dim, bright);

  char alines[1][WRAP_MAX_LINE];
  wrapText(st.pb.artist, 200, alines, 1);
  label(78, "ART", alines[0], dim, mid);

  char vol[16];
  if (st.pb.volume_pct < 0) {
    std::snprintf(vol, sizeof(vol), "--");
  } else {
    std::snprintf(vol, sizeof(vol), "%d%%", st.pb.volume_pct);
  }
  label(98, "VOL", vol, dim, mid);
  label(118, "SAV", st.pb.liked_known ? (st.pb.liked ? "YES" : "NO") : "?", dim,
        mid);

  crt::apply(0, 0, 320, 240);
  last_sec_ = -1;
}

void CyberdeckMode::tick(const AppState &st, const ViewCtx &, uint32_t now_ms) {
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  const bool cursor = ((now_ms / 500) % 2) == 0;
  if (sec == last_sec_ && cursor == cursor_on_) return;

  const uint16_t bright = phos(P_BRIGHT);
  const uint16_t mid = phos(P_MID);
  const uint16_t dim = phos(P_DIM);

  if (sec != last_sec_) {
    last_sec_ = sec;

    // Bar drawn as discrete cells, so it reads as a terminal gauge rather than
    // a smooth progress bar.
    constexpr int CELLS = 24, CW = 11;
    int filled = 0;
    if (st.pb.duration_ms > 0) {
      filled = static_cast<int>((static_cast<uint64_t>(CELLS) * st.pb.progress_ms) /
                                st.pb.duration_ms);
    }
    for (int i = 0; i < CELLS; ++i) {
      M5.Display.fillRect(LEFT + i * CW, BAR_Y, CW - 2, 10,
                          i < filled ? bright : dim);
    }

    char buf[16];
    M5.Display.setFont(theme::fontSmall());
    M5.Display.setTextColor(mid, TFT_BLACK);
    formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
    char pad[16];
    std::snprintf(pad, sizeof(pad), "%-8s", buf);
    M5.Display.setCursor(LEFT, TIME_Y);
    M5.Display.print(pad);

    formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
    std::snprintf(pad, sizeof(pad), "%8s", buf);
    M5.Display.setCursor(320 - LEFT - (8 * 6), TIME_Y);
    M5.Display.print(pad);

    crt::apply(0, BAR_Y - 2, 320, 34);
  }

  if (cursor != cursor_on_) {
    cursor_on_ = cursor;
    M5.Display.setFont(theme::fontSmall());
    M5.Display.setTextColor(bright, TFT_BLACK);
    M5.Display.setCursor(LEFT, CURSOR_Y);
    M5.Display.print(st.pb.is_playing ? "> PLAY " : "> PAUSE");
    M5.Display.fillRect(LEFT + 54, CURSOR_Y, 6, 10, cursor ? bright : TFT_BLACK);
  }
}
