#include "NowPlayingScreen.h"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

#include "../art/ArtRenderer.h"
#include "Anim.h"
#include "TimeFormat.h"
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

// Sprite sizes are kept small enough that the heart's expanding ring cannot
// reach into the artwork on its left, and the glyph cannot reach the timecodes
// on either side of it.
constexpr int HEART_CV = 24;
constexpr int GLYPH_CV_W = 18;
constexpr int GLYPH_CV_H = 14;

constexpr uint32_t HEART_ANIM_MS = 420;
constexpr uint32_t GLYPH_ANIM_MS = 240;

// Two lobes and a point, scalable about its own centre. Cheaper and far more
// legible at this size than any font glyph would be.
void drawHeartScaled(M5Canvas *cv, float cx, float cy, float s, uint16_t color) {
  cv->fillCircle(static_cast<int>(cx - 2.5f * s), static_cast<int>(cy - 3.0f * s),
                 static_cast<int>(3.0f * s + 0.5f), color);
  cv->fillCircle(static_cast<int>(cx + 2.5f * s), static_cast<int>(cy - 3.0f * s),
                 static_cast<int>(3.0f * s + 0.5f), color);
  cv->fillTriangle(static_cast<int>(cx - 5.5f * s), static_cast<int>(cy - 2.0f * s),
                   static_cast<int>(cx + 5.5f * s), static_cast<int>(cy - 2.0f * s),
                   static_cast<int>(cx), static_cast<int>(cy + 5.0f * s), color);
}

// Morphs pause bars into a play triangle by drawing the glyph as a row of
// vertical columns and interpolating each column's height between the two
// shapes. Genuine morph rather than a crossfade, which the panel could not do
// anyway without alpha.
//
//   playness 0 = two bars, 1 = triangle
void drawGlyphMorph(M5Canvas *cv, float cx, float cy, float playness,
                    uint16_t color) {
  constexpr int W = 11;
  constexpr float H = 11.0f;
  const float x0 = cx - (W / 2.0f);

  for (int i = 0; i < W; ++i) {
    const float fx = static_cast<float>(i) / (W - 1);

    // Pause: full-height where a bar is, nothing in the gap or the tail.
    const float bars = (fx <= 0.30f || (fx >= 0.58f && fx <= 0.88f)) ? 1.0f : 0.0f;
    // Play: linearly tapering to a point on the right.
    const float tri = 1.0f - (fx * 0.95f);

    const float h = anim::lerp(bars, tri, playness) * H;
    if (h < 1.0f) continue;
    const int ih = static_cast<int>(h + 0.5f);
    cv->fillRect(static_cast<int>(x0 + i), static_cast<int>(cy - ih / 2.0f), 1,
                 ih, color);
  }
}

}  // namespace

void NowPlayingScreen::drawArtRegion(const AppState &st) {
  using namespace theme;
  if (drawArt(st.pb.art_path, ART_X, ART_Y, ART_SIZE)) {
    // Tie the visualiser to whatever is actually playing. Sampled from the
    // panel after the decode, so it works for any image the decoder accepted.
    vis_.setTint(sampleArtTint(st.pb.art_path, pal.accent));
  } else {
    // Missing or undecodable artwork must degrade, never blank the device.
    M5.Display.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, pal.bar_bg);
    M5.Display.setFont(theme::fontSmall());
    M5.Display.setTextColor(pal.dim);
    M5.Display.setCursor(ART_X + 6, ART_Y + ART_SIZE / 2 - 4);
    M5.Display.print("no artwork");
    vis_.setTint(pal.accent);
  }
}

