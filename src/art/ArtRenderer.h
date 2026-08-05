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

// Samples the drawn artwork and returns its most vivid colour, for tinting UI
// that should feel like it belongs to the current album.
//
// Reads back from the panel rather than from the JPEG, so it works regardless
// of how the image was decoded. Note readRect hands back byte-swapped RGB565.
uint16_t sampleArtTint(int x, int y, int size, uint16_t fallback);
