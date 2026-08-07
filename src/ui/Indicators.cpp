#include "Indicators.h"

#include <cmath>

#include "../net/NetLog.h"
#include "Anim.h"
#include "Theme.h"

namespace {

// Sprite sizes are held down so the heart's expanding ring cannot reach into
// the artwork on its left, and the glyph cannot reach the timecodes either
// side of it.
constexpr int HEART_CV = 24;
constexpr int GLYPH_CV_W = 18;
constexpr int GLYPH_CV_H = 14;

constexpr uint32_t HEART_ANIM_MS = 420;
constexpr uint32_t GLYPH_ANIM_MS = 240;

constexpr int FOOT_H = 14;

// Two lobes and a point, scalable about its own centre. Cheaper and far more
// legible at this size than any font glyph.
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
// shapes. A genuine morph rather than a crossfade, which the panel could not do
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
    const float bars = (fx <= 0.30f || (fx >= 0.58f && fx <= 0.88f)) ? 1.0f : 0.0f;
    const float tri = 1.0f - (fx * 0.95f);

    const float h = anim::lerp(bars, tri, playness) * H;
    if (h < 1.0f) continue;
    const int ih = static_cast<int>(h + 0.5f);
    cv->fillRect(static_cast<int>(x0 + i), static_cast<int>(cy - ih / 2.0f), 1,
                 ih, color);
  }
}

M5Canvas *makeSprite(int w, int h) {
  auto *cv = new M5Canvas(&M5.Display);
  cv->setColorDepth(16);
  if (!cv->createSprite(w, h)) {
    // Out of contiguous heap. Returning null makes the caller skip drawing,
    // which loses an indicator rather than crashing the device.
    NETLOG("indicator sprite %dx%d FAILED to allocate", w, h);
    delete cv;
    return nullptr;
  }
  return cv;
}

void freeSprite(M5Canvas **cv) {
  if (!*cv) return;
  (*cv)->deleteSprite();
  delete *cv;
  *cv = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------

void HeartIndicator::trigger(bool liking, uint32_t now_ms) {
  anim_ = true;
  liking_ = liking;
  start_ms_ = now_ms;
}

void HeartIndicator::release() { freeSprite(&cv_); }

void HeartIndicator::render(bool known, bool liked, uint32_t now_ms, int ox,
                            int oy, uint16_t bg) {
  using namespace theme;
  if (!cv_) cv_ = makeSprite(HEART_CV, HEART_CV);
  if (!cv_) return;

  // Centre of the sprite; the bloom ring at full radius stays inside it.
  const float cx = HEART_CV / 2.0f;
  const float cy = HEART_CV / 2.0f;

  cv_->fillSprite(bg);

  if (!known) {
    cv_->pushSprite(ox, oy);
    return;
  }

  float scale = 1.0f;
  uint16_t color = liked ? pal.accent : pal.bar_bg;

  if (anim_) {
    const float t = anim::phase(start_ms_, now_ms, HEART_ANIM_MS);
    const float p = anim::pulse(t);

    if (liking_) {
      // Overshoot and settle, with a ring blooming outward and fading into the
      // background as it goes.
      scale = 1.0f + 0.60f * p;
      color = anim::lerp565(pal.bar_bg, pal.accent, anim::easeOutCubic(t));

      const int r = static_cast<int>(anim::lerp(3.0f, 11.0f, anim::easeOutCubic(t)));
      const uint16_t ring = anim::lerp565(pal.accent, bg, t);
      cv_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r, ring);
      if (r > 1) cv_->drawCircle(static_cast<int>(cx), static_cast<int>(cy), r - 1, ring);
    } else {
      // Unliking: a smaller inward dip, draining back to the inactive colour.
      scale = 1.0f - 0.35f * p;
      color = anim::lerp565(pal.accent, pal.bar_bg, anim::easeOutCubic(t));
    }

    if (t >= 1.0f) anim_ = false;
  }

  drawHeartScaled(cv_, cx, cy, scale, color);
  cv_->pushSprite(ox, oy);
}

// ---------------------------------------------------------------------------

void PlayGlyph::trigger(bool to_playing, uint32_t now_ms) {
  anim_ = true;
  to_playing_ = to_playing;
  start_ms_ = now_ms;
}

void PlayGlyph::release() { freeSprite(&cv_); }

void PlayGlyph::render(bool playing, uint32_t now_ms) {
  using namespace theme;
  if (!cv_) cv_ = makeSprite(GLYPH_CV_W, GLYPH_CV_H);
  if (!cv_) return;

  const int ox = SCREEN_W / 2 - (GLYPH_CV_W / 2);
  const int oy = TIME_Y - 2;

  cv_->fillSprite(pal.strip);

  float playness = playing ? 1.0f : 0.0f;
  if (anim_) {
    const float raw = anim::phase(start_ms_, now_ms, GLYPH_ANIM_MS);
    const float t = anim::easeInOutCubic(raw);
    playness = to_playing_ ? t : (1.0f - t);
    if (raw >= 1.0f) anim_ = false;
  }

  drawGlyphMorph(cv_, GLYPH_CV_W / 2.0f, GLYPH_CV_H / 2.0f, playness, pal.text);
  cv_->pushSprite(ox, oy);
}
