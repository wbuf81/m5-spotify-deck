#pragma once

// Draws album artwork scaled into the artwork region.
//
// This is where the design's main hardware risk lives: the target is 176px and
// Spotify serves 300px, which is not a power-of-two downscale. The emulator can
// prove the scaling is correct; only real hardware can prove it is fast enough
// and fits in heap.

#include <M5Unified.h>

#include <cstdint>

// Returns false if the artwork could not be drawn, in which case the caller is
// responsible for painting a flat fallback block.
// Reserves the shared artwork buffer. Call once at boot, before WiFi and TLS
// fragment the heap — a late allocation of this size is exactly what failed on
// hardware.
void initArtBuffer();

bool drawArt(const char *path, int x, int y, int size);

// Decodes artwork into any target — the panel, or a sprite a mode owns — fitted
// to `size`. Modes use this to build their own stylised renderings of the cover.
bool drawArtInto(LovyanGFX *dst, const char *path, int x, int y, int size);

// Samples the artwork and returns its most vivid colour, for tinting UI that
// should feel like it belongs to the current album.
//
// Decodes a thumbnail into an off-screen sprite and samples that. It
// deliberately does NOT read pixels back off the panel: ILI9342C readback is
// slow over SPI and unreliable on some units, and getting a wrong tint or a
// stall on every album change would be a miserable first-hardware surprise.
uint16_t sampleArtTint(const char *path, uint16_t fallback);
