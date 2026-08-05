#include "Scenes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Anim.h"
#include "Theme.h"

namespace {

using theme::VIS_H;
using theme::VIS_W;

// Deterministic hashes. Every "random" value here is derived from stable
// inputs, so a scene looks identical on every run — which is what lets the
// visual tests assert on it at all.
uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (s && *s) {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}

uint32_t mix(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  return h;
}

float unit(uint32_t h) { return (h & 0xFFFF) / 65535.0f; }

uint16_t dim(uint16_t c, float f) { return anim::lerp565(theme::pal.bg, c, f); }

// ---------------------------------------------------------------------------
// Synthwave horizon: mode-7 grid, Outrun sun, stars.
// The sun sinks as the track plays, so the scene doubles as a progress read.
// ---------------------------------------------------------------------------
class SynthwaveScene : public Scene {
 public:
  void reset(uint32_t seed) override { seed_ = seed; }
  const char *name() const override { return "synthwave"; }

  void render(M5Canvas *cv, const SceneCtx &ctx) override {
    constexpr int HORIZON = 30;

    // Stars above the horizon.
    for (int i = 0; i < 18; ++i) {
      const uint32_t h = mix(seed_, i);
      const int x = static_cast<int>(unit(h) * VIS_W);
      const int y = static_cast<int>(unit(h >> 8) * (HORIZON - 4));
      const float tw = 0.55f + 0.45f * std::sin(ctx.clock * 1.7f + i);
      cv->drawPixel(x, y, dim(theme::pal.text, 0.25f + 0.35f * tw));
    }

    // Sun, sinking with progress. Drawn before the ground so the horizon can
    // cut it off cleanly.
    const int sun_cy =
        static_cast<int>(anim::lerp(16.0f, HORIZON + 10.0f, ctx.progress01));
    constexpr int SUN_R = 12;
    for (int dy = -SUN_R; dy <= SUN_R; ++dy) {
      const int y = sun_cy + dy;
      if (y < 0 || y >= HORIZON) continue;
      const int half =
          static_cast<int>(std::sqrt(std::max(0.0f, float(SUN_R * SUN_R - dy * dy))));
      // Outrun slits: gaps widen toward the bottom of the disc.
      const int band = dy + SUN_R;
      if (band > 10 && ((band / 2) % 2) == 0) continue;
      const float g = 1.0f - (float(dy + SUN_R) / (SUN_R * 2)) * 0.45f;
      cv->drawFastHLine(60 - half, y, half * 2, dim(ctx.tint, g));
    }

    // Ground.
    cv->fillRect(0, HORIZON, VIS_W, VIS_H - HORIZON, theme::pal.bg);
    cv->drawFastHLine(0, HORIZON, VIS_W, dim(ctx.tint, 0.9f));

    // Verticals converging on the vanishing point.
    for (int i = -5; i <= 5; ++i) {
      cv->drawLine(60 + i * 26, VIS_H, 60 + i * 2, HORIZON,
                   dim(ctx.tint, 0.32f));
    }

    // Horizontals, spaced by a squared term for perspective, scrolling toward
    // the viewer while playback runs.
    const float scroll = std::fmod(ctx.clock * 0.35f, 1.0f);
    for (int k = 0; k < 7; ++k) {
      float t = (k + scroll) / 7.0f;
      if (t > 1.0f) t -= 1.0f;
      const int y = HORIZON + static_cast<int>((VIS_H - HORIZON) * t * t);
      if (y <= HORIZON || y >= VIS_H) continue;
      cv->drawFastHLine(0, y, VIS_W, dim(ctx.tint, 0.20f + 0.45f * t));
    }
  }

 private:
  uint32_t seed_ = 0;
};

// ---------------------------------------------------------------------------
// Hyperspace: parallax stars streaming from a vanishing point.
// Speed follows real volume.
// ---------------------------------------------------------------------------
class StarfieldScene : public Scene {
 public:
  void reset(uint32_t seed) override { seed_ = seed; }
  const char *name() const override { return "starfield"; }

