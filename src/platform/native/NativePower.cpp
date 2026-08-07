#if defined(EMULATOR)

#include <cstdlib>

#include "../../core/Power.h"

// The desktop build has no battery; EMU_BATTERY overrides the reading so the
// visual suite can render every level without a drained pack.
void powerBegin() {}

PowerInfo readPower() {
  PowerInfo p;
  p.pct = 100;
  if (const char *v = std::getenv("EMU_BATTERY")) {
    p.pct = static_cast<int8_t>(std::atoi(v));
  }
  return p;
}

#endif  // EMULATOR
