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
