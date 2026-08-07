#include "CyberdeckMode.h"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

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

// The event log region, between the LNK row and the strip's divider.
constexpr int LOG_Y = 146;
constexpr int LOG_LINE_H = 13;

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

  // The log survives across enters (a terminal's scrollback would), but the
  // panel repaint means it must draw again, and this track's load is news.
  char line[28];
  std::snprintf(line, sizeof(line), "> LOAD %.16s", st.pb.title);
  pushLog(line);
  std::snprintf(last_track_, sizeof(last_track_), "%s", st.pb.track_id);
  last_play_ = st.pb.is_playing ? 1 : 0;
  last_vol_ = st.pb.volume_pct;
  last_liked_ = st.pb.liked_known ? (st.pb.liked ? 1 : 0) : -1;
  drawLog();
}

void CyberdeckMode::pushLog(const char *line) {
  std::memmove(log_[0], log_[1], sizeof(log_[0]) * 2);
  std::snprintf(log_[2], sizeof(log_[2]), "%s", line);
  log_dirty_ = true;
}

void CyberdeckMode::drawLog() {
  const uint16_t mid = phos(P_MID);
  const uint16_t dim = phos(P_DIM);
  const uint16_t faint = phos(P_FAINT);

  // Clear the region, restore its scanlines, then the three lines. The newest
  // line is brightest, like phosphor persistence in reverse.
  M5.Display.fillRect(0, LOG_Y, 320, LOG_LINE_H * 3 + 4, TFT_BLACK);
  for (int y = LOG_Y; y < LOG_Y + LOG_LINE_H * 3 + 4; y += 3) {
    M5.Display.drawFastHLine(0, y, 320, faint);
  }
  M5.Display.setFont(theme::fontSmall());
  for (int i = 0; i < 3; ++i) {
    M5.Display.setTextColor(i == 2 ? mid : dim);
    M5.Display.setCursor(LEFT, LOG_Y + i * LOG_LINE_H);
    M5.Display.print(log_[i]);
  }
  log_dirty_ = false;
}

void CyberdeckMode::tick(const AppState &st, const ViewCtx &, uint32_t now_ms) {
  // Transport lives in the strip; this feeds the terminal's event log from
  // real state changes, because a terminal that never prints is a poster.
  char line[28];

  const int play_now = st.pb.is_playing ? 1 : 0;
  if (play_now != last_play_) {
    last_play_ = play_now;
    pushLog(play_now ? "> EXEC play" : "> HALT pause");
  }
  if (st.pb.volume_pct != last_vol_) {
    last_vol_ = st.pb.volume_pct;
    if (st.pb.volume_pct >= 0) {
      std::snprintf(line, sizeof(line), "> VOL %d", st.pb.volume_pct);
      pushLog(line);
    }
  }
  const int liked_now = st.pb.liked_known ? (st.pb.liked ? 1 : 0) : -1;
  if (liked_now != last_liked_) {
    if (last_liked_ != -2 && liked_now >= 0) {
      pushLog(liked_now ? "> SAV lib +track" : "> DEL lib -track");
    }
    last_liked_ = liked_now;
    // The SAV readout row must agree with the log it just printed.
    M5.Display.fillRect(LEFT + 40, 114, 80, 12, TFT_BLACK);
    label(114, "", liked_now == 1 ? "YES" : (liked_now == 0 ? "NO" : "?"),
          phos(P_DIM), phos(P_MID));
  }
  // Idle heartbeat, so the log never looks dead for long.
  if (now_ms - last_scan_ms_ > 9000) {
    last_scan_ms_ = now_ms;
    std::snprintf(line, sizeof(line), "> SCAN link.. %s",
                  st.link == LinkStatus::Online ? "ok" : "??");
    pushLog(line);
  }
  if (log_dirty_) drawLog();

  // The block cursor, blinking after the newest line.
  const bool cur = ((now_ms / 500) % 2) == 0;
  if (cur != cursor_on_) {
    cursor_on_ = cur;
    M5.Display.setFont(theme::fontSmall());
    const int w = M5.Display.textWidth(log_[2]);
    M5.Display.fillRect(LEFT + w + 4, LOG_Y + 2 * LOG_LINE_H, 7, 11,
                        cur ? phos(P_MID) : TFT_BLACK);
  }
}