  void render(M5Canvas *cv, const SceneCtx &ctx) override {
    const float vol = ctx.volume_pct < 0 ? 0.7f : ctx.volume_pct / 100.0f;
    const float speed = 0.20f + 0.60f * vol;

    for (int i = 0; i < 80; ++i) {
      const uint32_t h = mix(seed_, i);
      // Fixed direction per star, so it always travels the same radial line.
      const float dirx = unit(h) * 2.0f - 1.0f;
      const float diry = unit(h >> 7) * 2.0f - 1.0f;
      const float phase = unit(h >> 15);

      // Depth runs 1 -> 0; the reciprocal is what produces real perspective
      // rather than the evenly scattered dots a linear radius gives.
      float z = 1.0f - std::fmod(phase + ctx.clock * speed, 1.0f);
      if (z < 0.10f) z = 0.10f;
      const float spread = (1.0f / z) - 1.0f;

      const int x = static_cast<int>(60.0f + dirx * spread * 15.0f);
      const int y = static_cast<int>(25.0f + diry * spread * 9.0f);
      if (x < 0 || x >= VIS_W || y < 0 || y >= VIS_H) continue;

      const float bright = std::fmin(1.0f, 0.18f + spread * 0.42f);
      const uint16_t c =
          dim(i % 6 == 0 ? ctx.tint : theme::pal.text, bright);

      // Near stars stretch into streaks along their own travel direction.
      const int len = static_cast<int>(std::fmin(6.0f, spread * 1.6f));
      if (len >= 2) {
        cv->drawLine(x, y, x - static_cast<int>(dirx * len),
                     y - static_cast<int>(diry * len * 0.6f), c);
      } else {
        cv->drawPixel(x, y, c);
      }
    }
  }

 private:
  uint32_t seed_ = 0;
};

// ---------------------------------------------------------------------------
// Neon city: silhouetted skyline, flickering windows, album-tinted skyglow.
// ---------------------------------------------------------------------------
class CityScene : public Scene {
 public:
  void reset(uint32_t seed) override { seed_ = seed; }
  const char *name() const override { return "city"; }

  void render(M5Canvas *cv, const SceneCtx &ctx) override {
    // Skyglow.
    for (int y = 0; y < VIS_H; ++y) {
      const float f = 1.0f - (float(y) / VIS_H);
      cv->drawFastHLine(0, y, VIS_W, dim(ctx.tint, 0.05f + 0.16f * f * f));
    }

    // Deterministic skyline, wide enough to scroll and wrap.
    constexpr int COUNT = 22;
    int widths[COUNT];
    int heights[COUNT];
    int total = 0;
    for (int b = 0; b < COUNT; ++b) {
      const uint32_t h = mix(seed_, b);
      widths[b] = 7 + static_cast<int>(unit(h) * 9);
      heights[b] = 12 + static_cast<int>(unit(h >> 9) * 30);
      total += widths[b];
    }

    // Slow parallax drift, so the scene is continuously in motion rather than
    // only changing when a window happens to re-roll.
    const float scroll = std::fmod(ctx.clock * 3.5f, static_cast<float>(total));

    // Windows re-roll on a coarse time bucket: cheap, stateless, and stable
    // enough that they twinkle rather than strobe.
    const int bucket = static_cast<int>(ctx.clock * 2.5f);

    for (int pass = 0; pass < 2; ++pass) {
      int x = static_cast<int>(-scroll) + pass * total;
      for (int b = 0; b < COUNT; ++b) {
        const int w = widths[b];
        const int ht = heights[b];
        const int top = VIS_H - ht;

        if (x + w >= 0 && x < VIS_W) {
          const uint32_t h = mix(seed_, b);
          cv->fillRect(x, top, w - 2, ht, theme::pal.bg);
          cv->drawRect(x, top, w - 2, ht, dim(ctx.tint, 0.30f));

          for (int wy = top + 3; wy < VIS_H - 3; wy += 4) {
            for (int wx = x + 2; wx < x + w - 4; wx += 4) {
              if (wx < 0 || wx >= VIS_W - 1) continue;
              const uint32_t wh = mix(mix(h, b * 977 + wy), bucket);
              if ((wh & 7) < 3) {
                cv->fillRect(wx, wy, 2, 2,
                             dim(ctx.tint, 0.55f + 0.45f * unit(wh >> 4)));
              }
            }
          }
        }
        x += w;
      }
    }
  }

 private:
  uint32_t seed_ = 0;
};

// ---------------------------------------------------------------------------
// Orbiting planet: the moon completes exactly one orbit per track, so its
// position genuinely is the progress.
// ---------------------------------------------------------------------------
class PlanetScene : public Scene {
 public:
  void reset(uint32_t seed) override { seed_ = seed; }
  const char *name() const override { return "planet"; }