void NowPlayingScreen::drawTextColumn(const AppState &st) {
  using namespace theme;

  constexpr int TITLE_ARTIST_GAP = 6;

  // Top-aligned now, not centred: the space below belongs to the visualiser,
  // so the old vertical centring would push the text down into it.
  M5.Display.fillRect(COL_X, COL_Y, COL_W, TEXT_H, pal.bg);

  // Line spacing comes from the font itself so changing face or size does not
  // silently break the vertical maths.
  M5.Display.setFont(fontTitle());
  const int title_lead = M5.Display.fontHeight();
  char lines[TITLE_LINES][MAX_LINE];
  const int n = wrapText(st.pb.title, COL_W, lines, TITLE_LINES);

  M5.Display.setFont(fontArtist());
  const int artist_lead = M5.Display.fontHeight();
  char alines[2][MAX_LINE];
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
  constexpr int FOOT_H = 14;
  const int foot_y = COL_Y + COL_H - FOOT_H;
  M5.Display.fillRect(COL_X, foot_y, COL_W, FOOT_H, pal.bg);

  // The heart is drawn by drawHeartRegion, which owns a sprite so it can
  // animate without tearing. Clearing here is enough; that call repaints it.
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
    M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);
  }

  M5.Display.setFont(theme::fontSmall());
  // Opaque text: every glyph cell repaints its own background, so the digits
  // update in place with no intermediate blank.
  M5.Display.setTextColor(pal.dim, pal.strip);

  char buf[16];
  char padded[16];

  // Fixed-width fields with a monospaced face keep pixel extents stable, so a
  // shrinking string (10:00 -> 9:59) leaves no residue behind.
  formatElapsed(st.pb.progress_ms, buf, sizeof(buf));
  std::snprintf(padded, sizeof(padded), "%-7s", buf);
  M5.Display.setCursor(BAR_X, TIME_Y + 2);
  M5.Display.print(padded);

  // Right-hand figure counts down. Showing total duration there looked frozen,
  // because it is — the leading minus makes "remaining" unambiguous.
  formatRemaining(st.pb.progress_ms, st.pb.duration_ms, buf, sizeof(buf));
  const int rw = M5.Display.textWidth("-00:00");
  std::snprintf(padded, sizeof(padded), "%7s", buf);
  M5.Display.setCursor(SCREEN_W - MARGIN - rw, TIME_Y + 2);
  M5.Display.print(padded);
}

void NowPlayingScreen::drawToastRow(const AppState &st) {
  using namespace theme;
  M5.Display.fillRect(0, ROW_Y, SCREEN_W, ROW_H, pal.strip);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(pal.warn, pal.strip);
  const int w = M5.Display.textWidth(st.toast);
  M5.Display.setCursor((SCREEN_W - w) / 2, TIME_Y + 2);
  M5.Display.print(st.toast);
}

void NowPlayingScreen::ensureSprites() {
  if (heart_cv_) return;
  heart_cv_ = new M5Canvas(&M5.Display);
  heart_cv_->setColorDepth(16);
  heart_cv_->createSprite(HEART_CV, HEART_CV);

  glyph_cv_ = new M5Canvas(&M5.Display);
  glyph_cv_->setColorDepth(16);
  glyph_cv_->createSprite(GLYPH_CV_W, GLYPH_CV_H);
}

void NowPlayingScreen::drawHeartRegion(const AppState &st, uint32_t now_ms) {
  using namespace theme;
  ensureSprites();

  constexpr int FOOT_H = 14;
  const int foot_y = COL_Y + COL_H - FOOT_H;
  // Positioned so the ring at full radius still clears the artwork's right edge.
  const int ox = COL_X - 7;
  const int oy = foot_y - 5;
  const float cx = static_cast<float>(COL_X + 5 - ox);
  const float cy = static_cast<float>(foot_y + 7 - oy);

  heart_cv_->fillSprite(pal.bg);

  // Saved-state unknown: draw nothing. A dim heart would assert "not liked",
  // which we cannot claim when the API declines to answer.
  if (!st.pb.liked_known) {
    heart_cv_->pushSprite(ox, oy);
    return;
  }

  float scale = 1.0f;
  uint16_t color = st.pb.liked ? pal.accent : pal.bar_bg;

  if (heart_anim_active_) {
    const float t = anim::phase(heart_anim_start_ms_, now_ms, HEART_ANIM_MS);
    const float p = anim::pulse(t);

    if (heart_anim_liking_) {
      // Overshoot and settle, with a ring blooming outward and fading into the
      // background as it goes.
      scale = 1.0f + 0.60f * p;
      color = anim::lerp565(pal.bar_bg, pal.accent, anim::easeOutCubic(t));

      const int r = static_cast<int>(anim::lerp(3.0f, 11.0f, anim::easeOutCubic(t)));
      const uint16_t ring = anim::lerp565(pal.accent, pal.bg, t);
      heart_cv_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r, ring);
      if (r > 1) {
        heart_cv_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r - 1, ring);
      }
    } else {
      // Unliking: a smaller inward dip, draining back to the inactive colour.
      scale = 1.0f - 0.35f * p;
      color = anim::lerp565(pal.accent, pal.bar_bg, anim::easeOutCubic(t));
    }

    if (t >= 1.0f) heart_anim_active_ = false;
  }

  drawHeartScaled(heart_cv_, cx, cy, scale, color);
  heart_cv_->pushSprite(ox, oy);
}

