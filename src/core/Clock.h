#pragma once

// One millisecond clock, defined explicitly per platform rather than relying on
// whichever millis() happens to be in scope.

#include <cstdint>

#if defined(EMULATOR)
#include <SDL.h>
inline uint32_t nowMs() { return static_cast<uint32_t>(SDL_GetTicks()); }
#else
#include <Arduino.h>
inline uint32_t nowMs() { return static_cast<uint32_t>(millis()); }
#endif
