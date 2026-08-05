#include "ArtRenderer.h"

#include <M5Unified.h>

#include <cstdio>
#include <vector>

namespace {

// Reads the whole JPEG into RAM. Fine in the emulator, and fine on hardware for
// a 300px cover (~25KB), but the device path will stream from SD instead so that
// a 640px cover never has to fit in heap all at once.
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

}  // namespace

bool drawArt(const char *path, int x, int y, int size) {
  if (!path || path[0] == '\0') return false;

  std::vector<uint8_t> jpg;
  if (!readFile(path, &jpg)) return false;

  // scale 0.0 asks LovyanGFX to fit the image to maxWidth/maxHeight, which is
  // what makes the non-power-of-two 300 -> 176 reduction work without us
  // hardcoding the source dimensions.
  return M5.Display.drawJpg(jpg.data(), jpg.size(), x, y, size, size, 0, 0, 0.0f,
                            0.0f);
}

uint16_t sampleArtTint(int x, int y, int size, uint16_t fallback) {
  constexpr int GRID = 14;
  uint16_t best = fallback;
  float best_score = 0.0f;

  for (int gy = 0; gy < GRID; ++gy) {
    for (int gx = 0; gx < GRID; ++gx) {
      const int px = x + (size * gx) / GRID + (size / (GRID * 2));
      const int py = y + (size * gy) / GRID + (size / (GRID * 2));

      uint16_t raw = 0;
      M5.Display.readRect(px, py, 1, 1, &raw);
      // Panel bus order: the same byte swap the framebuffer dumper needs.
      const uint16_t c = static_cast<uint16_t>((raw >> 8) | (raw << 8));

      const int r = ((c >> 11) & 0x1F) << 3;
      const int g = ((c >> 5) & 0x3F) << 2;
      const int b = (c & 0x1F) << 3;

      const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      if (mx == 0) continue;

      const float sat = static_cast<float>(mx - mn) / mx;
      const float val = mx / 255.0f;

      // Favour vivid mid-to-bright colours. Weighting saturation above value
      // keeps it from always picking whatever is merely brightest, which on
      // most covers is a white or a blown-out highlight.
      const float score = (sat * sat) * (0.35f + 0.65f * val);
      if (score > best_score) {
        best_score = score;
        best = c;
      }
    }
  }

  // A cover with nothing vivid in it (monochrome sleeves, dark photography)
  // should not produce a muddy tint.
  if (best_score < 0.06f) return fallback;

  // Keep the album's hue but normalise its brightness. A dark cover otherwise
  // yields a tint too dim to read against the background, and the point of
  // sampling is recognisability, not literal fidelity.
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
