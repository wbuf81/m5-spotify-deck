#include "NowPlayingScreen.h"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "Theme.h"

namespace {

constexpr int MAX_LINE = 48;
constexpr int TITLE_LINES = 3;

// Greedy word wrap measured with the font currently selected on the display.
// Hard-breaks any single word too long to fit, and marks overflow with "..." so
// a long title truncates visibly rather than spilling out of the column.
int wrapText(const char *text, int max_w, char out[][MAX_LINE], int max_lines) {
  int nlines = 0;
  const char *p = text;

  while (*p && nlines < max_lines) {
    char buf[MAX_LINE] = {};
    int i = 0;
    int last_space = -1;

    while (p[i] && i < MAX_LINE - 1) {
      buf[i] = p[i];
      buf[i + 1] = '\0';
      if (M5.Display.textWidth(buf) > max_w) break;
      if (p[i] == ' ') last_space = i;
      ++i;
    }

    int cut;
    if (!p[i] || i >= MAX_LINE - 1) {
      cut = i;
    } else if (last_space > 0) {
      cut = last_space;
    } else {
      cut = i > 0 ? i : 1;  // single unbreakable word
    }

    std::memcpy(out[nlines], p, static_cast<size_t>(cut));
    out[nlines][cut] = '\0';
    ++nlines;

    p += cut;
    while (*p == ' ') ++p;
  }

  // Still text left over: mark the final line as truncated.
  if (*p && nlines > 0) {
    char *line = out[nlines - 1];
    int len = static_cast<int>(std::strlen(line));
    while (len > 0) {
      char probe[MAX_LINE];
      std::snprintf(probe, sizeof(probe), "%.*s...", len, line);
      if (M5.Display.textWidth(probe) <= max_w) {
        std::snprintf(line, MAX_LINE, "%s", probe);
        break;
      }
      --len;
    }
  }

  return nlines;
}

void formatTime(uint32_t ms, char *out, size_t cap) {
  const uint32_t total = ms / 1000;
  std::snprintf(out, cap, "%u:%02u", total / 60, total % 60);
}

// Two lobes and a point. Cheaper and far more legible at this size than any
// font glyph would be.
void drawHeart(int x, int y, uint16_t color) {
  M5.Display.fillCircle(x + 3, y + 3, 3, color);
  M5.Display.fillCircle(x + 8, y + 3, 3, color);
  M5.Display.fillTriangle(x, y + 4, x + 11, y + 4, x + 5, y + 11, color);
}

// Status indicator, not a button: it reports what the player is doing, so
// playing shows a triangle. (A touch button would show the inverse — the action
// it would perform.)
void drawPlayGlyph(int x, int y, bool playing, uint16_t color) {
  if (playing) {
    M5.Display.fillTriangle(x, y, x, y + 10, x + 9, y + 5, color);
  } else {
    M5.Display.fillRect(x, y, 3, 10, color);
    M5.Display.fillRect(x + 5, y, 3, 10, color);
  }
}

}  // namespace

void NowPlayingScreen::drawArtRegion(const AppState &st) {
  using namespace theme;
  if (!drawArt(st.pb.art_path, ART_X, ART_Y, ART_SIZE)) {
    // Missing or undecodable artwork must degrade, never blank the device.
    M5.Display.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, pal.bar_bg);
    M5.Display.setFont(theme::fontSmall());
    M5.Display.setTextColor(pal.dim);
    M5.Display.setCursor(ART_X + 6, ART_Y + ART_SIZE / 2 - 4);
    M5.Display.print("no artwork");
  }
}