  void render(M5Canvas *cv, const SceneCtx &ctx) override {
    for (int i = 0; i < 22; ++i) {
      const uint32_t h = mix(seed_, i + 400);
      const int x = static_cast<int>(unit(h) * VIS_W);
      const int y = static_cast<int>(unit(h >> 8) * VIS_H);
      const float tw = 0.5f + 0.5f * std::sin(ctx.clock * 1.3f + i * 0.9f);
      cv->drawPixel(x, y, dim(theme::pal.text, 0.15f + 0.30f * tw));
    }

    constexpr int CX = 52, CY = 25, R = 15;
    cv->fillCircle(CX, CY, R, dim(ctx.tint, 0.55f));

    // Banding, and a terminator so it reads as a lit sphere.
    for (int dy = -R; dy <= R; dy += 4) {
      const int half =
          static_cast<int>(std::sqrt(std::max(0.0f, float(R * R - dy * dy))));
      cv->drawFastHLine(CX - half, CY + dy, half * 2, dim(ctx.tint, 0.78f));
    }
    for (int dy = -R; dy <= R; ++dy) {
      const int half =
          static_cast<int>(std::sqrt(std::max(0.0f, float(R * R - dy * dy))));
      const int shade = half / 3;
      if (shade > 0) {
        cv->drawFastHLine(CX + half - shade, CY + dy, shade, dim(ctx.tint, 0.22f));
      }
    }

    // One orbit per track.
    const float ang = ctx.progress01 * 6.2831853f - 1.5707963f;
    const int mx = static_cast<int>(CX + std::cos(ang) * 34.0f);
    const int my = static_cast<int>(CY + std::sin(ang) * 20.0f);
    if (mx >= 2 && mx < VIS_W - 2 && my >= 2 && my < VIS_H - 2) {
      cv->fillCircle(mx, my, 3, dim(theme::pal.text, 0.85f));
      cv->drawCircle(mx, my, 4, dim(ctx.tint, 0.35f));
    }
  }

 private:
  uint32_t seed_ = 0;
};

SynthwaveScene g_synth;
StarfieldScene g_stars;
CityScene g_city;
PlanetScene g_planet;

}  // namespace

void ScenePanel::ensure() {
  if (cv_) return;
  cv_ = new M5Canvas(&M5.Display);
  cv_->setColorDepth(16);
  cv_->createSprite(theme::VIS_W, theme::VIS_H);

  scenes_[0] = &g_synth;
  scenes_[1] = &g_stars;
  scenes_[2] = &g_city;
  scenes_[3] = &g_planet;
  for (auto *s : scenes_) s->reset(0x5eed);
}

void ScenePanel::setTint(uint16_t tint) {
  if (have_tint_ && tint == tint_) return;
  tint_ = tint;
  have_tint_ = true;
  force_ = true;
}

void ScenePanel::onTrackChange(const char *track_id) {
  ensure();

#if defined(EMULATOR)
  // EMU_SCENE pins one scene, for inspection and for the visual tests.
  if (const char *forced = std::getenv("EMU_SCENE")) {
    current_ = std::atoi(forced) % SCENE_COUNT;
    scenes_[current_]->reset(mix(fnv1a(track_id), 0xA5A5));
    force_ = true;
    return;
  }
#endif

  const uint32_t h = fnv1a(track_id);
  int pick = static_cast<int>(h % SCENE_COUNT);
  // Never the same scene twice running, so rotation is always visible.
  if (pick == current_) pick = (pick + 1) % SCENE_COUNT;
  current_ = pick;

  scenes_[current_]->reset(mix(h, 0xA5A5));
  force_ = true;
}

const char *ScenePanel::currentName() const {
  return scenes_[current_] ? scenes_[current_]->name() : "";
}

void ScenePanel::render(bool playing, int volume_pct, uint32_t progress_ms,
                        uint32_t duration_ms, uint32_t now_ms) {
  ensure();

  const float dt =
      last_ms_ == 0 ? 0.016f : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;

  // One clock for every scene, advancing only while playback runs. Pausing
  // therefore freezes the whole panel without each scene handling it.
  if (playing) clock_ += dt;

  SceneCtx ctx;
  ctx.playing = playing;
  ctx.volume_pct = volume_pct;
  ctx.progress01 =
      duration_ms == 0 ? 0.0f
                       : std::fmin(1.0f, float(progress_ms) / duration_ms);
  ctx.clock = clock_;
  ctx.tint = have_tint_ ? tint_ : theme::pal.accent;

#if defined(EMULATOR)
  if (std::getenv("EMU_SCENE_DEBUG")) {
    static int n = 0;
    if ((n++ % 400) == 0) {
      std::fprintf(stderr, "[scene] %s clock=%.2f playing=%d prog=%.2f\n",
                   scenes_[current_]->name(), ctx.clock, (int)playing,
                   ctx.progress01);
    }
  }
#endif

  cv_->fillSprite(theme::pal.bg);
  scenes_[current_]->render(cv_, ctx);
  cv_->pushSprite(theme::VIS_X, theme::VIS_Y);
  force_ = false;
}
