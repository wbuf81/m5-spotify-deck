#pragma once

// Draws album artwork scaled into the artwork region.
//
// This is where the design's main hardware risk lives: the target is 176px and
// Spotify serves 300px, which is not a power-of-two downscale. The emulator can
// prove the scaling is correct; only real hardware can prove it is fast enough
// and fits in heap.

#include <cstdint>

// Returns false if the artwork could not be drawn, in which case the caller is
// responsible for painting a flat fallback block.
bool drawArt(const char *path, int x, int y, int size);