void NowPlayingScreen::drawTextColumn(const AppState &st) {
  using namespace theme;

  // Reserve the foot for heart/volume so a three-line title cannot collide.
  constexpr int FOOT_H = 14;
  constexpr int TITLE_ARTIST_GAP = 6;

  const int avail_h = COL_H - FOOT_H;
  M5.Display.fillRect(COL_X, COL_Y, COL_W, avail_h, pal.bg);

  // Wrap first, then centre: short titles otherwise sit against the top with a
  // large void beneath them, which reads as a rendering bug rather than a
  // layout. Line spacing comes from the font itself so changing face or size
  // does not silently break the vertical maths.
  M5.Display.setFont(fontTitle());
  const int title_lead = M5.Display.fontHeight();
  char lines[TITLE_LINES][MAX_LINE];
  const int n = wrapText(st.pb.title, COL_W, lines, TITLE_LINES);

  M5.Display.setFont(fontArtist());
  const int artist_lead = M5.Display.fontHeight();
  char alines[2][MAX_LINE];
  const int an = wrapText(st.pb.artist, COL_W, alines, 2);

  const int content_h =
      (n * title_lead) + TITLE_ARTIST_GAP + (an * artist_lead);
  int y = COL_Y + (avail_h - content_h) / 2;
  if (y < COL_Y) y = COL_Y;

  M5.Display.setFont(fontTitle());
  M5.Display.setTextColor(pal.text);
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(COL_X, y);
    M5.Display.print(lines[i]);
    y += title_lead;
  }

  y += TITLE_ARTIST_GAP;
  M5.Display.setFont(fontArtist());
  M5.Display.setTextColor(pal.dim);
  for (int i = 0; i < an; ++i) {
    M5.Display.setCursor(COL_X, y);
    M5.Display.print(alines[i]);
    y += artist_lead;
  }
}

void NowPlayingScreen::drawColumnFoot(const AppState &st) {
  using namespace theme;
  constexpr int FOOT_H = 14;
  const int foot_y = COL_Y + COL_H - FOOT_H;
  M5.Display.fillRect(COL_X, foot_y, COL_W, FOOT_H, pal.bg);

  // No heart at all when saved-state is unknown. A dim heart would read as
  // "not liked", which is a claim we cannot make when the API refuses to tell
  // us. Absence is the honest rendering.
  if (st.pb.liked_known) {
    drawHeart(COL_X, foot_y + 1, st.pb.liked ? pal.accent : pal.bar_bg);
  }

  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(pal.dim);
  char vol[8];
  if (st.pb.volume_pct < 0) {
    std::snprintf(vol, sizeof(vol), "--");
  } else {
    std::snprintf(vol, sizeof(vol), "%d%%", st.pb.volume_pct);
  }
  const int w = M5.Display.textWidth(vol);
  M5.Display.setCursor(COL_X + COL_W - w, foot_y + 3);
  M5.Display.print(vol);
}

// Nothing below clears a region it is about to overwrite. Panel_sdl (and a real
// SPI panel) can present a frame mid-draw, so clear-then-redraw shows up as a
// once-per-second blink of whatever sits in the cleared area.

void NowPlayingScreen::drawProgressBar(const AppState &st) {
  using namespace theme;
  int played = 0;
  if (st.pb.duration_ms > 0) {
    uint32_t p = st.pb.progress_ms;
    if (p > st.pb.duration_ms) p = st.pb.duration_ms;
    played = static_cast<int>((static_cast<uint64_t>(BAR_W) * p) /
                              st.pb.duration_ms);
  }
  // Both halves painted; the bar is fully covered without ever being blanked.
  M5.Display.fillRect(BAR_X, BAR_Y, played, BAR_H, pal.accent);
  M5.Display.fillRect(BAR_X + played, BAR_Y, BAR_W - played, BAR_H, pal.bar_bg);
}

