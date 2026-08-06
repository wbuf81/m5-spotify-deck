#include "NowPlayingScreen.h"

#include <cstdio>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "../platform/esp32/Esp32Storage.h"
#include "TextWrap.h"
#include "Theme.h"
#include "TimeFormat.h"

namespace {
constexpr int TITLE_LINES = 3;
constexpr int FOOT_H = 14;
constexpr int TITLE_ARTIST_GAP = 6;
}  // namespace

void NowPlayingScreen::release() {
  heart_.release();
  glyph_.release();
  scene_.release();
}

void NowPlayingScreen::drawArtRegion(const AppState &st) {
  using namespace theme;
  if (drawArt(st.pb.art_path, ART_X, ART_Y, ART_SIZE)) {
    // Tie the scene to whatever is actually playing.
    scene_.setTint(sampleArtTint(st.pb.art_path, pal.accent));
  } else {
    // Missing or undecodable artwork must degrade, never blank the device.
    // Name the actual cause: an absent card looks identical to a decode
    // failure otherwise, and sends you debugging the wrong thing.
    M5.Display.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, pal.bar_bg);
    M5.Display.setFont(fontSmall());
    M5.Display.setTextColor(pal.dim);
    const bool no_card = !storageAvailable();
    const char *line1 = no_card ? "no sd card" : "no artwork";
    const char *line2 = no_card ? "fat32 card for art" : "";
    M5.Display.setCursor(ART_X + 8, ART_Y + ART_SIZE / 2 - 10);
    M5.Display.print(line1);
    if (line2[0]) {
      M5.Display.setCursor(ART_X + 8, ART_Y + ART_SIZE / 2 + 4);
      M5.Display.print(line2);
    }
    scene_.setTint(pal.accent);
  }
}

void NowPlayingScreen::drawTextColumn(const AppState &st) {
  using namespace theme;

  // Top-aligned: the space below belongs to the scene panel, so centring would
  // push the text down into it.
  M5.Display.fillRect(COL_X, COL_Y, COL_W, TEXT_H, pal.bg);

  // Line spacing comes from the font itself, so changing face or size cannot
  // silently break the vertical maths.
  M5.Display.setFont(fontTitle());
  const int title_lead = M5.Display.fontHeight();
  char lines[TITLE_LINES][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, COL_W, lines, TITLE_LINES);

  M5.Display.setFont(fontArtist());
  const int artist_lead = M5.Display.fontHeight();
  char alines[2][WRAP_MAX_LINE];
  const int an = wrapText(st.pb.artist, COL_W, alines, 2);

  int y = COL_Y;

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
  const int foot_y = COL_Y + COL_H - FOOT_H;
  M5.Display.fillRect(COL_X, foot_y, COL_W, FOOT_H, pal.bg);
  // The heart is drawn by HeartIndicator, which owns a sprite so it can animate
  // without tearing. Clearing here is enough; it repaints itself.

  M5.Display.setFont(fontSmall());
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

// Nothing below clears a region it is about to overwrite. A panel can present a
// frame mid-draw, so clear-then-redraw shows up as a once-per-second blink of
// whatever sits in the cleared area.

void NowPlayingScreen::drawProgressBar(const AppState &st) {
  using namespace theme;
  int played = 0;
  if (st.pb.duration_ms > 0) {
    uint32_t p = st.pb.progress_ms;
    if (p > st.pb.duration_ms) p = st.pb.duration_ms;
    played = static_cast<int>((static_cast<uint64_t>(BAR_W) * p) / st.pb.duration_ms);
  }
  // Both halves painted, so the bar is fully covered without ever being blanked.
  M5.Display.fillRect(BAR_X, BAR_Y, played, BAR_H, pal.accent);
  M5.Display.fillRect(BAR_X + played, BAR_Y, BAR_W - played, BAR_H, pal.bar_bg);
}

void NowPlayingScreen::drawTimeRow(const AppState &st, bool clear_first) {
  using namespace theme;
  if (clear_first) M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);

  M5.Display.setFont(fontSmall());
  // Opaque text: every glyph cell repaints its own background, so digits update
  // in place with no intermediate blank.
  M5.Display.setTextColor(pal.dim, pal.strip);

  char buf[16];
  char padded[16];

  // Fixed-width fields with a monospaced face keep pixel extents stable, so a
  // shrinking string (10:00 -> 9:59) leaves no residue.
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(padded, sizeof(padded), "%-7s", buf);
  M5.Display.setCursor(BAR_X, TIME_Y + 2);
  M5.Display.print(padded);

  // The right-hand figure counts down. Total duration looked frozen there,
  // because it is; the leading minus makes "remaining" unambiguous.
  formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
  const int rw = M5.Display.textWidth("-00:00");
  std::snprintf(padded, sizeof(padded), "%7s", buf);
  M5.Display.setCursor(SCREEN_W - MARGIN - rw, TIME_Y + 2);
  M5.Display.print(padded);
}

