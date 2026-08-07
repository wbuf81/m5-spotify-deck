#pragma once
#include "ViewMode.h"

// The cover posterized to chunky blocks in an 8-bit palette. Full bleed, so the
// artwork is the entire screen.
class PixelAlbumMode : public ViewMode {
 public:
  const char *name() const override { return "pixel"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

};
