#pragma once

// Interactive review harness. Emulator only.
//
// Screenshots cannot show an animation, and waiting for a track to end to see
// the next mode is not a review loop. This lets every state and transition be
// summoned on demand: pin any view, scrub the clock, force a link failure,
// fire the like animation, override brightness.
//
// Enabled with EMU_HARNESS=1. Adds no code to the device build.

#if defined(EMULATOR)

#include <cstdint>

#include "../../core/AppState.h"

namespace harness {

// What the harness is currently overriding. Values that are not overridden are
// left to the normal logic.
struct Overrides {
  int link = -1;         // -1 = leave alone, else LinkStatus
  bool force_notrack = false;
  int brightness = -1;   // -1 = automatic
};

bool active();

// Reads the keyboard and applies actions. Returns the current overrides.
// `st` is mutated directly for things the harness fakes (likes, volume).
const Overrides &update(AppState *st, uint32_t now_ms);

// One-line status bar across the top, so it is always obvious what is pinned.
void drawOverlay();

bool overlayVisible();

// Printed once at startup.
void printKeymap();

}  // namespace harness

#endif  // EMULATOR
