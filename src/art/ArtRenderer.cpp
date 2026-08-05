#include "ArtRenderer.h"

#include <M5Unified.h>

#include <cstdio>
#include <vector>

namespace {

// Buffered decode on both platforms.
//
// The spec originally required streaming from SD so a cover never sat in heap
// whole. Two things changed that. This M5GFX version has no
// DataWrapperT<fs::FS> specialisation, so drawJpgFile cannot be handed the SD
// object at all; and the sizing that motivated streaming does not hold — we
// select the smallest image at or above the 176px artwork region, which is
// Spotify's 300px variant at roughly 25-40KB, against ~275KB of free heap.
//
// The download still streams straight to disk (see Esp32HttpClient), which was
// always the larger win: the file never sits in RAM while it is being fetched.
// Buffering only for the decode keeps one code path across both platforms, so
// the emulator exercises exactly what the device runs.
//
// If heap ever gets tight, the fix is a custom DataWrapper over stdio rather
// than a second code path.
bool readFile(const char *path, std::vector<uint8_t> *out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len <= 0) {
    std::fclose(f);
    return false;
  }
  out->resize(static_cast<size_t>(len));
  const size_t n = std::fread(out->data(), 1, out->size(), f);
  std::fclose(f);
  return n == out->size();
}

// Decodes a JPEG into any LovyanGFX target — the panel, or an off-screen
// sprite — fitted to `size`.
//
// scale 0.0 asks LovyanGFX to fit maxWidth/maxHeight, which is what makes the
// non-power-of-two 300 -> 176 reduction work without hardcoding the source
// dimensions. On the device the path lives under /sd, which Arduino's SD
// library exposes to stdio through the ESP-IDF VFS.
bool decodeInto(LovyanGFX *dst, const char *path, int x, int y, int size) {
  std::vector<uint8_t> jpg;
  if (!readFile(path, &jpg)) return false;
  return dst->drawJpg(jpg.data(), jpg.size(), x, y, size, size, 0, 0, 0.0f, 0.0f);
}

}  // namespace

bool drawArt(const char *path, int x, int y, int size) {
  if (!path || path[0] == '\0') return false;
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
