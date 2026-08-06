#include "Crt.h"

#include <M5Unified.h>

#include "Theme.h"

namespace crt {
namespace {
bool g_on = true;

// Every third row rather than every other: at 240 rows, halving the lines
// costs a third of the brightness and reads as a CRT. Every other row looks
// like a broken panel.
constexpr int PITCH = 3;
}  // namespace

bool enabled() { return g_on; }
void setEnabled(bool on) { g_on = on; }

void apply(int x, int y, int w, int h) {
  if (!g_on) return;

  const uint16_t line = theme::pal.bg;
  for (int yy = y + (y % PITCH == 0 ? 0 : PITCH - (y % PITCH)); yy < y + h;
       yy += PITCH) {
    // A solid background line at this pitch reads as a scanline gap without
    // needing alpha, which the panel does not have.
    M5.Display.drawFastHLine(x, yy, w, line);
  }
}

}  // namespace crt
