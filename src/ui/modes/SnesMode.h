#pragma once
#include <M5Unified.h>

#include "ViewMode.h"

// Mode 7: the album cover lying on a perspective floor plane, F-Zero style.
//
// The SNES's party trick was a background layer the PPU could rotate and
// scale per scanline, which is exactly what this fakes: every screen row below
// the horizon samples the cover texture at a depth-scaled position, so the
// cover reads as an infinite tiled plane rushing toward the viewer. Scroll
// speed follows volume, pausing freezes the plane, and rows fade toward the
// sky colour with distance because real Mode 7 games did exactly that to hide
// the horizon shimmer.
//
// A racer drives the plane: it weaves and drifts in depth on smooth
// sine-sum noise, scales with distance, and is simply drawn after the floor
// pass each frame — the floor repaint IS the erase.
class SnesMode : public ViewMode {
 public:
  const char *name() const override { return "snes"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;
  void release() override;

 private:
  M5Canvas *tex_ = nullptr;   // 64x64 cover texture, sampled per pixel
  uint16_t mip_[16 * 16] = {};  // box-filtered mip for the far rows
  M5Canvas *line_ = nullptr;  // one 320px scanline, pushed per row
  M5Canvas *car_ = nullptr;   // the racer, keyed, composited per scanline
  float clock_ = 0.0f;
  uint32_t last_ms_ = 0;
  int32_t last_v0_ = -1;  // quantised scroll; unchanged plane skips the redraw
};