void NowPlayingScreen::drawTimeRow(const AppState &st, bool clear_first) {
  using namespace theme;
  if (clear_first) {
    M5.Display.fillRect(0, TIME_Y, SCREEN_W, 10, pal.strip);
  }

  M5.Display.setFont(theme::fontSmall());
  // Opaque text: every glyph cell repaints its own background, so the digits
  // update in place with no intermediate blank.
  M5.Display.setTextColor(pal.dim, pal.strip);

  char buf[16];
  char padded[16];

  // Fixed-width fields with a fixed-width font keep the pixel extents stable,
  // so a shrinking string (10:00 -> 9:59) leaves no residue behind.
  formatTime(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(padded, sizeof(padded), "%-6s", buf);
  M5.Display.setCursor(BAR_X, TIME_Y + 2);
  M5.Display.print(padded);

  formatTime(st.pb.duration_ms, buf, sizeof(buf));
  std::snprintf(padded, sizeof(padded), "%6s", buf);
  M5.Display.setCursor(SCREEN_W - MARGIN - (6 * 6), TIME_Y + 2);
  M5.Display.print(padded);
}

void NowPlayingScreen::drawToastRow(const AppState &st) {
  using namespace theme;
  M5.Display.fillRect(0, TIME_Y, SCREEN_W, 10, pal.strip);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(pal.warn, pal.strip);
  const int w = M5.Display.textWidth(st.toast);
  M5.Display.setCursor((SCREEN_W - w) / 2, TIME_Y + 2);
  M5.Display.print(st.toast);
}

void NowPlayingScreen::drawPlayGlyphBox(const AppState &st) {
  using namespace theme;
  const int gx = SCREEN_W / 2 - 4;
  const int gy = TIME_Y - 1;
  // Only ever called when the state actually changed, so this small clear is
  // not on the once-a-second path.
  M5.Display.fillRect(gx - 1, gy - 1, 13, 13, pal.strip);
  drawPlayGlyph(gx, gy, st.pb.is_playing, pal.text);
}

void NowPlayingScreen::render(const AppState &st, uint32_t now_ms) {
  using namespace theme;

  const bool album_changed = std::strcmp(last_album_, st.pb.album_id) != 0;
  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;
  const int progress_sec = static_cast<int>(st.pb.progress_ms / 1000);
  const bool toast_active = st.toastActive(now_ms);

  M5.Display.startWrite();

  if (force_) {
    M5.Display.fillScreen(pal.bg);
    M5.Display.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, pal.strip);
  }

  if (force_ || album_changed) {
    drawArtRegion(st);
  }
  if (force_ || track_changed) {
    drawTextColumn(st);
  }
  if (force_ || track_changed || st.pb.liked != last_liked_ ||
      st.pb.liked_known != last_liked_known_ ||
      st.pb.volume_pct != last_volume_) {
    drawColumnFoot(st);
  }

  if (force_ || progress_sec != last_progress_sec_) {
    drawProgressBar(st);
  }

  // The toast borrows the time row, so redraw that row only on a real
  // transition rather than every frame the toast happens to be up.
  const bool toast_changed = (toast_active != last_toast_active_) ||
                             (toast_active &&
                              std::strcmp(last_toast_, st.toast) != 0);
  if (force_ || toast_changed) {
    if (toast_active) {
      drawToastRow(st);
    } else {
      drawTimeRow(st, /*clear_first=*/true);
    }
  } else if (!toast_active && progress_sec != last_progress_sec_) {
    drawTimeRow(st, /*clear_first=*/false);
  }

  if (force_ || st.pb.is_playing != last_playing_) {
    drawPlayGlyphBox(st);
  }

  // A problem with the link shows as a small amber marker rather than stealing
  // the strip, which the toast already owns.
  if (force_ || st.link != last_link_) {
    const uint16_t c = (st.link == LinkStatus::Online) ? pal.bg : pal.warn;
    M5.Display.fillRect(SCREEN_W - 6, 2, 4, 4, c);
  }

  M5.Display.endWrite();

  setStr(last_album_, ID_LEN, st.pb.album_id);
  setStr(last_track_, ID_LEN, st.pb.track_id);
  last_progress_sec_ = progress_sec;
  last_volume_ = st.pb.volume_pct;
  last_liked_ = st.pb.liked;
  last_liked_known_ = st.pb.liked_known;
  last_playing_ = st.pb.is_playing;
  last_toast_active_ = toast_active;
  setStr(last_toast_, sizeof(last_toast_), st.toast);
  last_link_ = st.link;
  force_ = false;
}
