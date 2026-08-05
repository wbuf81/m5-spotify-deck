#pragma once

// Small animation helpers.

#include <M5Unified.h>

#include <cmath>
#include <cstdint>

namespace anim {

// Progress through an animation, clamped to 0..1.
inline float phase(uint32_t start_ms, uint32_t now_ms, uint32_t duration_ms) {
  if (duration_ms == 0) return 1.0f;
  const uint32_t elapsed = now_ms - start_ms;
  if (elapsed >= duration_ms) return 1.0f;
  return static_cast<float>(elapsed) / static_cast<float>(duration_ms);
}

inline float easeOutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - (u * u * u);
}

inline float easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t
                  : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// A 0 -> 1 -> 0 pulse, for overshoot-and-settle motion.
inline float pulse(float t) { return std::sin(t * 3.14159265f); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Blends two RGB565 values. There is no alpha channel on the panel, so
// "fading out" means interpolating toward the background colour.
inline uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t <= 0.0f) return a;
  if (t >= 1.0f) return b;
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = static_cast<int>(ar + (br - ar) * t);
  const int g = static_cast<int>(ag + (bg - ag) * t);
  const int bl = static_cast<int>(ab + (bb - ab) * t);
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

}  // namespace anim
