#include "DaisySprite.h"

#include <M5Unified.h>

#include "../Theme.h"

namespace daisy {

namespace {

// Palette entry with the screen dim factor applied, so Daisy dims with the
// rest of the UI instead of glowing in a dark room.
uint16_t shade(uint16_t c, float f) {
  if (f >= 0.999f) return c;
  const int r = static_cast<int>(((c >> 11) & 0x1F) * f);
  const int g = static_cast<int>(((c >> 5) & 0x3F) * f);
  const int b = static_cast<int>((c & 0x1F) * f);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}  // namespace

int frameAt(DaisyAnim anim, uint32_t t_ms) {
  const AnimData &a = ANIMS[anim];
  return static_cast<int>((t_ms / a.frame_ms) % a.frame_count);
}

void draw(DaisyAnim anim, int frame, int x, int y, int scale, uint16_t bg) {
  const AnimData &a = ANIMS[anim];
  const uint8_t *px = a.frames[frame % a.frame_count];
  const float f = theme::dimFactor();

  M5.Display.startWrite();
  for (int cy = 0; cy < SPRITE_ROWS; ++cy) {
    // Runs of equal colour merge into one fillRect: pixel art is mostly runs,
    // so this cuts the draw calls by roughly 5x.
    int run_start = 0;
    uint8_t run_idx = px[cy * SPRITE_COLS];
    for (int cx = 1; cx <= SPRITE_COLS; ++cx) {
      const uint8_t idx =
          (cx < SPRITE_COLS) ? px[cy * SPRITE_COLS + cx] : 0xFF;
      if (idx == run_idx) continue;
      const uint16_t c =
          run_idx == 0 ? bg : shade(a.palette[run_idx], f);
      M5.Display.fillRect(x + run_start * scale, y + cy * scale,
                          (cx - run_start) * scale, scale, c);
      run_start = cx;
      run_idx = idx;
    }
  }
  M5.Display.endWrite();
}

void drawDiff(DaisyAnim prev_anim, int prev_frame, DaisyAnim anim, int frame,
              int x, int y, int scale, uint16_t bg) {
  if (prev_frame < 0 || prev_anim != anim) {
    draw(anim, frame, x, y, scale, bg);
    return;
  }
  const AnimData &a = ANIMS[anim];
  const uint8_t *now = a.frames[frame % a.frame_count];
  const uint8_t *was = a.frames[prev_frame % a.frame_count];
  if (now == was) return;
  const float f = theme::dimFactor();

  M5.Display.startWrite();
  for (int cy = 0; cy < SPRITE_ROWS; ++cy) {
    for (int cx = 0; cx < SPRITE_COLS; ++cx) {
      const uint8_t idx = now[cy * SPRITE_COLS + cx];
      if (idx == was[cy * SPRITE_COLS + cx]) continue;
      const uint16_t c = idx == 0 ? bg : shade(a.palette[idx], f);
      M5.Display.fillRect(x + cx * scale, y + cy * scale, scale, scale, c);
    }
  }
  M5.Display.endWrite();
}

}  // namespace daisy
