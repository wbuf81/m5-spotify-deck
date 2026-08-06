#pragma once
#include "ViewMode.h"

// A tape deck whose reels transfer as the track plays, so reel radius literally
// is the progress. The most data-honest of the modes.
class CassetteMode : public ViewMode {
 public:
  const char *name() const override { return "cassette"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;

 private:
  float spin_ = 0.0f;
  uint32_t last_ms_ = 0;
  int last_sec_ = -1;
};
