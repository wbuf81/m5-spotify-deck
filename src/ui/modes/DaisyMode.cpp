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

  M5.Display.setFont(theme::fontTitle());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[2][WRAP_MAX_LINE];
  const int n = wrapText(st.pb.title, 200, lines, 2);
  int y = 12;
  for (int i = 0; i < n; ++i) {
    M5.Display.setCursor(10, y);
    M5.Display.print(lines[i]);
    y += M5.Display.fontHeight();
  }
  y += 4;
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 200, lines, 1);
  M5.Display.setCursor(10, y);
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

  daisy::DaisyAnim want;
  if (now_ms < wag_until_ms_ && wag_until_ms_ != 0) {
    want = daisy::Daisy_Wag;
  } else if (!st.pb.is_playing) {
    want = daisy::Daisy_Sleep;  // she still breathes: sleep has frames
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
