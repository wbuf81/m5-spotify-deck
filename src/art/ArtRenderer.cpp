#include "ArtRenderer.h"

#include "../net/NetLog.h"
#include "../ui/Theme.h"

#include <M5Unified.h>

#include <cstdio>
#include <memory>
#include <new>

namespace {

// Streams the JPEG off disk instead of buffering it.
//
// Two earlier attempts failed on hardware and are worth recording:
//
//   1. std::vector::resize per decode. Covers are ~46KB, we decode twice per
//      album (tint, then draw), and once WiFi and TLS have fragmented the heap
//      a 46KB contiguous request fails even with 135KB free. bad_alloc with no
//      handler becomes abort(), so the board reboot-looped.
//   2. One 72KB buffer reserved at boot. That removed the churn but took the
//      memory permanently: free heap fell 256KB -> 182KB and mbedTLS could no
//      longer complete a handshake. The crash simply moved to the net task.
//
// LovyanGFX's default data wrapper reads through stdio, and SD is mounted at
// /sd via the ESP-IDF VFS, so drawJpgFile decodes straight from the card with
// no large allocation anywhere. The same call works on the host.
bool decodeInto(LovyanGFX *dst, const char *path, int x, int y, int size) {
  // scale 0.0 asks LovyanGFX to fit maxWidth/maxHeight, which is what makes the
  // non-power-of-two 300 -> 176 reduction work without hardcoding the source
  // dimensions.
  return dst->drawJpgFile(path, x, y, size, size, 0, 0, 0.0f, 0.0f);
}

}  // namespace

// Kept as a no-op: nothing is reserved any more, and the call site documents
// that the decision was deliberate rather than forgotten.
void initArtBuffer() {}

bool drawArtInto(LovyanGFX *dst, const char *path, int x, int y, int size) {
  if (!dst || !path || path[0] == '\0') return false;
  return decodeInto(dst, path, x, y, size);
}

bool drawArt(const char *path, int x, int y, int size) {
  if (!path || path[0] == '\0') return false;

#if defined(EMULATOR)
  // On hardware the backlight dims the artwork along with everything else. A
  // desktop window has none, so without this the art stays at full brightness
  // while the rest of the UI dims — which misrepresents what the device does at
  // the exact moment you are trying to judge it. Cost is a 62KB scratch sprite,
  // which is nothing on a host and is why this is emulator-only.
  const float f = theme::dimFactor();
  if (f < 0.99f) {
    M5Canvas tmp(&M5.Display);
    tmp.setColorDepth(16);
    if (tmp.createSprite(size, size)) {
      if (!decodeInto(&tmp, path, 0, 0, size)) {
        tmp.deleteSprite();
        return false;
      }
      for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
          const uint16_t c = tmp.readPixel(px, py);
          const int r = static_cast<int>((((c >> 11) & 0x1F) << 3) * f);
          const int g = static_cast<int>((((c >> 5) & 0x3F) << 2) * f);
          const int b = static_cast<int>(((c & 0x1F) << 3) * f);
          tmp.drawPixel(px, py, M5.Display.color565(r, g, b));
        }
      }
      tmp.pushSprite(x, y);
      tmp.deleteSprite();
      return true;
    }
  }
#endif

  return decodeInto(&M5.Display, path, x, y, size);
}

uint16_t sampleArtTint(const char *path, uint16_t fallback) {
  if (!path || path[0] == '\0') return fallback;

  // Decode a thumbnail into an off-screen sprite and sample that, rather than
  // reading pixels back off the panel. ILI9342C readback is slow over SPI and
  // unreliable on some units; sprite reads are plain memory. A second decode at
  // this size is cheap next to that risk.
  constexpr int TH = 40;
  M5Canvas thumb(&M5.Display);
  thumb.setColorDepth(16);
  if (!thumb.createSprite(TH, TH)) return fallback;
  thumb.fillSprite(0);

  if (!decodeInto(&thumb, path, 0, 0, TH)) {
    thumb.deleteSprite();
    return fallback;
  }

  uint16_t best = fallback;
  float best_score = 0.0f;

  for (int py = 0; py < TH; ++py) {
    for (int px = 0; px < TH; ++px) {
      const uint16_t c = thumb.readPixel(px, py);
      const int r = ((c >> 11) & 0x1F) << 3;
      const int g = ((c >> 5) & 0x3F) << 2;
      const int b = (c & 0x1F) << 3;

      const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      if (mx == 0) continue;

      const float sat = static_cast<float>(mx - mn) / mx;
      const float val = mx / 255.0f;

      // Saturation weighted above value, so this does not simply pick whatever
      // is brightest — on most covers that is a white or a blown highlight.
      const float score = (sat * sat) * (0.35f + 0.65f * val);
      if (score > best_score) {
        best_score = score;
        best = c;
      }
    }
  }

  thumb.deleteSprite();

  // Monochrome sleeves and dark photography should not yield a muddy tint.
  if (best_score < 0.06f) return fallback;

  // Keep the album's hue but normalise brightness: a dark cover otherwise
  // gives a tint too dim to read. The point is recognisability, not fidelity.
  int r = ((best >> 11) & 0x1F) << 3;
  int g = ((best >> 5) & 0x3F) << 2;
  int b = (best & 0x1F) << 3;
  const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (mx > 0 && mx < 230) {
    const float k = 230.0f / mx;
    r = static_cast<int>(r * k);
    g = static_cast<int>(g * k);
    b = static_cast<int>(b * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
  }
  return M5.Display.color565(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                             static_cast<uint8_t>(b));
}
