#include "StatusStrip.h"

#include <cstdio>
#include <cstring>

#include "Anim.h"
#include "Theme.h"
#include "TimeFormat.h"

// Nothing here clears a region it is about to overwrite. A panel can present a
// frame mid-draw, so clear-then-redraw shows up as a once-per-second blink of
// whatever sits in the cleared area. Both halves of the bar are painted, text
// is opaque over the band colour, and the animated glyphs land as sprite
// blits.

namespace {
constexpr int VOL_X = STRIP_VOL_X;
}

void StatusStrip::release() {
  heart_.release();
  glyph_.release();
}

void StatusStrip::drawBar(const AppState &st, uint16_t tint) {
  using namespace theme;
  int played = 0;
  if (st.pb.duration_ms > 0) {
    uint32_t p = st.pb.progress_ms;
    if (p > st.pb.duration_ms) p = st.pb.duration_ms;
    played = static_cast<int>((static_cast<uint64_t>(BAR_W) * p) / st.pb.duration_ms);
  }
  M5.Display.fillRect(BAR_X, BAR_Y, played, BAR_H, tint);
  M5.Display.fillRect(BAR_X + played, BAR_Y, BAR_W - played, BAR_H, pal.bar_bg);

  // Comet head: the leading edge brightens toward white through a short tail.
  if (played > 10) {
    for (int i = 0; i < 8; ++i) {
      M5.Display.drawFastVLine(BAR_X + played - 1 - i, BAR_Y, BAR_H,
                               anim::lerp565(tint, 0xFFFF, 0.9f - i * 0.11f));
    }
  }
}

void StatusStrip::drawRow(const AppState &st, bool clear_first) {
  using namespace theme;
  if (clear_first) M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);

  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.dim, pal.strip);

  char buf[16], padded[16];

  // Fixed-width fields with a monospaced face keep pixel extents stable, so a
  // shrinking string (10:00 -> 9:59) leaves no residue.
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(padded, sizeof(padded), "%-7s", buf);
  M5.Display.setCursor(BAR_X, TIME_Y + 2);
  M5.Display.print(padded);

  formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
  const int rw = M5.Display.textWidth("-00:00");
  std::snprintf(padded, sizeof(padded), "%7s", buf);
  M5.Display.setCursor(SCREEN_W - MARGIN - rw, TIME_Y + 2);
  M5.Display.print(padded);

  drawVolume(st);
  drawBattery(st);
}

void StatusStrip::drawVolume(const AppState &st) {
  using namespace theme;
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.dim, pal.strip);
  char vol[8];
  if (st.pb.volume_pct < 0) {
    std::snprintf(vol, sizeof(vol), "  --");
  } else {
    std::snprintf(vol, sizeof(vol), "%3d%%", st.pb.volume_pct);
  }
  const int tx = VOL_X + 14;
  M5.Display.setCursor(tx, TIME_Y + 2);
  M5.Display.print(vol);

  // Speaker glyph with level arcs, opaque plate behind it so a louder arc from
  // the previous value never lingers. sy centres the 10px glyph on y=221.
  const int sx = VOL_X, sy = TIME_Y + 4;
  M5.Display.fillRect(sx - 1, sy - 2, 14, 13, pal.strip);
  M5.Display.fillRect(sx, sy + 3, 3, 4, pal.dim);
  M5.Display.fillTriangle(sx + 3, sy + 4, sx + 6, sy, sx + 6, sy + 9, pal.dim);
  if (st.pb.volume_pct > 0) M5.Display.drawFastVLine(sx + 8, sy + 3, 4, pal.dim);
  if (st.pb.volume_pct > 50) M5.Display.drawFastVLine(sx + 10, sy + 1, 8, pal.dim);
}

