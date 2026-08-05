#pragma once

// Playback time formatting. Deliberately free of any display dependency so it
// can be unit tested on the host.

#include <cstdint>
#include <cstdio>

// "M:SS"
inline void formatElapsed(uint32_t ms, char *out, size_t cap) {
  const uint32_t total = ms / 1000;
  std::snprintf(out, cap, "%u:%02u", total / 60, total % 60);
}

// "-M:SS" counting down. Clamped at zero: progress can briefly exceed duration
// because the UI extrapolates between polls, and a negative remaining time
// would render as garbage.
inline void formatRemaining(uint32_t progress_ms, uint32_t duration_ms,
                            char *out, size_t cap) {
  const uint32_t rem = duration_ms > progress_ms ? duration_ms - progress_ms : 0;
  const uint32_t total = rem / 1000;
  std::snprintf(out, cap, "-%u:%02u", total / 60, total % 60);
}
