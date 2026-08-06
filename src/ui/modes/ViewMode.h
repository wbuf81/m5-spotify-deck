#pragma once

// A full-screen presentation of what is playing.
//
// Modes must not composite off-screen: a 320x240 RGB565 buffer is 150KB and
// this board has no PSRAM. They draw straight to the panel, which is why the
// interface splits in two — enter() repaints everything that only changes with
// the track, tick() touches just the parts that move. A full-screen blit costs
// roughly 30ms over SPI, so redrawing wholesale every frame would both cap the
// frame rate and saturate the bus.

#include <cstdint>

#include "../../core/AppState.h"

struct ViewCtx {
  uint16_t tint;         // sampled from the album art
  const char *art_path;  // may be empty
};

class ViewMode {
 public:
  virtual ~ViewMode() = default;
  virtual const char *name() const = 0;

  // Full repaint. Called when the mode becomes active, the track changes, or
  // the palette changes.
  virtual void enter(const AppState &st, const ViewCtx &ctx) = 0;

  // Per-frame. Keep the touched area small.
  virtual void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) = 0;

  // Hand back any sprite memory; this mode is no longer showing.
  virtual void release() {}
};