void StatusStrip::drawBattery(const AppState &st) {
  using namespace theme;
  const int8_t pct = st.battery_pct;
  const int x = STRIP_BATT_X, y = TIME_Y + 5;  // 9px box centred on y=221

  // Plate first, so a previous level never lingers.
  M5.Display.fillRect(x - 1, y - 2, 20, 13, pal.strip);
  if (pct < 0) return;  // no reading: draw nothing rather than a lie

  M5.Display.drawRect(x, y, 15, 9, pal.dim);
  M5.Display.fillRect(x + 15, y + 2, 2, 5, pal.dim);

  // Four fill levels for the four states the IP5306 can actually distinguish.
  // No percentage text: 25%-step hardware printing "73%" would be theater.
  const uint16_t c = pct <= 25   ? pal.warn
                     : pct <= 50 ? M5.Display.color565(0xF0, 0xC0, 0x40)
                                 : M5.Display.color565(0x40, 0xD0, 0x70);
  int fill = ((15 - 4) * pct) / 100;
  if (fill < 1) fill = 1;
  M5.Display.fillRect(x + 2, y + 2, fill, 5, c);
}

void StatusStrip::drawToast(const AppState &st) {
  using namespace theme;
  M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);
  M5.Display.setFont(fontSmall());
  M5.Display.setTextColor(pal.warn, pal.strip);
  const int w = M5.Display.textWidth(st.toast);
  M5.Display.setCursor((SCREEN_W - w) / 2, TIME_Y + 2);
  M5.Display.print(st.toast);
}

void StatusStrip::render(const AppState &st, uint32_t now_ms, uint16_t tint) {
  using namespace theme;

  const bool track_changed = std::strcmp(last_track_, st.pb.track_id) != 0;
  const int sec = static_cast<int>(st.pb.progress_ms / 1000);
  const bool toast_active = st.toastActive(now_ms);

  if (force_) {
    M5.Display.fillRect(0, STRIP_Y, SCREEN_W, STRIP_H, pal.strip);
  }

  // Animate only real user-visible transitions, never a track change and never
  // saved-state merely becoming known.
  if (!force_ && !track_changed && last_liked_known_ && st.pb.liked_known &&
      st.pb.liked != last_liked_) {
    heart_.trigger(st.pb.liked, now_ms);
  }
  if (!force_ && !track_changed && st.pb.is_playing != last_playing_) {
    glyph_.trigger(st.pb.is_playing, now_ms);
  }

  if (force_ || sec != last_sec_ || tint != last_tint_) drawBar(st, tint);

  const bool toast_changed =
      (toast_active != last_toast_active_) ||
      (toast_active && std::strcmp(last_toast_, st.toast) != 0);
  if (force_ || toast_changed) {
    if (toast_active) {
      drawToast(st);
    } else {
      drawRow(st, /*clear_first=*/true);
    }
  } else if (!toast_active &&
             (sec != last_sec_ || st.pb.volume_pct != last_volume_ ||
              st.battery_pct != last_battery_)) {
    drawRow(st, /*clear_first=*/false);
  }

  // Glyph and heart are suppressed while a toast owns the row: they would
  // punch holes through the message, and the restore path repaints them from
  // current state anyway.
  if (!toast_active) {
    if (force_ || toast_changed || st.pb.is_playing != last_playing_ ||
        glyph_.animating()) {
      glyph_.render(st.pb.is_playing, now_ms);
    }
    if (force_ || toast_changed || track_changed ||
        st.pb.liked != last_liked_ ||
        st.pb.liked_known != last_liked_known_ || heart_.animating()) {
      heart_.render(st.pb.liked_known, st.pb.liked, now_ms, STRIP_HEART_X,
                    STRIP_HEART_Y, pal.strip);
    }
  }

  setStr(last_track_, ID_LEN, st.pb.track_id);
  last_sec_ = sec;
  last_playing_ = st.pb.is_playing;
  last_liked_ = st.pb.liked;
  last_liked_known_ = st.pb.liked_known;
  last_volume_ = st.pb.volume_pct;
  last_battery_ = st.battery_pct;
  last_tint_ = tint;
  last_toast_active_ = toast_active;
  setStr(last_toast_, sizeof(last_toast_), st.toast);
  force_ = false;
}
