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
constexpr uint32_t P_FAINT = 0x0C2A08;

constexpr int LEFT = 12;

// Art panel: 24x24 cells of 3px inside a bracketed frame. The first version
// was 16 cells of 4px with a hard three-level threshold, which read as static
// rather than as a picture.
constexpr int IMG_CELLS = 24, IMG_CELL = 3;
constexpr int IMG_X = 232, IMG_Y = 44;
constexpr int IMG_W = IMG_CELLS * IMG_CELL;

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

// Corner brackets around a region — the terminal way to frame a picture
// without boxing it in.
void brackets(int x, int y, int w, int h, uint16_t c) {
  constexpr int L = 7;
  M5.Display.drawFastHLine(x - 2, y - 2, L, c);
  M5.Display.drawFastVLine(x - 2, y - 2, L, c);
  M5.Display.drawFastHLine(x + w - L + 2, y - 2, L, c);
  M5.Display.drawFastVLine(x + w + 1, y - 2, L, c);
  M5.Display.drawFastHLine(x - 2, y + h + 1, L, c);
  M5.Display.drawFastVLine(x - 2, y + h - L + 2, L, c);
  M5.Display.drawFastHLine(x + w - L + 2, y + h + 1, L, c);
  M5.Display.drawFastVLine(x + w + 1, y + h - L + 2, L, c);
}

}  // namespace

void CyberdeckMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(TFT_BLACK);

  const uint16_t bright = phos(P_BRIGHT);
  const uint16_t mid = phos(P_MID);
  const uint16_t dim = phos(P_DIM);
  const uint16_t faint = phos(P_FAINT);

  // Scanlines FIRST, as ambient texture behind everything. The old version
  // applied them last, over the finished frame, and at a 3px pitch they erased
  // every third row of the glyphs — the whole screen looked like corrupted
  // data, which is atmosphere of exactly the wrong kind. Text drawn after this
  // stays crisp, and the empty black still reads as a CRT.
  for (int y = 0; y < theme::STRIP_Y; y += 3) {
    M5.Display.drawFastHLine(0, y, 320, faint);
  }

  // Header: inverse-video bar, the way a real terminal marks a title.
  M5.Display.fillRect(0, 6, 320, 16, dim);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(bright, dim);
  M5.Display.setCursor(LEFT, 10);
  M5.Display.print("M5-DECK v2 :: NOW_PLAYING");
  M5.Display.drawFastHLine(0, 24, 320, mid);

  // Album art as a phosphor luminance ramp. Four levels with a 2x2 Bayer
  // nudge, so gradients ramp instead of banding into three flat zones.
  M5Canvas thumb(&M5.Display);
  thumb.setColorDepth(16);
  if (thumb.createSprite(IMG_CELLS, IMG_CELLS)) {
    thumb.fillSprite(0);
    if (drawArtInto(&thumb, ctx.art_path, 0, 0, IMG_CELLS)) {
      constexpr int BAYER[2][2] = {{-16, 16}, {24, -8}};
      for (int y = 0; y < IMG_CELLS; ++y) {
        for (int x = 0; x < IMG_CELLS; ++x) {
          const uint16_t c = thumb.readPixel(x, y);
          int lum = ((((c >> 11) & 0x1F) << 3) * 30 +
                     (((c >> 5) & 0x3F) << 2) * 59 +
                     ((c & 0x1F) << 3) * 11) / 100;
          lum += BAYER[y & 1][x & 1];
          const uint16_t cell = lum > 170 ? bright
                                : lum > 105 ? mid
                                : lum > 45  ? dim
                                            : faint;
          M5.Display.fillRect(IMG_X + x * IMG_CELL, IMG_Y + y * IMG_CELL,
                              IMG_CELL - 1, IMG_CELL - 1, cell);
        }
      }
    }
    thumb.deleteSprite();
  }
  brackets(IMG_X, IMG_Y, IMG_W, IMG_W, mid);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(dim, TFT_BLACK);
  M5.Display.setCursor(IMG_X + 6, IMG_Y + IMG_W + 8);
  M5.Display.print("IMG.0x2F");

  // Wrapped so a long title does not run under the thumbnail.
  char lines[2][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 200, lines, 2);
  label(40, "TRK", n > 0 ? lines[0] : "", dim, bright);
  if (n > 1) label(54, "", lines[1], dim, bright);

  char alines[1][WRAP_MAX_LINE];
  wrapText(st.pb.artist, 200, alines, 1);
  label(74, "ART", alines[0], dim, mid);

  char vol[16];
  if (st.pb.volume_pct < 0) {
    std::snprintf(vol, sizeof(vol), "--");
  } else {
    std::snprintf(vol, sizeof(vol), "%d%%", st.pb.volume_pct);
  }
  label(94, "VOL", vol, dim, mid);
  label(114, "SAV", st.pb.liked_known ? (st.pb.liked ? "YES" : "NO") : "?", dim,
        mid);

  const char *lnk = "?";
  switch (st.link) {
    case LinkStatus::Online:     lnk = "ONLINE"; break;
    case LinkStatus::Connecting: lnk = "SYNC.."; break;
    case LinkStatus::Offline:    lnk = "DOWN";   break;
    default:                     lnk = "INIT";   break;
  }
  label(134, "LNK", lnk, dim, mid);

  // Divider above the shared strip, echoing the header rule.
  M5.Display.drawFastHLine(0, theme::STRIP_Y - 4, 320, dim);
}

void CyberdeckMode::tick(const AppState &, const ViewCtx &, uint32_t) {
  // Transport, heart, volume and timecodes all live in the shared StatusStrip.
}
