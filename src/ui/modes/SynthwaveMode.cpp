#include "SynthwaveMode.h"

#include <cmath>
#include <cstdio>

#include "../../art/ArtRenderer.h"
#include "../Anim.h"
#include "../TextWrap.h"
#include "../Theme.h"
#include "../TimeFormat.h"

namespace {
constexpr int HORIZON = 150;
constexpr int SUN_D = 104;  // sun sprite is square, masked to a circle
constexpr int SUN_CX = 160;
}  // namespace

void SynthwaveMode::release() {
  if (!sun_) return;
  sun_->deleteSprite();
  delete sun_;
  sun_ = nullptr;
}

void SynthwaveMode::enter(const AppState &st, const ViewCtx &ctx) {
  M5.Display.fillScreen(theme::pal.bg);

  // The sun is the cover, masked to a disc and slit like an Outrun sunset.
  // Pre-rendered once because it only moves; re-decoding per frame as it sinks
  // would be absurd.
  if (!sun_) {
    sun_ = new M5Canvas(&M5.Display);
    sun_->setColorDepth(16);
    if (!sun_->createSprite(SUN_D, SUN_D)) {
      delete sun_;
      sun_ = nullptr;
    }
  }
  if (sun_) {
    sun_->fillSprite(theme::pal.bg);
    if (drawArtInto(sun_, ctx.art_path, 0, 0, SUN_D)) {
      const int r = SUN_D / 2;
      for (int y = 0; y < SUN_D; ++y) {
        for (int x = 0; x < SUN_D; ++x) {
          const int dx = x - r, dy = y - r;
          const bool outside = (dx * dx + dy * dy) > (r - 1) * (r - 1);
          // Slits widen toward the base of the disc.
          const bool slit = y > SUN_D / 2 && ((y / 3) % 2) == 0 &&
                            y > (SUN_D / 2) + ((SUN_D / 2) - y % SUN_D) / 4;
          if (outside || slit) sun_->drawPixel(x, y, theme::pal.bg);
        }
      }
    } else {
      sun_->fillCircle(SUN_D / 2, SUN_D / 2, SUN_D / 2 - 1, ctx.tint);
    }
  }

  // Stars above the horizon.
  for (int i = 0; i < 60; ++i) {
    const int x = (i * 5779) % 320;
    const int y = (i * 2411) % (HORIZON - 10);
    M5.Display.drawPixel(x, y, anim::lerp565(theme::pal.bg, theme::pal.text,
                                             0.2f + 0.5f * ((i % 5) / 5.0f)));
  }

  // Title in the sky, top-left, clear of the sun's column. The transport used
  // to live under the horizon; that space belongs to the shared strip now.
  M5.Display.setFont(theme::fontArtist());
  M5.Display.setTextColor(theme::pal.text, theme::pal.bg);
  char lines[1][WRAP_MAX_LINE];
  wrapText(st.pb.title, 210, lines, 1);
  M5.Display.setCursor(10, 8);
  M5.Display.print(lines[0]);
  M5.Display.setFont(theme::fontSmall());
  M5.Display.setTextColor(theme::pal.dim, theme::pal.bg);
  wrapText(st.pb.artist, 210, lines, 1);
  M5.Display.setCursor(10, 26);
  M5.Display.print(lines[0]);

  last_sun_y_ = -1000;
  last_ms_ = 0;
}

void SynthwaveMode::tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) {
  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;
  if (st.pb.is_playing) clock_ += dt;

  const float p = st.pb.duration_ms == 0
                      ? 0.0f
                      : std::fmin(1.0f, float(st.pb.progress_ms) / st.pb.duration_ms);

  // The sun sinks as the track plays, so the scene reads as progress.
  const int sun_y = static_cast<int>(anim::lerp(28.0f, HORIZON - 18.0f, p));
  if (sun_ && sun_y != last_sun_y_) {
    if (last_sun_y_ > -999) {
      M5.Display.fillRect(SUN_CX - SUN_D / 2, last_sun_y_, SUN_D,
                          HORIZON - last_sun_y_ > 0 ? SUN_D : SUN_D,
                          theme::pal.bg);
    }
    last_sun_y_ = sun_y;
    M5.Display.setClipRect(0, 0, 320, HORIZON);
    sun_->pushSprite(SUN_CX - SUN_D / 2, sun_y);
    M5.Display.clearClipRect();
  }

  // Ground: horizon line plus a perspective grid scrolling toward the viewer.
  M5.Display.fillRect(0, HORIZON, 320, theme::STRIP_Y - HORIZON, theme::pal.bg);
  M5.Display.drawFastHLine(0, HORIZON, 320, ctx.tint);
  for (int i = -7; i <= 7; ++i) {
    M5.Display.drawLine(160 + i * 46, theme::STRIP_Y - 1, 160 + i * 5, HORIZON,
                        anim::lerp565(theme::pal.bg, ctx.tint, 0.30f));
  }
  const float scroll = std::fmod(clock_ * 0.4f, 1.0f);
  for (int k = 0; k < 7; ++k) {
    float t = (k + scroll) / 7.0f;
    if (t > 1.0f) t -= 1.0f;
    const int y = HORIZON + static_cast<int>((theme::STRIP_Y - HORIZON) * t * t);
    if (y <= HORIZON || y >= theme::STRIP_Y) continue;
    M5.Display.drawFastHLine(0, y, 320,
                             anim::lerp565(theme::pal.bg, ctx.tint, 0.2f + 0.5f * t));
  }

}