void NowPlayingScreen::drawToastRow(const AppState &st) {
  using namespace theme;
  M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.warn, pal.strip);
  const int w = M5.Display.textWidth(st.toast);
  M5.Display.setCursor((SCREEN_W - w) / 2, TIME_Y + 2);
  M5.Display.print(st.toast);
}

void NowPlayingScreen::render(const AppState &st, uint32_t now_ms) {
  using namespace theme;

  const bool album_changed = std::strcmp(last_album_, st.pb.album_id) != 0;
  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;
  const bool liked_changed = st.pb.liked != last_liked_ ||
                             st.pb.liked_known != last_liked_known_;
  const int progress_sec = static_cast<int>(st.pb.progress_ms / 1000);
  const bool toast_active = st.toastActive(now_ms);

  M5.Display.startWrite();

  if (force_) {
    M5.Display.fillScreen(pal.bg);
    M5.Display.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, pal.strip);
    scene_.invalidate();
  }

  if (force_ || album_changed) drawArtRegion(st);
  if (force_ || track_changed) {
    drawTextColumn(st);
    scene_.onTrackChange(st.pb.track_id);  // a new song gets a new scene
  }

  // Animate only a real like/unlike. Not the first sight of a track, and not
  // saved-state merely becoming known — neither is a user action, and animating
  // them would make the display twitch on every song.
  if (!force_ && !track_changed && last_liked_known_ && st.pb.liked_known &&
      st.pb.liked != last_liked_) {
    heart_.trigger(st.pb.liked, now_ms);
  }

  if (force_ || track_changed || liked_changed ||
      st.pb.volume_pct != last_volume_) {
    drawColumnFoot(st);
  }
  if (force_ || track_changed || liked_changed || heart_.animating()) {
    heart_.render(st.pb.liked_known, st.pb.liked, now_ms);
  }

  if (force_ || progress_sec != last_progress_sec_) drawProgressBar(st);

  // The toast borrows the whole info row, glyph included, so redraw that row
  // only on a real transition — and restore both halves of it, or the play
  // glyph stays erased and looks like the button vanished.
  const bool toast_changed =
      (toast_active != last_toast_active_) ||
      (toast_active && std::strcmp(last_toast_, st.toast) != 0);
  if (force_ || toast_changed) {
    if (toast_active) {
      drawToastRow(st);
    } else {
      drawTimeRow(st, /*clear_first=*/true);
      glyph_.render(st.pb.is_playing, now_ms);
    }
  } else if (!toast_active && progress_sec != last_progress_sec_) {
    drawTimeRow(st, /*clear_first=*/false);
  }

  // Every frame: continuously animated, and the one element whose whole job is
  // to look alive.
  scene_.render(st.pb.is_playing, st.pb.volume_pct, st.pb.progress_ms,
                st.pb.duration_ms, now_ms);

  if (!force_ && st.pb.is_playing != last_playing_) {
    glyph_.trigger(st.pb.is_playing, now_ms);
  }
  // Suppressed while a toast is up: the glyph would punch a hole through the
  // message, and the restore path repaints it from current state anyway.
  if (!toast_active &&
      (force_ || st.pb.is_playing != last_playing_ || glyph_.animating())) {
    glyph_.render(st.pb.is_playing, now_ms);
  }

  // A link problem shows as a small amber marker rather than stealing the
  // strip, which the toast already owns.
  if (force_ || st.link != last_link_) {
    M5.Display.fillRect(SCREEN_W - 6, 2, 4, 4,
                        st.link == LinkStatus::Online ? pal.bg : pal.warn);
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
