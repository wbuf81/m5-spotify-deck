#pragma once
#include <M5Unified.h>

#include "ViewMode.h"

// The cover posterized to chunky blocks in an 8-bit palette. Full bleed, so the
// artwork is the entire screen.
class PixelAlbumMode : public ViewMode {
 public:
  const char *name() const override { return "pixel"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;
  void release() override;

 private:
  // Persistent row-blit sprite for the shimmer. Creating and deleting one
  // every frame worked, but the churn shredded the heap into fragments and
  // the JPEG decoder starved — covers stopped decoding after enough uptime.
  M5Canvas *shimmer_strip_ = nullptr;
  // The posterized cells, kept so the shimmer can redraw rows without another
  // JPEG decode. 80x40 cells = 6.4KB, static cost, no heap.
  uint16_t cells_[80 * 40] = {};
  bool have_cells_ = false;
  float clock_ = 0.0f;
  uint32_t last_ms_ = 0;
  int band_last_row_ = -1000;
};
