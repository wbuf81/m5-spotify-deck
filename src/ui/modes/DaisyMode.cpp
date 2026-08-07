#include "DaisyMode.h"
#include "../../core/Hash.h"

#include <cstdio>
#include <cstring>

#include "../../art/ArtRenderer.h"
#include "../Anim.h"
#include "../daisy/DaisySprite.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {

constexpr int SCALE = 3;
constexpr int DAISY_W = daisy::SPRITE_COLS * SCALE;  // 165
constexpr int DAISY_H = daisy::SPRITE_ROWS * SCALE;  // 123

// The sprite box ends just above the floor line. Her gallop frames reach the
// bottom row of the box at full stride, so extended paws brush the line and
// the gathered frames ride higher — which is what a gallop looks like. The
// first placement pushed the box 14px past the line, and its background fill
// erased the line and clipped the progress bar on every full redraw.
constexpr int FLOOR_Y = 188;  // just above the shared strip
constexpr int DAISY_X = 24;
constexpr int DAISY_Y = FLOOR_Y - DAISY_H - 1;

constexpr int ART_SIZE_PX = 88;
constexpr int ART_PX = 320 - ART_SIZE_PX - 10;  // 222
constexpr int ART_PY = 10;


}  // namespace

daisy::DaisyAnim DaisyMode::pickPlayingAnim(const AppState &st) const {
  // Loud music earns zoomies. Below that, the track hash picks her activity,
  // so a song always brings the same one — same rule as scene and view
  // selection, and it makes her feel like she has opinions about songs.
  if (st.pb.volume_pct >= 85) return daisy::Daisy_Zoomies;
  static const daisy::DaisyAnim POOL[] = {
      daisy::Daisy_Trot, daisy::Daisy_Sniff, daisy::Daisy_Dig,
      daisy::Daisy_Wag};
  return POOL[seed_ % 4];
}

void DaisyMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);
  seed_ = fnv1a(st.pb.track_id);

  // Album art as a poster on the wall, tint-framed like the classic view.
  if (drawArt(ctx.art_path, ART_PX, ART_PY, ART_SIZE_PX)) {
    const uint16_t glow = anim::lerp565(ctx.tint, 0xFFFF, 0.25f);
    M5.Display.drawRect(ART_PX, ART_PY, ART_SIZE_PX, ART_SIZE_PX, glow);
    M5.Display.drawRect(ART_PX - 1, ART_PY - 1, ART_SIZE_PX + 2,
                        ART_SIZE_PX + 2,
                        anim::lerp565(theme::pal.bg, ctx.tint, 0.5f));
  } else {
    M5.Display.fillRect(ART_PX, ART_PY, ART_SIZE_PX, ART_SIZE_PX,
                        theme::pal.bar_bg);
  }

  // One line each: her sprite box now reaches y=63 (the floor moved up when
  // the shared strip arrived), and a two-line title put the artist inside it —
  // the box's background fill was erasing half of "Radiohead".
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 200, lines, 1);
  M5.Display.setCursor(10, 10);
  M5.Display.print(lines[0]);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 200, lines, 1);
  M5.Display.setCursor(10, 28);
  M5.Display.print(lines[0]);

  // The floor she stands on, with a few tufts so it reads as ground.
  M5.Display.drawFastHLine(0, FLOOR_Y, 320, anim::lerp565(theme::pal.bg, ctx.tint, 0.6f));
  for (int x = 12; x < 320; x += 46) {
    M5.Display.drawFastVLine(x, FLOOR_Y - 3, 3, anim::lerp565(theme::pal.bg, ctx.tint, 0.4f));
    M5.Display.drawFastVLine(x + 3, FLOOR_Y - 2, 2, anim::lerp565(theme::pal.bg, ctx.tint, 0.3f));
  }

  frame_ = -1;  // force a full sprite draw on the first tick
  last_liked_ = st.pb.liked;
  last_liked_known_ = st.pb.liked_known;
  wag_until_ms_ = 0;
}

void DaisyMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  // A real like while she is on screen earns a wag burst, same trigger rule as
  // the classic heart: only a genuine toggle, never first sight of a track.
  if (last_liked_known_ && st.pb.liked_known && st.pb.liked != last_liked_) {
    wag_until_ms_ = now_ms + 3000;
  }
  last_liked_ = st.pb.liked;
  last_liked_known_ = st.pb.liked_known;

  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;
  if (st.pb.is_playing) clock_ += dt;

  // The ball: for 2.2 seconds of every 34, it bounces across the floor. Its
  // whole flight is a function of the clock, so pause freezes it mid-hop.
  const float cycle = std::fmod(clock_, 34.0f);
  const bool ball_flying = cycle < 2.2f;
  if (ball_flying) {
    const float t = cycle / 2.2f;
    const int bx = -8 + static_cast<int>(t * 336.0f);
    // Three decaying hops.
    const float hop = std::fabs(std::sin(t * 3.0f * 3.14159f));
    const int by = FLOOR_Y - 5 - static_cast<int>(hop * (44.0f - t * 22.0f));
    if (bx != ball_last_x_) {
      if (ball_last_x_ > -999) {
        M5.Display.fillRect(ball_last_x_ - 5, ball_last_y_ - 5, 11, 11,
                            theme::pal.bg);
        // Restore the floor line where the erase crossed it.
        M5.Display.drawFastHLine(ball_last_x_ - 5, FLOOR_Y, 11,
                                 anim::lerp565(theme::pal.bg, ctx.tint, 0.6f));
      }
      M5.Display.fillCircle(bx, by, 4, M5.Display.color565(0xD8, 0x40, 0x38));
      M5.Display.fillCircle(bx - 1, by - 1, 1,
                            M5.Display.color565(0xFF, 0xA0, 0x98));
      ball_last_x_ = bx;
      ball_last_y_ = by;
    }
  } else if (ball_last_x_ > -999) {
    M5.Display.fillRect(ball_last_x_ - 5, ball_last_y_ - 5, 11, 11,
                        theme::pal.bg);
    M5.Display.drawFastHLine(ball_last_x_ - 5, FLOOR_Y, 11,
                             anim::lerp565(theme::pal.bg, ctx.tint, 0.6f));
    ball_last_x_ = -1000;
  }

  // Falling asleep is a sequence, not a cut: drowsy, one yawn, then sleep.
  if (!st.pb.is_playing && pause_started_ms_ == 0) pause_started_ms_ = now_ms;
  if (st.pb.is_playing) pause_started_ms_ = 0;

  daisy::DaisyAnim want;
  if (now_ms < wag_until_ms_ && wag_until_ms_ != 0) {
    want = daisy::Daisy_Wag;
  } else if (!st.pb.is_playing) {
    const uint32_t asleep_t = now_ms - pause_started_ms_;
    want = asleep_t < 5000   ? daisy::Daisy_Drowsy
           : asleep_t < 6800 ? daisy::Daisy_Yawn
                             : daisy::Daisy_Sleep;
  } else if (ball_flying) {
    want = daisy::Daisy_Alert;  // she watches the ball go by
  } else {
    want = pickPlayingAnim(st);
  }

  const int f = daisy::frameAt(want, now_ms);
  if (want != anim_ || f != frame_) {
    daisy::drawDiff(anim_, frame_, want, f, DAISY_X, DAISY_Y, SCALE,
                    theme::pal.bg);
    anim_ = want;
    frame_ = f;
  }
}
