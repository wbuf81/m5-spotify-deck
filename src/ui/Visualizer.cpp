#include "Visualizer.h"

#include <cmath>

#include "Anim.h"
#include "Theme.h"

namespace {

// Named to avoid colliding with theme::BAR_W, the progress bar's width.
constexpr int VIS_BAR_W = 6;
constexpr int VIS_BAR_GAP = 2;

// Attack fast, decay slow. Equal rates look like noise; asymmetry is what makes
// bars read as responding to something rather than jittering.
constexpr float ATTACK = 0.45f;
constexpr float DECAY = 0.10f;

// Layered sines at incommensurate rates, so the pattern does not visibly repeat.
float targetFor(int i, float t) {
  const float a = 0.5f + 0.5f * std::sin(t * (1.10f + i * 0.13f) + i * 2.10f);
  const float b = 0.5f + 0.5f * std::sin(t * (0.43f + i * 0.05f) + i * 0.70f);
  const float envelope = 0.45f + 0.55f * b;

  // Gentle centre bias: a flat-topped block looks like a bug, a slight arch
  // looks intentional.
  const float mid = 1.0f - std::fabs((i / 14.0f) - 0.5f) * 0.7f;
  return a * envelope * mid;
}

}  // namespace

void Visualizer::setTint(uint16_t tint) {
  if (have_tint_ && tint == tint_) return;
  tint_ = tint;
  have_tint_ = true;
  force_ = true;
}

void Visualizer::ensureSprite() {
  if (cv_) return;
  cv_ = new M5Canvas(&M5.Display);
  cv_->setColorDepth(16);
  cv_->createSprite(theme::VIS_W, theme::VIS_H);
}

void Visualizer::render(bool playing, int volume_pct, uint32_t now_ms) {
  using namespace theme;
  ensureSprite();

  const float dt =
      last_ms_ == 0 ? 0.016f
                    : std::fmin(0.1f, (now_ms - last_ms_) / 1000.0f);
  last_ms_ = now_ms;
  const float t = now_ms / 1000.0f;

  // Volume scales amplitude, so turning it down visibly calms the display.
  const float vol = volume_pct < 0 ? 0.7f : (volume_pct / 100.0f);
  const float amp = 0.30f + 0.70f * vol;

  const uint16_t tint = have_tint_ ? tint_ : pal.accent;

  cv_->fillSprite(pal.bg);

  for (int i = 0; i < BARS; ++i) {
    const float target = playing ? targetFor(i, t) * amp : 0.0f;

    // Frame-rate independent smoothing, so the motion looks the same on the
    // host at 200fps and on the device at 30.
    const float rate = target > level_[i] ? ATTACK : DECAY;
    const float k = 1.0f - std::pow(1.0f - rate, dt * 60.0f);
    level_[i] += (target - level_[i]) * k;

    // A 2px floor keeps a resting baseline visible when paused, instead of the
    // region looking empty or broken.
    const int h = 2 + static_cast<int>(level_[i] * (VIS_H - 4));
    const int x = i * (VIS_BAR_W + VIS_BAR_GAP);
    const int y = VIS_H - h;

    // Vertical gradient: bright at the tip, fading into the background at the
    // base, so tall bars read as energetic rather than as solid blocks.
    for (int row = 0; row < h; ++row) {
      const float f = h <= 1 ? 0.0f : static_cast<float>(row) / (h - 1);
      const uint16_t c = anim::lerp565(tint, pal.bar_bg, 1.0f - f);
      cv_->drawFastHLine(x, y + row, VIS_BAR_W, c);
    }
  }

  cv_->pushSprite(VIS_X, VIS_Y);
  force_ = false;
}