void NowPlayingScreen::drawGlyphRegion(const AppState &st, uint32_t now_ms) {
  using namespace theme;
  ensureSprites();

  const int ox = SCREEN_W / 2 - (GLYPH_CV_W / 2);
  const int oy = TIME_Y - 2;

  glyph_cv_->fillSprite(pal.strip);

  float playness = st.pb.is_playing ? 1.0f : 0.0f;
  if (glyph_anim_active_) {
    const float t =
        anim::easeInOutCubic(anim::phase(glyph_anim_start_ms_, now_ms, GLYPH_ANIM_MS));
    playness = glyph_anim_to_playing_ ? t : (1.0f - t);
    if (anim::phase(glyph_anim_start_ms_, now_ms, GLYPH_ANIM_MS) >= 1.0f) {
      glyph_anim_active_ = false;
    }
  }

  drawGlyphMorph(glyph_cv_, GLYPH_CV_W / 2.0f, GLYPH_CV_H / 2.0f, playness,
                 pal.text);
  glyph_cv_->pushSprite(ox, oy);
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
    // A new song gets a new scene.
    vis_.onTrackChange(st.pb.track_id);
  }
  // Start the heart animation on a real like/unlike, not on the first sight of
  // a track or on saved-state simply becoming known — neither is a user action
  // and animating them would make the display twitch on every track change.
  if (!force_ && !track_changed && last_liked_known_ && st.pb.liked_known &&
      st.pb.liked != last_liked_) {
    heart_anim_active_ = true;
    heart_anim_liking_ = st.pb.liked;
    heart_anim_start_ms_ = now_ms;
  }

  if (force_ || track_changed || st.pb.liked != last_liked_ ||
      st.pb.liked_known != last_liked_known_ ||
      st.pb.volume_pct != last_volume_) {
    drawColumnFoot(st);
  }

  if (force_ || track_changed || st.pb.liked != last_liked_ ||
      st.pb.liked_known != last_liked_known_ || heart_anim_active_) {
    drawHeartRegion(st, now_ms);
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
      // The toast occupies the whole row, glyph included. Restoring the row
      // means restoring both halves of it — redrawing only the times leaves the
      // play glyph erased until the next play/pause, which looks like the
      // button vanished.
      drawTimeRow(st, /*clear_first=*/true);
      drawGlyphRegion(st, now_ms);
    }
  } else if (!toast_active && progress_sec != last_progress_sec_) {
    drawTimeRow(st, /*clear_first=*/false);
  }

  // Runs every frame: it is continuously animated, and it is the one element
  // whose whole job is to look alive.
  if (force_) {
    vis_.invalidate();
  }
  vis_.render(st.pb.is_playing, st.pb.volume_pct, st.pb.progress_ms,
              st.pb.duration_ms, now_ms);

  if (!force_ && st.pb.is_playing != last_playing_) {
    glyph_anim_active_ = true;
    glyph_anim_to_playing_ = st.pb.is_playing;
    glyph_anim_start_ms_ = now_ms;
  }

  // Suppressed while a toast is up: the glyph would punch a hole through the
  // message. The restore path above repaints it from current state anyway.
  if (!toast_active &&
      (force_ || st.pb.is_playing != last_playing_ || glyph_anim_active_)) {
    drawGlyphRegion(st, now_ms);
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
