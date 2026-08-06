#pragma once

// Scanline and vignette overlay, applied by a mode to whatever region it just
// repainted.
//
// It is a per-region call rather than a whole-screen pass because there is no
// off-screen buffer to composite against: the only way to keep scanlines
// consistent is for each mode to re-apply them over what it drew.

#include <cstdint>

namespace crt {

bool enabled();
void setEnabled(bool on);

// Darkens alternate rows across the region, and shades the screen corners when
// the region reaches them.
void apply(int x, int y, int w, int h);

}  // namespace crt
