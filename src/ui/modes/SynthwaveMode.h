#pragma once
#include <M5Unified.h>

#include "ViewMode.h"

// The horizon scene at full size, with the album art itself as the setting sun.
class SynthwaveMode : public ViewMode {
 public:
  const char *name() const override { return "synthwave"; }
  void enter(const AppState &st, const ViewCtx &ctx) override;
  void tick(const AppState &st, const ViewCtx &ctx, uint32_t now_ms) override;
  void release() override;

 private:
  M5Canvas *sun_ = nullptr;   // pre-rendered disc, re-blitted as it sinks
  float clock_ = 0.0f;
  uint32_t last_ms_ = 0;
  int last_sec_ = -1;
  int last_sun_y_ = -1000;
};
